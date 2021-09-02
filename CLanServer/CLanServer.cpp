#include "CLanServer.h"
#include "CrashDump\CrashDump.h"
#include "Define\GGM_ERROR.h"
#include <process.h>
#include <tchar.h>

using namespace std;

namespace GGM
{
	unsigned int GGM::CLanServer::AcceptThread(LPVOID Param)
	{
		//////////////////////////////////////////////////////////////////////////////////
		// 0. AcceptThread ���ο��� ����� ���� �ʱ�ȭ
		//////////////////////////////////////////////////////////////////////////////////

		// Accept ������ �Լ��� static�̱� ������ ��������� �����ϰų� ����Լ��� ȣ���ϱ� ���� 
		// this �����͸� ���ڷ� �޾ƿ´�.
		CLanServer *pThis = (CLanServer*)Param;		

		// ��������
		SOCKET Listen = pThis->m_Listen;

		// IOCP �ڵ�
		HANDLE hIOCP = pThis->m_hWorkerIOCP;	

		// ���ǹ迭�� ������ ������
		LanSession *SessionArray = pThis->m_SessionArray;

		// ���� ������ �� �ε����� ������ �ִ� ����		
		CLockFreeStack<WORD> *pNextSlot = pThis->m_NextSlot;		
		
		// SessionID, ���� ���� ������ �߱��� �� �ĺ��� 
		ULONGLONG SessionID = 0;

		// ���� ���� ���� Ŭ���̾�Ʈ ���� ������
		ULONGLONG *pSessionCount = &(pThis->m_SessionCount);

		// �ִ� Accept�� �� �ִ� ���� ����
		DWORD MaxSessions = pThis->m_MaxSessions;		

		// ������ Ŭ���̾�Ʈ �ּ� ���� 
		SOCKADDR_IN ClientAddr;
		int AddrLen = sizeof(ClientAddr);	

		// ����͸��� AcceptTotal ����
		ULONGLONG *pAcceptTotal = &pThis->AcceptTotal;
		
		while (true)
		{
			//////////////////////////////////////////////////////////////////////////////////
			// 1. Accept �Լ� ȣ���Ͽ� Ŭ���̾�Ʈ ���Ӵ�� 
			//////////////////////////////////////////////////////////////////////////////////
			SOCKET client = accept(Listen, (SOCKADDR*)&ClientAddr, &AddrLen);

			// accept ������ ��� ���� ó�� �Լ��� OnError ȣ�����ش�.			
			if (client == INVALID_SOCKET)
			{	
				// ���� ���� ������ ���������� close�ߴٸ� Accept ������ ����
				DWORD error = WSAGetLastError();
				if (error == WSAEINTR || error == WSAENOTSOCK)
				{
					// �׽�Ʈ�� �ܼ� ��� ���߿� ������ ��
					_tprintf_s(_T("[SERVER] SERVER OFF! ACCEPT THREAD EXIT [ID %d]\n"), GetCurrentThreadId());				

					return 0;
				}

				pThis->OnError(WSAGetLastError(), _T("Accept Failed %d"));
				continue;
			}		

			++(*pAcceptTotal);

			//////////////////////////////////////////////////////////////////////////////////
			// 2. ���ӿ�û�� ������ ��ȿ�� Ȯ��
			//////////////////////////////////////////////////////////////////////////////////

			// ���� ���� ���� ������ �ִ� ���ǿ� �����ߴٸ� ���� ���´�.
			if (*pSessionCount == MaxSessions)
			{
				closesocket(client);				
				pThis->OnError(GGM_ERROR::TOO_MANY_CONNECTION, _T("Too Many Connection %d"));
				continue;
			}

			// Accept ���� ����ó�� �� �� �ִ� Ŭ���̾�Ʈ���� Ȯ��
			if ((pThis->OnConnectionRequest(ClientAddr)) == false)
			{
				// ���� �Ұ����� Ŭ���̾�Ʈ��� closesocket()
				// ���� �񵿱� I/O ������̹Ƿ� �ٷ� closesocket ȣ�� ����
				closesocket(client);			
				pThis->OnError(GGM_ERROR::ONCONNECTION_REQ_FAILED, _T("OnConnectionRequest false %d"));
				continue;
			}					

			//////////////////////////////////////////////////////////////////////////////////
			// 3. ��ȿ�� �����̹Ƿ� ���� ���� �迭�� ���� �߰�
			//////////////////////////////////////////////////////////////////////////////////

			// �ش� ���� ������ ������ �迭�� �ε����� ���´�.
			WORD  SessionSlot;
			bool bSuccess = pNextSlot->Pop(&SessionSlot);
			
			if (bSuccess == false)
			{
				closesocket(client);				
				pThis->OnError(GGM_ERROR::INDEX_STACK_POP_FAILED, _T("Index Stack Pop Failed %d"));
				continue;
			}			
			
			// ���� ���� ä���.				
			// �������ڸ��� ����� ������ �ٸ� �����忡 ���� ������ ������ �ֱ� ������ �׿� ���� ������ I/O Count �÷���						
			InterlockedIncrement(&(SessionArray[SessionSlot].IoCount));
			// SessionID ������ �ش� ������ �ε����� ��Ʈ�������� �����صд�.
			SessionArray[SessionSlot].SessionID = ((SessionID << 16) | SessionSlot);
			++SessionID;
			SessionArray[SessionSlot].SessionSlot = SessionSlot;
			SessionArray[SessionSlot].socket = client;
			SessionArray[SessionSlot].SentPacketCount = 0;
			SessionArray[SessionSlot].RecvQ.ClearRingBuffer();
			SessionArray[SessionSlot].SendQ.ClearLockFreeQueue();
			SessionArray[SessionSlot].IsSending = FALSE;
			SessionArray[SessionSlot].IsReleased = FALSE;
			SessionArray[SessionSlot].IsCanceled = false;

			// IOCP �� ���� ���� 
			HANDLE hRet = CreateIoCompletionPort((HANDLE)client, hIOCP, (ULONG_PTR)&(SessionArray[SessionSlot]), 0);

			if (hRet != hIOCP)
			{
				pThis->OnError(GetLastError(), _T("CreateIoCompletionPort Failed %d"));
				closesocket(client);				
				continue;
			}

			// ���� ���� Ŭ���̾�Ʈ �� ����			
			InterlockedIncrement(pSessionCount);

			//////////////////////////////////////////////////////////////////////////////////
			// 4. ���ӿϷ� �� ������ �������� �ؾ��� �۾� ����
			//////////////////////////////////////////////////////////////////////////////////

			// ���ӿϷ� �̺�Ʈ �ڵ鸵 �Լ� ȣ��		
			pThis->OnClientJoin(ClientAddr, SessionArray[SessionSlot].SessionID);				

			//////////////////////////////////////////////////////////////////////////////////
			// 5. RecvPost ���
			//////////////////////////////////////////////////////////////////////////////////
			
			// ���������� OnClientJoin���� �Ǹ� RecvPost ���
			// ���� RecvPost�� AcceptThread���� ������־�� ��			
			pThis->RecvPost(&SessionArray[SessionSlot]);			
			pThis->SessionReleaseLock(&SessionArray[SessionSlot]);
		}

		return 0;
	}

