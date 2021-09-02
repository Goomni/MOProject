#include "CNetServer.h"
#include <tchar.h>
#include <strsafe.h>
#include "Define\GGM_ERROR.h"
#include "Logger\Logger.h"

namespace GGM
{
	unsigned int __stdcall GGM::CNetServer::AcceptThread(LPVOID Param)
	{
		//////////////////////////////////////////////////////////////////////////////////
		// 0. AcceptThread ���ο��� ����� ���� �ʱ�ȭ
		//////////////////////////////////////////////////////////////////////////////////

		// Accept ������ �Լ��� static�̱� ������ ��������� �����ϰų� ����Լ��� ȣ���ϱ� ���� 
		// this �����͸� ���ڷ� �޾ƿ´�.
		CNetServer *pThis = (CNetServer*)Param;

		// ��������
		SOCKET Listen = pThis->m_Listen;

		// IOCP �ڵ�
		HANDLE hIOCP = pThis->m_hWorkerIOCP;

		// ���ǹ迭�� ������ ������
		NetSession *SessionArray = pThis->m_SessionArray;

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

		// ����͸��� Accept
		ULONGLONG *pAcceptTotal = &(pThis->m_AcceptTotal);
		ULONGLONG *pAcceptTPS = &(pThis->m_AcceptTPS);		

		while (true)
		{
			//////////////////////////////////////////////////////////////////////////////////
			// 1. Accept �Լ� ȣ���Ͽ� Ŭ���̾�Ʈ ���Ӵ�� 
			//////////////////////////////////////////////////////////////////////////////////
			SOCKET client = accept(Listen, (SOCKADDR*)&ClientAddr, &AddrLen);		

			// ����͸��� accept total ���� ++
			(*pAcceptTotal)++;

			// ����͸��� accept TPS ���� ++
			InterlockedIncrement(pAcceptTPS);

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

				pThis->OnError(WSAGetLastError(), _T("Accept Failed"));
				continue;
			}

			//////////////////////////////////////////////////////////////////////////////////
			// 2. ���ӿ�û�� ������ ��ȿ�� Ȯ��
			//////////////////////////////////////////////////////////////////////////////////

			// ���� ���� ���� ������ �ִ� ���ǿ� �����ߴٸ� ���� ���´�.
			if (*pSessionCount == MaxSessions)
			{
				closesocket(client);
				client = INVALID_SOCKET;
				pThis->OnError(GGM_ERROR::TOO_MANY_CONNECTION, _T("Too Many Connection Error Code[%d]"));
				continue;
			}

			// Accept ���� ����ó�� �� �� �ִ� Ŭ���̾�Ʈ���� Ȯ��
			if ((pThis->OnConnectionRequest(ClientAddr)) == false)
			{
				// ���� �Ұ����� Ŭ���̾�Ʈ��� closesocket()
				// ���� �񵿱� I/O ������̹Ƿ� �ٷ� closesocket ȣ�� ����
				closesocket(client);
				client = INVALID_SOCKET;
				pThis->OnError(GGM_ERROR::ONCONNECTION_REQ_FAILED, _T("OnConnectionRequest false Error Code[%d]"));
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
				client = INVALID_SOCKET;
				pThis->OnError(0, _T("Index Stack Pop Failed"));
				continue;
			}			

			// ���� ���� ä���.				
			// SessionID ������ �ش� ������ �ε����� ��Ʈ�������� �����صд�.			
			// �������ڸ��� ����� ������ �ٸ� �����忡 ���� ������ ������ �ֱ� ������ �׿� ���� ������ I/O Count�� ���������ش�.	
			InterlockedIncrement(&(SessionArray[SessionSlot].IoCount));	
			SessionArray[SessionSlot].SessionID = ((SessionID << 16) | SessionSlot);	
			++SessionID;
			SessionArray[SessionSlot].SessionSlot = SessionSlot;
			SessionArray[SessionSlot].socket = client;
			SessionArray[SessionSlot].SentPacketCount = 0;
			SessionArray[SessionSlot].RecvQ.ClearRingBuffer();
			SessionArray[SessionSlot].SendQ.ClearLockFreeQueue();	
			SessionArray[SessionSlot].IsSending = FALSE;
			SessionArray[SessionSlot].SessionIP = ClientAddr.sin_addr;
			SessionArray[SessionSlot].SessionPort = ClientAddr.sin_port;
			SessionArray[SessionSlot].IsSendAndDisconnect = false;
			SessionArray[SessionSlot].IsCanceled = false;
			SessionArray[SessionSlot].DisconnectPacket = nullptr;			
			SessionArray[SessionSlot].IsReleased = FALSE;					

			// IOCP �� ���� ���� 
			HANDLE hRet = CreateIoCompletionPort((HANDLE)client, hIOCP, (ULONG_PTR)&(SessionArray[SessionSlot]), 0);

			if (hRet != hIOCP)
			{
				pThis->OnError(GetLastError(), _T("CreateIoCompletionPort Failed"));
				closesocket(client);
				client = INVALID_SOCKET;
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

	unsigned int __stdcall GGM::CNetServer::WorkerThread(LPVOID Param)
	{
		// Worker ������ �Լ��� static�̱� ������ ��������� �����ϰų� ����Լ��� ȣ���ϱ� ���� 
		// this �����͸� ���ڷ� �޾ƿ´�.
		CNetServer *pThis = (CNetServer*)Param;

		// IOCP �ڵ�
		HANDLE hIOCP = pThis->m_hWorkerIOCP;

		// ���� �������� ����ü ������
		OVERLAPPED *pDummyOverlapped = &pThis->m_DummyOverlapped;

		LONG64 *pSendTPS = &(pThis->m_SendTPS);				
	
		// ���� ���鼭 �۾� �Ѵ�.
		while (true)
		{
			// �Ϸ����� �� ����� ���� �������� �ʱ�ȭ �Ѵ�.
			DWORD BytesTransferred; // �ۼ��� ����Ʈ ��
			NetSession *pSession; // ���ø��� Ű�� ������ ���� ����ü ������
			OVERLAPPED *pOverlapped; // Overlapped ����ü �������� ������		

			// GQCS�� ȣ���ؼ� �ϰ��� ���⸦ ����Ѵ�.			
			bool bOk = GetQueuedCompletionStatus(hIOCP, &BytesTransferred, (PULONG_PTR)&pSession, (LPOVERLAPPED*)&pOverlapped, INFINITE);

			// ��Ŀ ������ �� ��������.
			//pThis->OnWorkerThreadBegin();		

			if (bOk == false)
			{
				if (GetLastError() == 121)
				{
					pThis->OnSemError(pSession->SessionID);
				}
			}

			//SendPacket�Լ� ���ο��� PQCS�� SendPost��û �ߴٸ� �� ������ ó�� 
			if (pOverlapped == pDummyOverlapped)
			{
				if (pSession->SendQ.size() > 0)
				{
					pThis->SendPost(pSession);
				}	

				// ���� ����ȭ�� ���� IOCount�÷ȴ� ���� �� ������ ����				
				pThis->SessionReleaseLock(pSession);
				continue;
			}

			// Overlapped ����ü�� nullptr�̸� ������ �߻��� ���̴�.
			if (pOverlapped == nullptr)
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

				pThis->OnError(WSAGetLastError(), _T("GQCS FAILED / OVERLAPPED NULL"));
				continue;
			}

			// pOverlapped�� nullptr�� �ƴ϶�� �ۼ��Ź���Ʈ ���� 0���� �˻��ؾ� �Ѵ�. 
			// IOCP���� �ۼ��� ����Ʈ���� (0 �̳� ��û�� ����Ʈ ��) �̿��� �ٸ� ���� ���� �� ����. 			
			// ���� �ۼ��� ����Ʈ ���� 0�� �ƴ϶�� ��û�� �񵿱� I/O�� ���������� ó���Ǿ����� �ǹ��Ѵ�.			
			if (BytesTransferred == 0 || (pSession->IsCanceled == true))
			{				
				pThis->SessionReleaseLock(pSession);
				continue;
			}

			// �Ϸ�� I/O�� RECV���� SEND���� �Ǵ��Ѵ�.
			// �������� ����ü ������ ��
			if (pOverlapped == &(pSession->RecvOverlapped))
			{
				//////////////////////////////////////////////////////
				// �Ϸ�� I/O�� RECV �� ���
				//////////////////////////////////////////////////////

				// �Ϸ������� ���� ����Ʈ ����ŭ Rear�� �Ű��ش�.				
				pSession->RecvQ.RelocateWrite(BytesTransferred);				

				// ��Ʈ��ũ Ŭ���� ������ �ϼ��� ��Ŷ�� �ִ��� Ȯ��
				// CheckPacket ���ο��� ��� �Ϸ�� ��Ŷ�� OnRecv�� ����
				if (pThis->CheckPacket(pSession) == false)
				{
					// ������ ��Ŷ�� Ȯ���ϴ� ���߿� �̻��� ��Ŷ ������ Ȯ���ߴٸ� �ٷ� ������� false ����
					// I/O Count �����ϰ� Recv ���� �ʴ´�.
					pThis->SessionReleaseLock(pSession);
					continue;
				}

				// RecvPost ȣ���ؼ� WSARecv ����
				pThis->RecvPost(pSession);
			}
			else
			{
				//////////////////////////////////////////////////////
				// �Ϸ�� I/O�� SEND �� ���
				//////////////////////////////////////////////////////							

				ULONG PacketCount = pSession->SentPacketCount;
				InterlockedAdd64(pSendTPS, PacketCount);
				CNetPacket **PacketArray = pSession->PacketArray;

				// ������ ���� Ȯ�� 
				if (pSession->IsSendAndDisconnect == true)
				{
					CNetPacket* pDisconnectPacket = pSession->DisconnectPacket;					

					// ������ ���� �÷��װ� true��� ���� ������ �Ϸ������� �ش� ��Ŷ�� ���ԵǾ����� Ȯ��
					for (ULONG i = 0; i < PacketCount; i++)
					{
						if (PacketArray[i] == pDisconnectPacket)
						{
							// ���� ������ �Ϸ������� ������ ���⿡ �ش��ϴ� ��Ŷ�� �����Ѵٸ� �ش缼�ǰ��� ��������
							pThis->Disconnect(pSession->SessionID);
							break;
						}
					}					
				}						

				// �񵿱� I/O�� ��û�� ������ �� ����ȭ ���۸� �������ش�.				
				for (ULONG i = 0; i < PacketCount; i++)
					CNetPacket::Free(PacketArray[i]);

				pSession->SentPacketCount = 0;

				// 2. ��û�� �񵿱� Send�� �Ϸ�Ǿ����Ƿ� ���� �ش� ���ǿ� ���� Send�� �� �ְ� �Ǿ���
				pSession->IsSending = FALSE;

				// 3. ���� ������ ���� ���� �ִ��� Ȯ���ؼ� �ִٸ� ����
				if (pSession->SendQ.size() > 0)
				{
					pThis->SendPost(pSession);					
				}
					
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

	bool GGM::CNetServer::CheckPacket(NetSession * pSession)
	{
		CRingBuffer *pRecvQ = &(pSession->RecvQ);

		// ���� RECV ������ ��뷮 üũ
		int CurrentUsage = pRecvQ->GetCurrentUsage();
		int Result;

		// �����ۿ� �ּ��� ������� �̻��� �ִ��� ����Ȯ���Ѵ�.
		// ������� �̻��� ������ �������鼭 �����ϴ� �ϼ��� ��Ŷ �� �̾Ƽ� OnRecv�� ����
		LONG64 PacketCount = 0;
		while (CurrentUsage >= NET_HEADER_LENGTH)
		{			
			NET_HEADER Header;

			// �ش� ���� �����ۿ��� �����ŭ PEEK
			Result = pRecvQ->Peek((char*)&Header, NET_HEADER_LENGTH);

			if (Result != NET_HEADER_LENGTH)
			{				
				OnError(GGM_ERROR::BUFFER_READ_FAILED, _T("[CNetServer] NetHeader Peek Failed!!"));			
				return false;				
			}

			CurrentUsage -= NET_HEADER_LENGTH;

			// ��Ŷ �ڵ尡 �ùٸ��� �ʴٸ� �ش� ���� ��������
			if (Header.PacketCode != CNetPacket::PacketCode)
			{				
				TCHAR SessionIP[IP_LEN];
				InetNtop(AF_INET, &(pSession->SessionIP), SessionIP, IP_LEN);
				CLogger::GetInstance()->Log(_T("CNetServer Log"), LEVEL::ERR, OUTMODE::FILE, _T("WRONG PACKET CODE[%s:%hd]"), SessionIP, htons(pSession->SessionPort));
				Disconnect(pSession->SessionID);
				return false;
			}

			// ������ ���̷ε� ����� ����ȭ ������ ũ�⸦ �ʰ��Ѵٸ� ������´�.
			if (Header.Length > DEFAULT_BUFFER_SIZE - NET_HEADER_LENGTH)
			{
				TCHAR SessionIP[IP_LEN];
				InetNtop(AF_INET, &(pSession->SessionIP), SessionIP, IP_LEN);
				CLogger::GetInstance()->Log(_T("CNetServer Log"), LEVEL::ERR, OUTMODE::FILE, _T("TOO BIG PACKET RECV[%s:%hd]"), SessionIP, htons(pSession->SessionPort));
				Disconnect(pSession->SessionID);
				return false;
			}

			// ���� �����ۿ� �ϼ��� ��Ŷ��ŭ ������ �ݺ��� Ż�� 
			if (CurrentUsage < Header.Length)				
				break;

			// �ϼ��� ��Ŷ�� �ִٸ� HEADER�� RecvQ���� �����ش�.
			pRecvQ->EraseData(NET_HEADER_LENGTH);

			CNetPacket *pPacket = CNetPacket::Alloc();						

			// �ϼ��� ��Ŷ�� �̾Ƽ� ����ȭ ���ۿ� ��´�.
			Result = pRecvQ->Dequeue(pPacket->m_pSerialBuffer, Header.Length);

			if (Result != Header.Length)
			{
				OnError(GGM_ERROR::BUFFER_READ_FAILED, _T("[CNetServer] Payload Dequeue Failed!!"));
				break;
			}		

			// ��Ŷ�� ī���Ѹ�ŭ ���� ������ �ݿ�
			pPacket->RelocateWrite(Header.Length - NET_HEADER_LENGTH);

			// ���� ���޵� ��Ŷ�� ��ȣȭ �ϰ� ��ȿ���� �˻��Ѵ�.
			if (pPacket->Decode(&Header) == false)
			{
				// ��ȣȭ�� ��Ŷ�� �̻��ϴٸ� ������ ���´�.	

				/*TCHAR ErrMsg[512];

				StringCbPrintf(
					ErrMsg,
					512,
					_T("PACKET DECODE ERROR [%s:%hd]"),
					InetNtop(AF_INET, &(pSession->SessionIP), ErrMsg, sizeof(ErrMsg)),
					htons(pSession->SessionPort)
				);

				OnError(GGM_ERROR_PACKET_DECODE_ERROR, ErrMsg);*/

				Disconnect(pSession->SessionID);
				CNetPacket::Free(pPacket);
				return false;
			}

			// �ϼ��� ��Ŷ�� �����ϹǷ� OnRecv�� �ϼ� ��Ŷ�� �����Ѵ�.
			OnRecv(pSession->SessionID, pPacket);

			// ���� �������� ��� ����� ��Ŷ �����ŭ ����
			CurrentUsage -= Header.Length;

			// Alloc�� ���� ����
			CNetPacket::Free(pPacket);

			PacketCount++;
		}

		InterlockedAdd64(&m_RecvTPS, PacketCount);

		return true;
	}

	bool GGM::CNetServer::SendPost(NetSession * pSession)
	{
		///////////////////////////////////////////////////////////////////
		// 0. Send ���� �÷��� üũ 
		///////////////////////////////////////////////////////////////////		
		while (InterlockedExchange(&(pSession->IsSending), TRUE) == FALSE)
		{
			CLockFreeQueue<CNetPacket*> *pSendQ = &(pSession->SendQ);

			// ���� SendQ ��뷮�� Ȯ���ؼ� ���� ���� �ִ��� �ٽ� �ѹ� Ȯ���غ���.
			// ���� ���� �ִ��� �˰� ���Դµ� ���� ���� ���� ���� �ִ�.
			ULONG CurrentUsage = pSendQ->size();

			// �������� ���ٸ� Send �÷��� �ٽ� �ٲپ� �ְ� ����
			if (CurrentUsage == 0)
			{
				// ���� �� �κп��� �÷��׸� �ٲٱ����� ���ؽ�Ʈ ����Ī�� �Ͼ�ٸ� ������ �ȴ�.
				// �ٸ� �����尡 ���� ���� �־��µ� �÷��װ� ����־ �� ������ ���� �ִ�. 

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
			// ȥ�������� [SendQ : ����ȭ ������ ������ (CNetPacket*)] [ WSABUF : ����ȭ���۳��� ���������� (char*)]
			// WSABUF�� �ѹ��� ��� ���� ��Ŷ�� ������ �������� 100 ~ 500�� ���̷� ���Ѵ�.
			// WSABUF�� ������ �ʹ� ���� ������ �ý����� �ش� �޸𸮸� �� �ɱ� ������ �޸𸮰� �̽��� �߻��� �� �ִ�. 
			// ����ȭ������ �����Ϳ� �۽��� ������ �����ߴٰ� �Ϸ������� �߸� �޸� ���� 

			// ����ȭ ���۳��� ��Ŷ �޸� ������(char*)�� ���� WSABUF ����ü �迭
			WSABUF        wsabuf[MAX_PACKET_COUNT];

			// SendQ���� �̾Ƴ� ����ȭ ������ ������(CPacket*)�� ���� �迭
			CNetPacket **PacketArray = pSession->PacketArray;

			// SendQ���� �ѹ��� �۽� ������ ��Ŷ�� �ִ� ������ŭ ����ȭ ������ �����͸� Dequeue�Ѵ�.		
			// ���Ǻ��� ������ �ִ� ����ȭ ���� ������ �迭�� �װ��� �����Ѵ�.
			// Peek�� �� �� �Ϸ����� ���Ŀ� Dequeue�� �ϸ� memcpy�� �߰������� �Ͼ�Ƿ� �޸𸮸� ����ؼ� Ƚ���� �ٿ���.			

			// ���� ť�� ����ִ� �������� ������ �ִ�ġ�� �ʰ��ߴٸ� �������ش�.
			if (CurrentUsage > MAX_PACKET_COUNT)
				CurrentUsage = MAX_PACKET_COUNT;

			for(ULONG i=0;i<CurrentUsage;i++)
				pSession->SendQ.Dequeue(&PacketArray[i]);

			// ���� ��Ŷ�� ������ ����ߴٰ� ���߿� �Ϸ������� ���� �޸𸮸� �����ؾ� �Ѵ�.			
			DWORD PacketCount = pSession->SentPacketCount = CurrentUsage;		

			// ��Ŷ ������ŭ �ݺ��� ���鼭 ���� ����ȭ ���� ���� ��Ŷ ���� �����͸� WSABUF����ü�� ����
			for (DWORD i = 0; i < PacketCount; i++)
			{
				wsabuf[i].buf = (char*)PacketArray[i]->m_pSerialBuffer;
				wsabuf[i].len = PacketArray[i]->m_Rear;
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

	bool GGM::CNetServer::RecvPost(NetSession * pSession)
	{
		///////////////////////////////////////////////////////////////////
		// 0. I/O ��û�� �ϱ� ���� ���� �������� ���������� �˻��Ѵ�.
		///////////////////////////////////////////////////////////////////

		CRingBuffer *pRecvQ = &(pSession->RecvQ);
		int CurrentSpare = pRecvQ->GetCurrentSpare();

		// ���� RecvQ�� ���������� 0�̶�� ���������� ��Ȳ�̴�.
		// �������� ��Ȳ�̶�� ������ �����۰� �������� ��Ȳ�� �߻����� ���� ���̴�.
		// �׷��� �������� �������� �ǵ��� ������ �����۰� ���밡���� �� �̻����� ����� ���̷ε� ����� �����ϰ� �Ŵ��� ���̷ε带 ���������ٸ� �� �� �� �ִ�.
		// �̷� ��� �� ������ ����� �Ѵ�.
		if (CurrentSpare == 0)
		{
			// ���� ����		

			/*TCHAR ErrMsg[512];

				StringCbPrintf(
					ErrMsg,
					512,
					_T("RECV BUFFER FULL [%s:%hd]"),
					InetNtop(AF_INET, &(pSession->SessionIP), ErrMsg, sizeof(ErrMsg)),
					htons(pSession->SessionPort)
				);

				OnError(GGM_ERROR_RECV_BUFFER_FULL, ErrMsg);*/			

			// �ش� ���� ��������
			OnError(GGM_ERROR::BUFFER_FULL, _T("[CNetServer] RecvPost() RecvQ CurrentSpare == 0 ErrorCode[%d]"));
			Disconnect(pSession->SessionID);
			return true;
		}

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
		// 3. WSARecv ������� I/O ī��Ʈ ����
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

	void GGM::CNetServer::ReleaseSession(NetSession * pSession)
	{
		// ReleaseFlag �� TRUE�� �Ǹ� � �����嵵 �� ���ǿ� �����ϰų� ������ �õ��� �ؼ��� �ȵȴ�.
		// IoCount�� ReleaseFlag�� 4����Ʈ�� ���޾Ƽ� ��ġ�� �����Ƿ� �Ʒ��� ���� ���Ͷ��Լ� ȣ���Ѵ�.
		// IoCount(LONG) == 0 ReleaseFlag(LONG) == 0 >> ���Ͷ� ������ >>IoCount(LONG) == 0 ReleaseFlag(LONG) == 1
		if (InterlockedCompareExchange64((volatile LONG64*)&(pSession->IoCount), 0x0000000100000000, FALSE) != FALSE)			
			return;								
		
		ULONGLONG SessionID = pSession->SessionID;			
		pSession->SessionID = INIT_SESSION_ID;		

		// ���� ����ȭ ���۸� Alloc �� Send�ϴ� ���߿� ���������� Release�� �ؾ��Ѵٸ�
		// Free�� �־�� �Ѵ�.
		ULONG PacketCount = pSession->SentPacketCount;
		CNetPacket **PacketArray = pSession->PacketArray;		
		for (WORD i = 0; i < PacketCount; i++)
			CNetPacket::Free(PacketArray[i]);		

		// SendQ ���� ���� ��� ����		
		ULONG GarbagePacketCount = pSession->SendQ.size();		
		for (ULONG i = 0; i < GarbagePacketCount; i++)				
		{
			CNetPacket *pGarbagePacket;
			pSession->SendQ.Dequeue(&pGarbagePacket);
			CNetPacket::Free(pGarbagePacket);
		}
	
		// ���� ����		
		closesocket(pSession->socket);
		pSession->socket = INVALID_SOCKET;		
	
		// �ش� ���ǿ� ���� ��� ���ҽ��� �����Ǿ����Ƿ� �̺�Ʈ �ڵ鸵 �Լ� ȣ��
		OnClientLeave(SessionID);	

		// �ش� ������ �ε����� ���ÿ� �ִ´�. ( ��� ���� �ε����� ����� ���� )		
		m_NextSlot->Push(pSession->SessionSlot);

		// ���� ���� ���� Ŭ���̾�Ʈ �� ����
		InterlockedDecrement(&m_SessionCount);			
	}

	bool GGM::CNetServer::SessionAcquireLock(NetSession * pSession, ULONGLONG LocalSessionID)
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

	void GGM::CNetServer::SessionReleaseLock(NetSession * pSession)
	{
		// ���ǿ� ���� ������ ��� �������Ƿ� ������ �ø� I/O ī��Ʈ�� ���� ��Ų��.			
		LONG IoCount = InterlockedDecrement(&(pSession->IoCount));
		if (IoCount <= 0)
		{
			if (IoCount < 0)	
			{
				OnError(GGM_ERROR::NEGATIVE_IO_COUNT, _T("[CNetServer] SessionReleaseLock() IoCount Negative ErrorCode[%d]"));
				CCrashDump::ForceCrash();
			}			

			ReleaseSession(pSession);
		}
	}
	bool CNetServer::WSAInit(TCHAR * BindIP, USHORT port, bool IsNoDelay)
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
			int iResult = setsockopt(m_Listen, IPPROTO_TCP, TCP_NODELAY, (const char*)&IsNoDelay, sizeof(IsNoDelay));

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
		
		tcp_keepalive KeepAliveOpt;
		KeepAliveOpt.onoff = 1;
		KeepAliveOpt.keepalivetime = 30000;
		KeepAliveOpt.keepaliveinterval = 1000;
		DWORD BytesReturned = 0;
		Result = WSAIoctl(m_Listen, SIO_KEEPALIVE_VALS, (LPVOID)&KeepAliveOpt, (DWORD)sizeof(KeepAliveOpt), nullptr, 0, &BytesReturned, nullptr, nullptr);

		if (Result == SOCKET_ERROR)
		{
			OnError(WSAGetLastError(), _T("WSAIoctl Failed"));
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

	bool CNetServer::SessionInit(WORD MaxSessions)
	{
		m_MaxSessions = MaxSessions;

		// �ִ� ���� ���� ��ŭ �迭 �����Ҵ�
		m_SessionArray = new NetSession[MaxSessions];

		if (m_SessionArray == nullptr)
		{
			OnError(GetLastError(), _T("MEM Alloc Failed"));
			return false;
		}

		for (int i = 0; i < MaxSessions; i++)
		{			
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
		for (WORD i = 0; i < (MaxSessions); i++)
		{
			m_NextSlot->Push(i);
		}
		
		return true;
	}

	bool CNetServer::ThreadInit(DWORD ConcurrentThreads, DWORD MaxThreads)
	{
		// ��Ŀ������� ������ IOCP ����
		m_hWorkerIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, ConcurrentThreads);

		if (m_hWorkerIOCP == NULL)
		{
			OnError(GetLastError(), _T("CreateIoCompletionPort Failed"));
			return false;
		}

		// ������ �������� �ڵ��� ������ �迭 
		m_ThreadHandleArr = new HANDLE[MaxThreads + 1];

		if (m_ThreadHandleArr == nullptr)
		{
			OnError(GetLastError(), _T("MEM Alloc Failed"));
			return false;
		}

		// ������ �������� ���̵� ������ �迭
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
				CNetServer::WorkerThread, // ��Ŀ ������ �Լ� Ŭ���� ���ο� private static���� ����Ǿ�����
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
			CNetServer::AcceptThread, // Accept ������ �Լ� Ŭ���� ���ο� private static���� ����Ǿ�����
			this, // Accept ������ �Լ��� static���� ����Ǿ������Ƿ� �ٸ� ��� ������ �Լ��� �����ϱ� ���ؼ� �ش� �ν��Ͻ��� this �����Ͱ� �ʿ���
			0,
			(unsigned int*)&m_ThreadIDArr[MaxThreads]
		);

		if (m_ThreadHandleArr[MaxThreads] == NULL)
		{
			OnError(GetLastError(), _T("_beginthreadex Failed"));
			return false;
		}	

		// ������ ��Ŀ ������ ���� �����Ѵ�
		m_IOCPThreadsCount = MaxThreads;

		return true;
	}


	bool GGM::CNetServer::Start(
		TCHAR * BindIP,
		USHORT port,
		DWORD ConcurrentThreads,
		DWORD MaxThreads,
		bool IsNoDelay,
		WORD MaxSessions,
		BYTE PacketCode,
		BYTE PacketKey		
	)
	{		
		
		bool IsSuccess = WSAInit(BindIP, port, IsNoDelay);	

		if (IsSuccess == false)
			return false;

		IsSuccess = SessionInit(MaxSessions);

		if (IsSuccess == false)
			return false;

		IsSuccess = CNetPacket::CreatePacketPool(0);

		if (IsSuccess == false)
			return false;

		CNetPacket::SetPacketCode(PacketCode, PacketKey);	

		IsSuccess = ThreadInit(ConcurrentThreads, MaxThreads);

		if (IsSuccess == false)
			return false;
		
		return true;
	}

	void GGM::CNetServer::Stop()
	{
		////////////////////////////////////////////////////////////////////
		// ���� ���� ���� 
		// 1. �� �̻� accept�� ���� �� ������ ���������� �ݴ´�.
		// 2. ��� ���ǿ� FIN�� ������.		
		// 3. ��� ������ I/O COUNT�� 0�� �ǰ� Release�� ������ ����Ѵ�.
		// 4. ��Ŀ������ �����Ų��.
		// 5. �������� ����ߴ� ��Ÿ ���ҽ����� �����Ѵ�.
		////////////////////////////////////////////////////////////////////				
		closesocket(m_Listen);
		m_Listen = INVALID_SOCKET;		
		WaitForSingleObject(m_ThreadHandleArr[m_IOCPThreadsCount], INFINITE);
		CloseHandle(m_ThreadHandleArr[m_IOCPThreadsCount]);
		
		NetSession *pSession = m_SessionArray;
		for (DWORD i = 0; i < m_MaxSessions; i++)
		{			
			if (pSession[i].socket != INVALID_SOCKET)
			{
				Disconnect(pSession[i].SessionID);						
			}
		}

		// ������ ���� ������ ������ ����Ѵ�.
		while (m_SessionCount > 0)
		{
			Sleep(100);
		}
		
		CloseHandle(m_hWorkerIOCP);	
		WaitForMultipleObjects(m_IOCPThreadsCount, m_ThreadHandleArr, TRUE, INFINITE);
		for (DWORD i = 0; i < m_IOCPThreadsCount; i++)
			CloseHandle(m_ThreadHandleArr[i]);
		
		delete[] m_ThreadHandleArr;
		delete[] m_ThreadIDArr;
		delete[] m_SessionArray;
		delete   m_NextSlot;
		CNetPacket::DeletePacketPool();		
	}

	ULONGLONG GGM::CNetServer::GetSessionCount() const
	{
		return m_SessionCount;
	}

	bool GGM::CNetServer::Disconnect(ULONGLONG SessionID)
	{
		// �� ���Ǻ��� ����� ���� ���̵��� ������ 2����Ʈ���� �ش� ������ ����� �迭�� �ε����� ���õǾ��ִ�.	
		WORD SessionSlot = ((WORD)SessionID);
		NetSession *pSession = &m_SessionArray[SessionSlot];			

		// �ܼ� ������ ���� �Ͱ� ������ ������ �ϴ� ���� �ٸ� ���̴�.
		// ��û�� I/O�� ����Ͽ� �ڿ������� I/O Count�� 0�� �ǵ��� �����Ͽ� ������ �������Ѵ�.
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

	bool GGM::CNetServer::SendPacket(ULONGLONG SessionID, CNetPacket * pPacket)
	{
		// �� ���Ǻ��� ����� ���� ���̵��� ������ 2����Ʈ���� �ش� ������ ����� �迭�� �ε����� ���õǾ��ִ�.	
		WORD SessionSlot = (WORD)SessionID;
		NetSession *pSession = &m_SessionArray[SessionSlot];
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

			// ��Ŷ�� ���ڵ��Ѵ�.
			pPacket->Encode();		

			// SendQ�� ��Ŷ�� �����͸� ��´�.	
			InterlockedIncrement(&(pPacket->m_RefCnt));	
			Result = pSession->SendQ.Enqueue(pPacket);
			if (Result == false)
			{
				// SendQ�� Enqueue ���д� �־�� �ȵǴ� ���̴�. 
				// �ٷ� ���� ����.
				CCrashDump::ForceCrash();
			}		

			// SendFlag�� Ȯ���غ��� Send���� �ƴ϶�� PQCS�� WSASend ��û
			// �̷��� �����ν�, ���������� WSASend�� ȣ���Ͽ� ������Ʈ�� �������� ���� ���� �� �ִ�.
			// �׷��� ��Ŷ ���伺�� ������
			if (InterlockedOr(&pSession->IsSending, 0) == TRUE)
				break;			
	
			PostQueuedCompletionStatus(m_hWorkerIOCP, 0, (ULONG_PTR)pSession, &m_DummyOverlapped);	

			// PQCS�� �ϰ����� IOCount�� �ٷ� ���� �ʰ� �Ϸ��������� ��´�.					
			return true;		

		} while (0);	

		// ���ǿ� ������ �Ϸ������� �˸�		
		SessionReleaseLock(pSession);

		return Result;
	}

	bool CNetServer::SendPacketAndDisconnect(ULONGLONG SessionID, CNetPacket * pPacket)
	{
		// ��Ŷ�� �۽��ϰ� ���� ������ ���´�.
		// �� ���Ǻ��� ����� ���� ���̵��� ������ 2����Ʈ���� �ش� ������ ����� �迭�� �ε����� ���õǾ��ִ�.	
		WORD SessionSlot = (WORD)SessionID;
		NetSession *pSession = &m_SessionArray[SessionSlot];
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

			// ��Ŷ�� ���ڵ��Ѵ�.
			pPacket->Encode();

			// SendQ�� ��Ŷ�� �����͸� ��´�.	
			InterlockedIncrement(&(pPacket->m_RefCnt));
			Result = pSession->SendQ.Enqueue(pPacket);
			if (Result == false)
			{
				// SendQ�� Enqueue ���д� �־�� �ȵǴ� ���̴�. 
				// �ٷ� ���� ����.
				CCrashDump::ForceCrash();
			}
			
			// �ش� ��Ŷ�� �Ϸ������� �����ϸ� ������ ���� �� �ֵ��� �÷��׸� �Ҵ�.
			// ���߿� �Ϸ��������� Ȯ���� �ʿ䰡 �����Ƿ� �ش� ��Ŷ�� �����͸� �����Ѵ�.
			pSession->DisconnectPacket = pPacket;
			pSession->IsSendAndDisconnect = true;	

			// SendFlag�� Ȯ���غ��� Send���� �ƴ϶�� PQCS�� WSASend ��û
			// �̷��� �����ν�, ���������� WSASend�� ȣ���Ͽ� ������Ʈ�� �������� ���� ���� �� �ִ�.
			// �׷��� ��Ŷ ���伺�� ������
			if (InterlockedOr(&pSession->IsSending, 0) == TRUE)
				break;

			PostQueuedCompletionStatus(m_hWorkerIOCP, 0, (ULONG_PTR)pSession, &m_DummyOverlapped);		

			// PQCS�� �ϰ����� IOCount�� �ٷ� ���� �ʰ� �Ϸ��������� ��´�.			
			return true;		

		} while (0);

		// ���ǿ� ������ �Ϸ������� �˸�		
		SessionReleaseLock(pSession);

		return Result;
	}
}