	unsigned int GGM::CLanServer::WorkerThread(LPVOID Param)
	{
		// Worker ������ �Լ��� static�̱� ������ ��������� �����ϰų� ����Լ��� ȣ���ϱ� ���� 
		// this �����͸� ���ڷ� �޾ƿ´�.
		CLanServer *pThis = (CLanServer*)Param;			

		// IOCP �ڵ�
		HANDLE hIOCP = pThis->m_hWorkerIOCP;

		// ���� �������� ����ü ������
		OVERLAPPED *pDummyOverlapped = &pThis->m_DummyOverlapped;

		// ����͸��� send tps
		volatile LONG *pSendTps = &pThis->SendTps;
	
		// ���� ���鼭 �۾� �Ѵ�.
		while (true)
		{
			// �Ϸ����� �� ����� ���� �������� �ʱ�ȭ �Ѵ�.
			DWORD BytesTransferred; // �ۼ��� ����Ʈ ��
			LanSession *pSession; // ���ø��� Ű�� ������ ���� ����ü ������
			OVERLAPPED *pOverlapped; // Overlapped ����ü �������� ������		

			// GQCS�� ȣ���ؼ� �ϰ��� ���⸦ ����Ѵ�.			
			bool bOk = GetQueuedCompletionStatus(hIOCP, &BytesTransferred, (PULONG_PTR)&pSession, (LPOVERLAPPED*)&pOverlapped, INFINITE);

			// ��Ŀ ������ �� ��������.
			//pThis->OnWorkerThreadBegin();			

			// SendPacket�Լ� ���ο��� PQCS�� SendPost��û �ߴٸ� �� ������ ó�� 
			if (pOverlapped == pDummyOverlapped)
			{
				if (pSession->SendQ.size() > 0)
					pThis->SendPost(pSession);

				// ���� ����ȭ�� ���� IOCount�÷ȴ� ���� �� ������ ����
				pThis->SessionReleaseLock(pSession);
				continue;
			}

			// Overlapped ����ü�� nullptr�̸� ������ �߻��� ���̴�.
			if(pOverlapped == nullptr)
			{
				DWORD Error = WSAGetLastError();
				if (bOk == FALSE && Error == ERROR_ABANDONED_WAIT_0)
				{
					// �������� ����ü�� ���ε� �����ڵ尡 ERROR_ABANDONED_WAIT_0��� �ܺο��� ���� ������� IOCP �ڵ��� ���� ���̴�.
					// ������ ����ó�� �����̹Ƿ� ���� ���ָ� �ȴ�.			
					
					// �׽�Ʈ�� �ܼ� ��� ���߿� ������ ��
					_tprintf_s(_T("[SERVER] SERVER OFF! WORKER THREAD EXIT [ID %d]\n"), GetCurrentThreadId());
				
					return 0;
				}

				pThis->OnError(WSAGetLastError(), _T("GQCS FAILED / OVERLAPPED NULL %d"));
				continue;
			}

			// pOverlapped�� nullptr�� �ƴ϶�� �ۼ��Ź���Ʈ ���� 0���� �˻��ؾ� �Ѵ�. 
			// IOCP���� �ۼ��� ����Ʈ���� (0 �̳� ��û�� ����Ʈ ��) �̿��� �ٸ� ���� ���� �� ����. 			
			// ���� �ۼ��� ����Ʈ ���� 0�� �ƴ϶�� ��û�� �񵿱� I/O�� ���������� ó���Ǿ����� �ǹ��Ѵ�.			
			if (BytesTransferred == 0 || (pSession->IsCanceled == true))
			{
				//0�� ������ ���ʿ��� FIN�� ���°ų� ������ �� ����̴�.
				//I/O Count�� 0���� �����ؼ� ������ �ڿ������� ������ �� �ֵ��� �Ѵ�.				
				pThis->SessionReleaseLock(pSession);				
				continue;						
			}

			// �Ϸ�� I/O�� RECV���� SEND���� �Ǵ��Ѵ�.
			// �������� ����ü ������ ��
			if (pOverlapped == &(pSession->RecvOverlapped))
			{
				// �Ϸ������� ���� ����Ʈ ����ŭ Rear�� �Ű��ش�.				
				pSession->RecvQ.RelocateWrite(BytesTransferred);

				// ��Ʈ��ũ Ŭ���� ������ �ϼ��� ��Ŷ�� �ִ��� Ȯ��
				// CheckPacket ���ο��� ��� �Ϸ�� ��Ŷ�� OnRecv�� ����
				pThis->CheckPacket(pSession);

				// RecvPost ȣ���ؼ� WSARecv ����
				pThis->RecvPost(pSession);				
			}
			else
			{
				// �Ϸ�� I/O�� SEND��� OnSend ȣ��
				//pThis->OnSend(pSession->SessionID, BytesTransferred);

				// OnSend�Ŀ��� �۽ſϷ� �۾��� ���־�� ��	
				
				// 1. �񵿱� I/O�� ��û�� ������ �� ����ȭ ���۸� �������ش�.
				ULONG PacketCount = pSession->SentPacketCount;
				InterlockedAdd(pSendTps, PacketCount);
				CPacket **PacketArray = pSession->PacketArray;				
				for (ULONG i = 0; i < PacketCount; i++)
				{							
					CPacket::Free(PacketArray[i]);
				}								

				pSession->SentPacketCount = 0;
				
				// 2. ��û�� �񵿱� Send�� �Ϸ�Ǿ����Ƿ� ���� �ش� ���ǿ� ���� Send�� �� �ְ� �Ǿ���
				//InterlockedBitTestAndReset(&(pSession->IsSending), 0);
				pSession->IsSending = FALSE;

				// 3. ���� ������ ���� ���� �ִ��� Ȯ���ؼ� �ִٸ� ����
				if (pSession->SendQ.size() > 0)
					pThis->SendPost(pSession);
			}

			// I/O Count�� �Ϲ������� �̰����� ���ҽ�Ų��. ( ������ ���� ���� ������ �߻��� ������ ���� ��Ŵ )
			// ���Ұ�� I/O COUNT�� 0�� �Ǹ� ������ �������Ѵ�. 				
			// 0���� ���� �����尡 �� ������ ������ �ؾ� �Ѵ�.			
			pThis->SessionReleaseLock(pSession);
			// ��Ŀ ������ �� ������
			//pThis->OnWorkerThreadEnd();
		}

		return 0;
	}

	void CLanServer::CheckPacket(LanSession * pSession)
	{
		CRingBuffer *pRecvQ = &(pSession->RecvQ);

		// ���� RECV ������ ��뷮 üũ
		int CurrentUsage = pRecvQ->GetCurrentUsage();
		int Result;

		CPacket Packet(0);
		char *pPacketbuf = (char*)Packet.GetBufferPtr();
		LAN_HEADER Header;

		// �����ۿ� �ּ��� ������� �̻��� �ִ��� ����Ȯ���Ѵ�.
		// ������� �̻��� ������ �������鼭 �����ϴ� �ϼ��� ��Ŷ �� �̾Ƽ� OnRecv�� ����
		LONG PacketCount = 0;
		while (CurrentUsage >= LAN_HEADER_LENGTH)
		{
			char *pPacket = pPacketbuf;

			// �ش� ���� �����ۿ��� �����ŭ PEEK
			Result = pRecvQ->Peek((char*)&Header, LAN_HEADER_LENGTH);

			if (Result != LAN_HEADER_LENGTH)
			{
				// �̰��� ������ �� �̻� �����ϸ� �ȵǴ� ��Ȳ ������ ���ܼ� ������ Ȯ������
				CCrashDump::ForceCrash();
				break;
			}

			CurrentUsage -= LAN_HEADER_LENGTH;

			// ���� �����ۿ� �ϼ��� ��Ŷ��ŭ ������ �ݺ��� Ż�� 
			if (CurrentUsage < Header.size)
				break;

			// �ϼ��� ��Ŷ�� �ִٸ� HEADER�� RecvQ���� �����ش�.
			pRecvQ->EraseData(LAN_HEADER_LENGTH);

			// �ϼ��� ��Ŷ�� �̾Ƽ� ����ȭ ���ۿ� ��´�.
			Result = pRecvQ->Dequeue(pPacket, Header.size);

			if (Result != Header.size)
			{
				// �̰��� ������ �� �̻� �����ϸ� �ȵǴ� ��Ȳ ������ ���ܼ� ������ Ȯ������
				CCrashDump::ForceCrash();
				break;
			}

			Packet.RelocateWrite(Header.size);

			// �ϼ��� ��Ŷ�� �����ϹǷ� OnRecv�� �ϼ� ��Ŷ�� �����Ѵ�.
			OnRecv(pSession->SessionID, &Packet);

			// ���� �������� ��� ����� ��Ŷ �����ŭ ����
			CurrentUsage -= Header.size;

			// ����ȭ ���۸� ��Ȱ���ϱ� ���� Rear�� Front �ʱ�ȭ
			Packet.InitBuffer();

			PacketCount++;
		}

		InterlockedAdd(&RecvTps, PacketCount);
	}

	bool GGM::CLanServer::SendPost(LanSession * pSession)
	{
		///////////////////////////////////////////////////////////////////
		// 0. Send ���� �÷��� üũ 
		///////////////////////////////////////////////////////////////////
		while (InterlockedExchange(&(pSession->IsSending), TRUE) == FALSE)
		{			
			CLockFreeQueue<CPacket*> *pSendQ = &(pSession->SendQ);

			// ���� SendQ ��뷮�� Ȯ���ؼ� ���� ���� �ִ��� �ٽ� �ѹ� Ȯ���غ���.
			// ���� ���� �ִ��� �˰� ���Դµ� ���� ���� ���� ���� �ִ�.
			ULONG CurrentUsage = pSendQ->size();

			// �������� ���ٸ� Send �÷��� �ٽ� �ٲپ� �ְ� ����
			if (CurrentUsage == 0)
			{
				// ���� �� �κп��� �÷��׸� �ٲٱ����� ���ؽ�Ʈ ����Ī�� �Ͼ�ٸ� ������ �ȴ�.
				// �ٸ� �����尡 ���� ���� �־��µ� �÷��װ� ����־ �� ������ ���� �ִ�. 

				//InterlockedBitTestAndReset(&(pSession->IsSending), 0);					
				pSession->IsSending = FALSE;
				
				// ���� ������ ������ �߻��ߴٸ� �� �ʿ��� ������ �Ѵ�.
				// �׷��� ������ �ƹ��� ������ �ʴ� ��Ȳ�� �߻��Ѵ�.
				if (pSendQ->size() > 0)
					continue; 

				return true;
			}

			///////////////////////////////////////////////////////////////////
			// 1. WSASend�� Overlapped ����ü �ʱ�ȭ
			///////////////////////////////////////////////////////////////////
			ZeroMemory(&(pSession->SendOverlapped), sizeof(OVERLAPPED));

			///////////////////////////////////////////////////////////////////
			//2. WSABUF ����ü �ʱ�ȭ
			///////////////////////////////////////////////////////////////////

			// SendQ���� ��Ŷ������ ����ȭ���� �����Ͱ� ����Ǿ� �ִ�.
			// �����͸� SendQ���� ��ť�ؼ� �ش� ����ȭ ���۰� ������ �ִ� ��Ŷ�� �����͸� WSABUF�� �����ϰ�, ��Ƽ� ������.
			// ȥ�������� [SendQ : ����ȭ ������ ������ (CPacket*)] [ WSABUF : ����ȭ���۳��� ���������� (char*)]
			// WSABUF�� �ѹ��� ��� ���� ��Ŷ�� ������ �������� 100 ~ 500�� ���̷� ���Ѵ�.
			// WSABUF�� ������ �ʹ� ���� ������ �ý����� �ش� �޸𸮸� �� �ɱ� ������ �޸𸮰� �̽��� �߻��� �� �ִ�. 
			// ����ȭ������ �����Ϳ� �۽��� ������ �����ߴٰ� �Ϸ������� �߸� �޸� ���� 
			
			// ����ȭ ���۳��� ��Ŷ �޸� ������(char*)�� ���� WSABUF ����ü �迭
			WSABUF        wsabuf[MAX_PACKET_COUNT];	

			// SendQ���� �̾Ƴ� ����ȭ ������ ������(CPacket*)�� ���� �迭
			CPacket **PacketArray = pSession->PacketArray;			

			// SendQ���� �ѹ��� �۽� ������ ��Ŷ�� �ִ� ������ŭ ����ȭ ������ �����͸� Dequeue�Ѵ�.		
			// ���Ǻ��� ������ �ִ� ����ȭ ���� ������ �迭�� �װ��� �����Ѵ�.
			// Peek�� �� �� �Ϸ����� ���Ŀ� Dequeue�� �ϸ� memcpy�� �߰������� �Ͼ�Ƿ� �޸𸮸� ����ؼ� Ƚ���� �ٿ���.			
			
			// ���� ť�� ����ִ� �������� ������ �ִ�ġ�� �ʰ��ߴٸ� �������ش�.
			if (CurrentUsage > MAX_PACKET_COUNT)
				CurrentUsage = MAX_PACKET_COUNT;			

			for (ULONG i = 0; i < CurrentUsage; i++)
				pSession->SendQ.Dequeue(&PacketArray[i]);		
			
			// ���� ��Ŷ�� ������ ����ߴٰ� ���߿� �Ϸ������� ���� �޸𸮸� �����ؾ� �Ѵ�.			
			DWORD PacketCount = pSession->SentPacketCount = CurrentUsage;

			// ��Ŷ ������ŭ �ݺ��� ���鼭 ���� ����ȭ ���� ���� ��Ŷ ���� �����͸� WSABUF����ü�� ����
			for (DWORD i = 0; i < PacketCount; i++)
			{
				wsabuf[i].buf = (char*)PacketArray[i]->GetBufferPtr();
				wsabuf[i].len = PacketArray[i]->GetCurrentUsage();
			}

			///////////////////////////////////////////////////////////////////
			// 3. WSASend ����ϱ� ���� I/O ī��Ʈ ����
			///////////////////////////////////////////////////////////////////

			InterlockedIncrement(&(pSession->IoCount));

			///////////////////////////////////////////////////////////////////
			// 4. WSASend ��û
			///////////////////////////////////////////////////////////////////
			DWORD bytesSent = 0;
			int Result = WSASend(pSession->socket, wsabuf, PacketCount, &bytesSent, 0, &(pSession->SendOverlapped), nullptr);

			///////////////////////////////////////////////////////////////////
			// 5. WSASend ��û�� ���� ����ó��
			///////////////////////////////////////////////////////////////////
			DWORD Error = WSAGetLastError();

			if (Result == SOCKET_ERROR)
			{
				if (Error != WSA_IO_PENDING)
				{
					// Error �ڵ尡 WSAENOBUFS��� ���� ����
					if (Error == WSAENOBUFS)
						OnError(WSAENOBUFS, _T("WSAENOBUFS"));					

					// WSASend�� WSA_IO_PENDING �̿��� ������ ���� �Լ� ȣ���� �����Ѱ��̴�.
					// I/O Count ���ҽ�Ų��.					
					SessionReleaseLock(pSession);
				}
			}

			// Disconnect �Լ��� ȣ��Ǿ I/O�� ��� ��ҵǾ���Ѵٸ� ��� �� ��Ŀ�����尡 ��û�� I/O�� ����Ѵ�.
			if (pSession->IsCanceled == true)
				CancelIoEx((HANDLE)pSession->socket, nullptr);

			return true;
		}

		return true;
	}

	bool GGM::CLanServer::RecvPost(LanSession * pSession)
	{
		///////////////////////////////////////////////////////////////////
		// 0. I/O ��û�� �ϱ� ���� ���� �������� ���������� �˻��Ѵ�.
		///////////////////////////////////////////////////////////////////

		CRingBuffer *pRecvQ = &(pSession->RecvQ);
		int CurrentSpare = pRecvQ->GetCurrentSpare();

		///////////////////////////////////////////////////////////////////
		// 1. WSARecv�� Overlapped ����ü �ʱ�ȭ
		///////////////////////////////////////////////////////////////////

		ZeroMemory(&(pSession->RecvOverlapped), sizeof(OVERLAPPED));

		///////////////////////////////////////////////////////////////////
		//2. WSABUF ����ü �ʱ�ȭ
		///////////////////////////////////////////////////////////////////

		// WSABUF�� �ΰ� ����ϴ� ������ �����۰� �߰��� �����Ǿ��� ������ �ѹ��� �� �ޱ� �����̴�.
		WSABUF wsabuf[2];
		int RecvBufCount = 1;
		int RecvSize = pRecvQ->GetSizeWritableAtOnce();

		// ù��° ���ۿ� ���� ������ ����ü�� ����Ѵ�.
		wsabuf[0].buf = pRecvQ->GetWritePtr();
		wsabuf[0].len = RecvSize;

		// ���� �����۰� �߸� ���¶�� �ΰ��� ���ۿ� ������ �޴´�.
		if (CurrentSpare > RecvSize)
		{
			RecvBufCount = 2;
			wsabuf[1].buf = pRecvQ->GetBufferPtr();
			wsabuf[1].len = CurrentSpare - RecvSize;
		}

		//////////////////////////////////////////////////////////////////////
		// 3. WSARecv ������� I/O ī��Ʈ ����, �ٸ� �����忡���� �����ϴ� ����̹Ƿ� ���� �ʿ��ϴ�.
		//////////////////////////////////////////////////////////////////////
		InterlockedIncrement(&(pSession->IoCount));

		///////////////////////////////////////////////////////////////////
		// 4. I/O ��û
		///////////////////////////////////////////////////////////////////
		DWORD BytesRead = 0;		
		DWORD Flags = 0;
		int Result = WSARecv(pSession->socket, wsabuf, RecvBufCount, &BytesRead, &Flags, &(pSession->RecvOverlapped), nullptr);

		///////////////////////////////////////////////////////////////////
		// 5. I/O ��û�� ���� ���� ó��
		///////////////////////////////////////////////////////////////////	
		if (Result == SOCKET_ERROR)
		{
			DWORD Error = WSAGetLastError();
			if (Error != WSA_IO_PENDING)
			{
				// �����ڵ尡 WSAENOBUFS��� ��������
				if (Error == WSAENOBUFS)
					OnError(WSAENOBUFS, _T("WSAENOBUFS"));

				// WSARecv�� WSA_IO_PENDING �̿��� ������ ���� �Լ� ȣ���� �����Ѱ��̴�.
				// I/O Count ���ҽ�Ų��.						
				SessionReleaseLock(pSession);
			}
		}

		// Disconnect �Լ��� ȣ��Ǿ I/O�� ��� ��ҵǾ���Ѵٸ� ��� �� ��Ŀ�����尡 ��û�� I/O�� ����Ѵ�.
		if (pSession->IsCanceled == true)
			CancelIoEx((HANDLE)pSession->socket, nullptr);

		return true;

	}

	void CLanServer::ReleaseSession(LanSession * pSession)
	{	
		// ReleaseFlag �� TRUE�� �Ǹ� � �����嵵 �� ���ǿ� �����ϰų� ������ �õ��� �ؼ��� �ȵȴ�.
		// IoCount�� ReleaseFlag�� 4����Ʈ�� ���޾Ƽ� ��ġ�� �����Ƿ� �Ʒ��� ���� ���Ͷ��Լ� ȣ���Ѵ�.
		// IoCount(LONG) == 0 ReleaseFlag(LONG) == 0 >> ���Ͷ� ������ >>IoCount(LONG) == 0 ReleaseFlag(LONG) == 1
		if (InterlockedCompareExchange64((volatile LONG64*)&(pSession->IoCount), 0x0000000100000000, FALSE) != FALSE)
			return;

		ULONGLONG SessionID = pSession->SessionID;
		pSession->SessionID = 0xffffffffffffffff;

		// ���� ����ȭ ���۸� �����Ҵ� �� Send�ϴ� ���߿� ���������� Release�� �ؾ��Ѵٸ�
		// �޸𸮸� ������ �־�� �Ѵ�.
		ULONG PacketCount = pSession->SentPacketCount;
		CPacket **PacketArray = pSession->PacketArray;
		if (PacketCount > 0)
		{					
			for (WORD i = 0; i < PacketCount; i++)				
				CPacket::Free(PacketArray[i]);			
		}		

		// ������ ���� ���� ��� ����	
		ULONG GarbagePacketCount = pSession->SendQ.size();
		if (GarbagePacketCount > 0)
		{			
			for (ULONG i = 0; i < GarbagePacketCount; i++)
			{
				CPacket *pGarbagePacket;
				pSession->SendQ.Dequeue(&pGarbagePacket);
				CPacket::Free(pGarbagePacket);
			}
		}
		
		// ������ �޸𸮸� ���� �������� �ʱ� ������ ������ ��밡���ϴٴ� ���¸� �ٲ۴�.
		// socket�� INVALID_SOCKET�̸� ��밡���� ���̴�.		
		closesocket(pSession->socket);			
		pSession->socket = INVALID_SOCKET;				
		
		// �ش� ���ǿ� ���� ��� ���ҽ��� �����Ǿ����Ƿ� �̺�Ʈ �ڵ鸵 �Լ� ȣ��
		OnClientLeave(SessionID);

		// �ش� ������ �ε����� ���ÿ� �ִ´�. ( ��� ���� �ε����� ����� ���� )
		m_NextSlot->Push(pSession->SessionSlot);

		// ���� ���� ���� Ŭ���̾�Ʈ �� ����
		InterlockedDecrement(&m_SessionCount);
	}

	bool GGM::CLanServer::SessionAcquireLock(LanSession * pSession, ULONGLONG LocalSessionID)
	{
		// �� �Լ��� ȣ���ߴٴ� ���� ���� �������� �ش� ������ ����ϰڴٴ� �ǹ��̴�.				
		// �� ������ ���� � ���������� ������ ���ǿ� �����Ѵٴ� �ǹ̷�, I/O Count�� ������Ų��.
		// I/O Count�� ���� ������������ ������ �Ǵ� ���� ���� �� �ִ�.
		// ���� I/O Count�� �������״µ� 1�̶�� �� �̻��� ���� ������ ���ǹ��ϴ�.
		ULONGLONG RetIOCount = InterlockedIncrement(&(pSession->IoCount));

		if (RetIOCount == 1 || pSession->IsReleased == TRUE || pSession->SessionID != LocalSessionID)
			return false;

		return true;
	}

	void GGM::CLanServer::SessionReleaseLock(LanSession * pSession)
	{
		// ���ǿ� ���� ������ ��� �������Ƿ� ������ �ø� I/O ī��Ʈ�� ���� ��Ų��.				
		LONG IoCount = InterlockedDecrement(&(pSession->IoCount));
		if (IoCount <= 0)
		{
			if (IoCount < 0)
			{
				OnError(GGM_ERROR::NEGATIVE_IO_COUNT, _T("[CLanServer] SessionReleaseLock() IO COUNT NEGATIVE %d"));
				CCrashDump::ForceCrash();
			}

			ReleaseSession(pSession);
		}
	}

	bool CLanServer::WSAInit(TCHAR * BindIP, USHORT port, bool IsNoDelay)
	{		
		WSADATA wsa;		
		if (int SocketError = WSAStartup(MAKEWORD(2, 2), &wsa))
		{
			OnError(SocketError, _T("WSAStartup Failed"));
			return false;
		}
		
		SOCKADDR_IN m_ServerAddr;
		ZeroMemory(&m_ServerAddr, sizeof(m_ServerAddr));
		m_ServerAddr.sin_port = htons(port);
		m_ServerAddr.sin_family = AF_INET;

		// BindIP�� nullptr�� �����ϸ� INADDR_ANY�� ���ε�
		// ���ε��� �ּҸ� �����ߴٸ� �ش��ּҷ� �ּ� ����
		if (BindIP == nullptr)
			m_ServerAddr.sin_addr.s_addr = htonl(INADDR_ANY);
		else
			InetPton(AF_INET, BindIP, &(m_ServerAddr.sin_addr));
	
		m_Listen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

		if (m_Listen == INVALID_SOCKET)
		{
			OnError(WSAGetLastError(), _T("Listen socket Creation Failed"));
			return false;
		}
		
		if (IsNoDelay == true)
		{
			int iResult = setsockopt(
				m_Listen,
				IPPROTO_TCP,
				TCP_NODELAY,
				(const char*)&IsNoDelay,
				sizeof(IsNoDelay)
			);

			if (iResult == SOCKET_ERROR)
			{
				OnError(WSAGetLastError(), _T("setsockopt [TCP_NODELAY] Failed"));
				return false;
			}
		}
	
		LINGER linger = { 1,0 };
		int Result = setsockopt(m_Listen, SOL_SOCKET, SO_LINGER, (const char*)&linger, sizeof(linger));

		if (Result == SOCKET_ERROR)
		{
			OnError(WSAGetLastError(), _T("setsockopt [SO_LINGER] Failed"));
			return false;
		}
		
		bool KeepAliveOn = true;

		Result = setsockopt(m_Listen, SOL_SOCKET, SO_KEEPALIVE, (const char*)&KeepAliveOn, sizeof(KeepAliveOn));

		if (Result == SOCKET_ERROR)
		{
			OnError(WSAGetLastError(), _T("setsockopt [SO_KEEPALIVE] Failed"));
			return false;
		}

		Result = bind(m_Listen, (SOCKADDR*)&m_ServerAddr, sizeof(m_ServerAddr));

		if (Result == SOCKET_ERROR)
		{
			OnError(WSAGetLastError(), _T("bind Failed"));
			return false;
		}
		
		Result = listen(m_Listen, SOMAXCONN);

		if (Result == SOCKET_ERROR)
		{
			OnError(WSAGetLastError(), _T("listen Failed"));
			return false;
		}

		return true;
	}

	bool CLanServer::SessionInit(WORD MaxSessions)
	{
		// �ִ� ���� ���� ����� ���� 
		m_MaxSessions = MaxSessions;

		// �ִ� ���� ���� ��ŭ �迭 �����Ҵ�
		m_SessionArray = new LanSession[MaxSessions];

		if (m_SessionArray == nullptr)
		{
			OnError(GetLastError(), _T("MEM Alloc Failed"));
			return false;
		}

		for (DWORD i = 0; i < MaxSessions; i++)
		{
			// Session ��ü�� �迭�� ���������Ƿ� ���ο� ���Ե� ��ü�� ���� �ʱ�ȭ�� �ʿ��� ��ü���� �ʱ�ȭ���ش�.
			// �ϴ��� ������ť�� �ʱ�ȭ�� �ʿ�
			m_SessionArray[i].SendQ.InitLockFreeQueue(0, MAX_LOCK_FREE_Q_SIZE);
		}

		// ���� �迭 �ε����� ������ ����
		m_NextSlot = new CLockFreeStack<WORD>(MaxSessions);

		if (m_NextSlot == nullptr)
		{
			OnError(GetLastError(), _T("MEM Alloc Failed"));
			return false;
		}

		// ó���̴ϱ� ��� �ε����� �־��ش�.	
		for (WORD i = 0; i < MaxSessions; i++)
		{
			m_NextSlot->Push(i);
		}

		return true;
	}

	bool CLanServer::ThreadInit(DWORD ConcurrentThreads, DWORD MaxThreads)
	{
		// ��Ŀ������� ������ IOCP ����
		m_hWorkerIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, ConcurrentThreads);

		if (m_hWorkerIOCP == NULL)
		{
			OnError(GetLastError(), _T("CreateIoCompletionPort Failed"));
			return false;
		}
		
		m_ThreadHandleArr = new HANDLE[MaxThreads + 1];

		if (m_ThreadHandleArr == nullptr)
		{
			OnError(GetLastError(), _T("MEM Alloc Failed"));
			return false;
		}
	
		m_ThreadIDArr = new DWORD[MaxThreads + 1];

		if (m_ThreadIDArr == nullptr)
		{
			OnError(GetLastError(), _T("MEM Alloc Failed"));
			return false;
		}		

		// ����ڰ� ��û�� ������ ��Ŀ ������(���ý��� ���� �ƴ�) ����
		for (DWORD i = 0; i < MaxThreads; i++)
		{
			m_ThreadHandleArr[i] = (HANDLE)_beginthreadex(
				nullptr,
				0,
				CLanServer::WorkerThread, // ��Ŀ ������ �Լ� Ŭ���� ���ο� private static���� ����Ǿ�����
				this, // ��Ŀ������ �Լ��� static���� ����Ǿ������Ƿ� �ٸ� ��� ������ �Լ��� �����ϱ� ���ؼ� �ش� �ν��Ͻ��� this �����Ͱ� �ʿ���
				0,
				(unsigned int*)&m_ThreadIDArr[i]
			);

			if (m_ThreadHandleArr[i] == NULL)
			{
				OnError(GetLastError(), _T("_beginthreadex Failed"));
				return false;
			}
		}	
		
		m_ThreadHandleArr[MaxThreads] = (HANDLE)_beginthreadex(
			nullptr,
			0,
			CLanServer::AcceptThread, // Accept ������ �Լ� Ŭ���� ���ο� private static���� ����Ǿ�����
			this, // Accept ������ �Լ��� static���� ����Ǿ������Ƿ� �ٸ� ��� ������ �Լ��� �����ϱ� ���ؼ� �ش� �ν��Ͻ��� this �����Ͱ� �ʿ���
			0,
			(unsigned int*)&m_ThreadIDArr[MaxThreads]
		);

		if (m_ThreadHandleArr[MaxThreads] == NULL)
		{
			OnError(GetLastError(), _T("_beginthreadex Failed"));
			return false;
		}		

		m_IOCPThreadsCount = MaxThreads;

		return true;
	}

	bool GGM::CLanServer::Start(TCHAR * BindIP, USHORT port, DWORD ConcurrentThreads, DWORD MaxThreads, bool IsNoDelay, WORD MaxSessions)
	{	
		bool IsSuccess = WSAInit(BindIP, port, IsNoDelay);

		if (IsSuccess == false)
			return false;

		IsSuccess = CPacket::CreatePacketPool(0);

		if (IsSuccess == false)
			return false;

		IsSuccess = SessionInit(MaxSessions);

		if (IsSuccess == false)
			return false;

		IsSuccess = ThreadInit(ConcurrentThreads, MaxThreads);

		if (IsSuccess == false)
			return false;
	
		return true;
	}

	void GGM::CLanServer::Stop()
	{
		////////////////////////////////////////////////////////////////////
		// ���� ���� ���� 
		// 1. �� �̻� accept�� ���� �� ������ ���������� �ݴ´�.
		// 2. ��� ���� ������ �����Ѵ�.		
		// 3. ��� ������ I/O COUNT�� 0�� �ǰ� Release�� ������ ����Ѵ�.
		// 4. ��Ŀ������ �����Ų��.
		// 5. �������� ����ߴ� ��Ÿ ���ҽ����� �����Ѵ�.
		////////////////////////////////////////////////////////////////////	
			
		// Aceept ������ ����
		closesocket(m_Listen);
		m_Listen = INVALID_SOCKET;

		// ���� ��� 
		WaitForSingleObject(m_ThreadHandleArr[m_IOCPThreadsCount], INFINITE);
		CloseHandle(m_ThreadHandleArr[m_IOCPThreadsCount]);
		
		LanSession *pSession = m_SessionArray;
		for(DWORD i=0; i< m_MaxSessions ; i++)
		{			
			if(pSession[i].socket != INVALID_SOCKET)
			{	
				Disconnect(pSession[i].SessionID);
			}			
		}		
		
		// ������ ���� ������ ������ ����Ѵ�.
		while (m_SessionCount > 0)
		{
			Sleep(100);
		}

		// ��Ŀ ������ IOCP �ڵ��� �ݴ´�. �� ������ ��Ŀ�����尡 ��� ����ȴ�.
		CloseHandle(m_hWorkerIOCP);		

		// ��Ŀ�����尡 ��� ����� ������ ����Ѵ�.
		WaitForMultipleObjects(m_IOCPThreadsCount, m_ThreadHandleArr, TRUE, INFINITE);
		for(DWORD i=0; i< m_IOCPThreadsCount; i++)
			CloseHandle(m_ThreadHandleArr[i]);	

		// ��Ÿ ���ҽ� ����
		delete[] m_ThreadHandleArr;
		delete[] m_ThreadIDArr;	
		delete[] m_SessionArray;	
		delete m_NextSlot;
		CPacket::DeletePacketPool();

		// 
		WSACleanup();
	
		// ���������� ������ ���� ���� ����ÿ� ���� ������ ����� �Ǿ����� Ȯ���ϱ� ���� �뵵
		_tprintf_s(_T("[SERVER OFF LAST INFO] SESSION ALIVE COUNT : %lld\n\n"), m_SessionCount);		
	}

	ULONGLONG GGM::CLanServer::GetSessionCount() const
	{
		return m_SessionCount;
	}

	bool GGM::CLanServer::Disconnect(ULONGLONG SessionID)
	{
		// �� ���Ǻ��� ����� ���� ���̵��� �ֻ��� 2����Ʈ���� �ش� ������ ����� �迭�� �ε����� ���õǾ��ִ�.	
		WORD SessionSlot = (WORD)SessionID;
		LanSession *pSession = &m_SessionArray[SessionSlot];			

		// �ܼ� ������ ���� �Ͱ� ������ ������ �ϴ� ���� �ٸ� ���̴�.		
		// CancelIoEx�� �̿��� �ڿ������� I/O Count�� 0�� �ǵ��� �����Ͽ� ������ �������Ѵ�.
		do
		{
			// ���ǿ� ������ �õ��Ѵ�.
			// ���� ���ٿ� �����ϸ� �׳� ������.
			if (SessionAcquireLock(pSession, SessionID) == false)
				break;

			pSession->IsCanceled = true;
			CancelIoEx((HANDLE)pSession->socket, nullptr);

		} while (0);

		// ���ǿ� ������ �Ϸ������� �˸�
		SessionReleaseLock(pSession);
		return true;
	}

	bool GGM::CLanServer::SendPacket(ULONGLONG SessionID, CPacket *pPacket)
	{
		// �� ���Ǻ��� ����� ���� ���̵��� ������ 2����Ʈ���� �ش� ������ ����� �迭�� �ε����� ���õǾ��ִ�.	
		WORD SessionSlot = (WORD)SessionID;
		LanSession *pSession = &m_SessionArray[SessionSlot];
		bool Result;		
		
		do
		{
			// ���ǿ� ������ �õ��Ѵ�.
			// ���� ���ٿ� �����ϸ� (�̹� �����尡 ������ ���̶��) �׳� ������.
			if (SessionAcquireLock(pSession, SessionID) == false)
			{
				Result = true;
				break;
			}

			// ������ �������� ����ȭ ���۸� �����Ҵ��ؼ� �Ѱ��ش�. 
			// ��Ʈ��ũ ���������� ����ȭ ������ ���� �տ� ��Ʈ��ũ ������ ����� ���δ�.
			LAN_HEADER Header;
			Header.size = pPacket->GetCurrentUsage() - LAN_HEADER_LENGTH;			
			pPacket->EnqueueHeader((char*)&Header);

			// SendQ�� ��Ʈ��ũ ��Ŷ�� �����͸� ��´�.	
			pPacket->AddRefCnt();
			Result = pSession->SendQ.Enqueue(pPacket);
			if (Result == false)
			{
				CCrashDump::ForceCrash();			
			}

			// SendFlag�� Ȯ���غ��� Send���� �ƴ϶�� PQCS�� WSASend ��û
			// �̷��� �����ν�, ���������� WSASend�� ȣ���Ͽ� ������Ʈ�� �������� ���� ���� �� �ִ�.
			// �׷��� ��Ŷ ���伺�� ������
			if (InterlockedAnd(&pSession->IsSending, 1) == TRUE)
				break;

			PostQueuedCompletionStatus(m_hWorkerIOCP, 0, (ULONG_PTR)pSession, &m_DummyOverlapped);

			// PQCS�� �ϰ����� IOCount�� �ٷ� ���� �ʰ� �Ϸ��������� ��´�.
			return true;	

		} while (0);		

		// ���ǿ� ������ �Ϸ������� �˸�
		SessionReleaseLock(pSession);		

		return true;
	}	
}