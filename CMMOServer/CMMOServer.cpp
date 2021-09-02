#include "CMMOServer.h"
#include "CrashDump\CrashDump.h"
#include <tchar.h>
#include "Define\GGM_CONSTANTS.h"
#include "Define\GGM_ERROR.h"
#include <strsafe.h>

namespace GGM
{
	bool GGM::CMMOServer::Start(
		TCHAR		 *BindIP, 
		USHORT		  port, 
		DWORD		  ConcurrentWorkerThreads, 
		DWORD		  MaxWorkerThreads, 
		bool		  IsNoDelay, 
		WORD		  MaxSessions, 
		BYTE		  PacketCode, 
		BYTE	      PacketKey,		
		CMMOSession  *pPlayerArr, 
		int			  PlayerSize,
		WORD          SendSleepTime,
		WORD          AuthSleepTime,
		WORD          GameSleepTime,
		WORD          ReleaseSleepTime,
		WORD          AuthPacketPerLoop,
		WORD          GamePacketPerLoop,
		WORD          AcceptPerLoop,
		WORD          AuthToGamePerLoop,		
		ULONGLONG     TimeoutLimit,
		bool          IsTimeoutOn
	)	 
	{
		bool IsSuccess = WSAInit(BindIP, port, IsNoDelay);

		if (IsSuccess == false)
			return false;		
		
		IsSuccess = CNetPacket::CreatePacketPool(0);

		if (IsSuccess == false)
			return false;
		
		CNetPacket::SetPacketCode(PacketCode, PacketKey);	

		IsSuccess = SessionInit(MaxSessions, pPlayerArr, PlayerSize);

		if (IsSuccess == false)
			return false;
		
		// ���� ���� ���� �ʱ�ȭ			
		m_TimeoutLimit = TimeoutLimit;
		m_IsTimeout    = IsTimeoutOn;		
		m_SendSleepTime = SendSleepTime;
		m_AuthSleepTime = AuthSleepTime;
		m_GameSleepTime = GameSleepTime;
		m_ReleaseSleepTime = ReleaseSleepTime;		
		m_WorkerThreadsCount = MaxWorkerThreads;		
		m_AuthPacketPerLoop = AuthPacketPerLoop;
		m_GamePacketPerLoop = GamePacketPerLoop;		
		m_AcceptPerLoop = AcceptPerLoop;		
		m_AuthToGamePerLoop = AuthToGamePerLoop;

		IsSuccess = ThreadInit(ConcurrentWorkerThreads, MaxWorkerThreads);

		if (IsSuccess == false)
			return false;		
		
		return true;
	}

	void GGM::CMMOServer::Stop()
	{
		////////////////////////////////////////////////////////////////////
		// ���� ���� ���� 
		// 1. �� �̻� accept�� ���� �� ������ ���������� �ݴ´�. accept ������ ����
		// 2. ��� ���ǰ��� ������ ���´�.		
		// 3. ��� ������ I/O COUNT�� 0�� �ǰ� Release�� ������ ����Ѵ�.
		// 4. ������ ��� �����ϰ� �����Ǹ�, auth, game �����带 ���� ��Ų��.
		// 5. ��Ŀ������ �����Ų��.		
		// 6. �������� ����ߴ� ��Ÿ ���ҽ����� �����Ѵ�.
		////////////////////////////////////////////////////////////////////

		// Aceept ������ ����
		closesocket(m_Listen);
		m_Listen = INVALID_SOCKET;

		// Accept Thread ���� ��� 
		WaitForSingleObject(m_AcceptThread, INFINITE);
		CloseHandle(m_AcceptThread);

		// ��� ���ǿ� shutdown�� ������.
		CMMOSession **SessionArr = m_SessionArr;
		for (DWORD i = 0; i < m_MaxSessions; i++)
		{
			// ���� �迭�� ��ȸ�ϸ鼭 ��� ���ǿ� shutdown�� ������.
			if (SessionArr[i]->m_socket != INVALID_SOCKET)
			{
				SessionArr[i]->Disconnect();
			}
		}

		// ������ ���� ������ ������ ����Ѵ�.
		while (m_SessionCount > 0)
		{
			Sleep(100);
		}

		// auth, game, send �����尡 ������ �� �ֵ��� �÷��� �ٲ�
		// auth, game, send ������� �� �����Ӹ��� �� �÷��׸� üũ�ϸ� ����Ȯ���Ѵ�.
		m_IsExit = true;

		// Auth Thread ���� ��� 
		WaitForSingleObject(m_AuthThread, INFINITE);
		CloseHandle(m_AuthThread);

		// Game Thread ���� ��� 
		WaitForSingleObject(m_GameThread, INFINITE);
		CloseHandle(m_GameThread);

		// Send Thread ���� ��� 
		for (int i = 0; i < SEND_THREAD_COUNT; i++)
		{
			WaitForSingleObject(m_SendThread[i], INFINITE);
			CloseHandle(m_SendThread[i]);
		}		

		// Release Thread ���� ��� 
		WaitForSingleObject(m_ReleaseThread, INFINITE);
		CloseHandle(m_ReleaseThread);

		// ��Ŀ ������ IOCP �ڵ��� �ݴ´�. �� ������ ��Ŀ�����尡 ��� ����ȴ�.
		CloseHandle(m_hWorkerIOCP);

		// ��Ŀ�����尡 ��� ����� ������ ����Ѵ�.
		WaitForMultipleObjects(m_WorkerThreadsCount, m_WorkerThreadHandleArr, TRUE, INFINITE);
		for (DWORD i = 0; i < m_WorkerThreadsCount; i++)
			CloseHandle(m_WorkerThreadHandleArr[i]);

		// ��Ÿ ���ҽ� ����
		delete[] m_WorkerThreadHandleArr;
		delete[] m_WorkerThreadIDArr;
		delete[] m_SessionArr;	

		// ������ ť ����
		delete m_AcceptQueue;
		
		// ���� �ε��� ť ���� 
		delete m_NextSession;

		// ��ŶǮ ����
		CNetPacket::DeletePacketPool();

		// ���������� ������ ���� ���� ����ÿ� ���� ������ ����� �Ǿ����� Ȯ���ϱ� ���� �뵵
		_tprintf_s(_T("[SERVER OFF LAST INFO] SESSION ALIVE COUNT : %lld\n\n"), m_SessionCount);
	}

	ULONGLONG CMMOServer::GetSessionCount() const
	{
		return m_SessionCount;
	}

	unsigned int __stdcall CMMOServer::WorkerThread(LPVOID Param)
	{
		// Worker ������ �Լ��� static�̱� ������ ��������� �����ϰų� ����Լ��� ȣ���ϱ� ���� 
		// this �����͸� ���ڷ� �޾ƿ´�.
		CMMOServer *pThis = (CMMOServer*)Param;

		// IOCP �ڵ�
		HANDLE hIOCP = pThis->m_hWorkerIOCP;		

		// ���� ���鼭 �۾� �Ѵ�.
		while (true)
		{
			// �Ϸ����� �� ����� ���� ������
			DWORD BytesTransferred; // �ۼ��� ����Ʈ ��
			CMMOSession *pSession; // ���ø��� Ű�� ������ CMMOSession ������
			OVERLAPPED *pOverlapped; // Overlapped ����ü �������� ������		

			// GQCS�� ȣ���ؼ� �ϰ��� ���⸦ ����Ѵ�.			
			bool bOk = GetQueuedCompletionStatus(hIOCP, &BytesTransferred, (PULONG_PTR)&pSession, (LPOVERLAPPED*)&pOverlapped, INFINITE);

			// ��Ŀ ������ �� ��������.
			//pThis->OnWorkerThreadBegin();		

			if (bOk == false)
			{
				if (GetLastError() == 121)
				{
					pThis->OnSemError(pSession->m_SessionSlot);
				}
			}

			// Overlapped ����ü�� nullptr�̸� ������ �߻��� ���̴�.
			if (pOverlapped == nullptr)
			{
				DWORD Error = WSAGetLastError();
				if (bOk == false && Error == ERROR_ABANDONED_WAIT_0)
				{
					// �������� ����ü�� ���ε� �����ڵ尡 ERROR_ABANDONED_WAIT_0��� �ܺο��� ���� ������� IOCP �ڵ��� ���� ���̴�.
					// ������ ����ó�� �����̹Ƿ� ���� ���ָ� �ȴ�.		
					return 0;
				}

				pThis->OnError(WSAGetLastError(), _T("GQCS FAILED / OVERLAPPED NULL"));
				continue;
			}

			// pOverlapped�� nullptr�� �ƴ϶�� �ۼ��Ź���Ʈ ���� 0���� �˻��ؾ� �Ѵ�. 
			// IOCP���� �ۼ��� ����Ʈ���� 0 �̳� ��û�� ����Ʈ �� �̿��� �ٸ� ���� ���� �� ����. 			
			// ���� �ۼ��� ����Ʈ ���� 0�� �ƴ϶�� ��û�� �񵿱� I/O�� ���������� ó���Ǿ����� �ǹ��Ѵ�.		
			// �߰��� �ܺο��� I/O�� ������ ĵ���ߴ��� ���ε� Ȯ�� 
			if (BytesTransferred == 0 || pSession->m_IsCanceled == true)
			{				
				if (pOverlapped == &(pSession->m_SendOverlapped))
					pSession->m_IsSending = false;				

				pSession->DecreaseIoCount();
				continue;
			}

			// �Ϸ�� I/O�� RECV���� SEND���� �Ǵ��Ѵ�.
			// �������� ����ü ������ ��
			if (pOverlapped == &(pSession->m_RecvOverlapped))
			{
				//////////////////////////////////////////////////////
				// �Ϸ�� I/O�� RECV �� ���
				//////////////////////////////////////////////////////

				// �Ϸ������� ���� ����Ʈ ����ŭ �������� Rear�� �Ű��ش�.				
				pSession->m_RecvQ.RelocateWrite(BytesTransferred);

				// ��Ʈ��ũ Ŭ���� ������ �ϼ��� ��Ŷ�� �ִ��� Ȯ��
				// CheckPacket ���ο��� ��� �Ϸ�� ��Ŷ�� ���Ǻ� ��Ŷť�� �־��ش�.
				if (pThis->CheckPacket(pSession) == false)
				{
					// �̻��� ��Ŷ ������ Ȯ���ߴٸ� �ٷ� ������� false ����
					// I/O Count �����ϰ� Recv ���� �ʴ´�.
					pSession->DecreaseIoCount();
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

				//pThis->OnSend(pSession->SessionID, BytesTransferred);				

				ULONG PacketCount = pSession->m_SentPacketCount;				
				CNetPacket **PacketArray = pSession->m_PacketArray;

				// ������ ���� Ȯ�� 
				if (pSession->m_IsSendAndDisconnect == true)
				{
					CNetPacket* pDisconnectPacket = pSession->m_pDisconnectPacket;				

					// ������ ���� �÷��װ� true��� ���� ������ �Ϸ������� �ش� ��Ŷ�� ���ԵǾ����� Ȯ��
					for (ULONG i = 0; i < PacketCount; i++)
					{
						if (PacketArray[i] == pDisconnectPacket)
						{
							// ���� ������ �Ϸ������� ������ ���⿡ �ش��ϴ� ��Ŷ�� �����Ѵٸ� �ش缼�ǰ��� ��������							
							pSession->Disconnect();
							break;
						}
					}
				}

				// �񵿱� I/O�� ��û�� ������ �� ����ȭ ���۸� �������ش�.				
				for (ULONG i = 0; i < PacketCount; i++)
					CNetPacket::Free(PacketArray[i]);

				pSession->m_SentPacketCount = 0;

				// 2. ��û�� �񵿱� Send�� �Ϸ�Ǿ����Ƿ� ���� �ش� ���ǿ� ���� Send�� �� �ְ� �Ǿ���				
				pSession->m_IsSending = false;				
			}

			// I/O Count�� �Ϲ������� �̰����� ���ҽ�Ų��. ( ������ ���� ���� ������ �߻��� ������ ���� ��Ŵ )
			// ���Ұ�� I/O COUNT�� 0�� �Ǹ� �α׾ƿ� �÷��׸� �Ҵ�.		
			// �α׾ƿ� �÷��װ� ������, Auth�� Game���� ��� ���� ��, ���������� GameThread���� ������ ������ �ȴ�.
			pSession->DecreaseIoCount();

			// ��Ŀ ������ �� ������
			//pThis->OnWorkerThreadEnd();
		}

		return 0;
	}

	unsigned int __stdcall CMMOServer::AcceptThread(LPVOID Param)
	{
		//////////////////////////////////////////////////////////////////////////////////
		// 0. AcceptThread ���ο��� ����� ���� �ʱ�ȭ
		//////////////////////////////////////////////////////////////////////////////////

		// Accept ������ �Լ��� static �Լ��̱� ������ ��������� �����ϰų� ����Լ��� ȣ���ϱ� ���� 
		// this �����͸� ���ڷ� �޾ƿ´�.
		CMMOServer *pThis = (CMMOServer*)Param;

		// ���� �迭 
		CMMOSession **SessionArr = pThis->m_SessionArr;

		// ��������
		SOCKET Listen = pThis->m_Listen;

		// IOCP �ڵ�
		HANDLE hIOCP = pThis->m_hWorkerIOCP;

		// ���� ������ ������ ������ �ִ� ť (����, �ּ�, �ε���)		
		CListQueue<WORD> *pNextSession = pThis->m_NextSession;

		// Auth Thread���� ���� ���� ������ �����ϱ� ���� ť		
		CLockFreeQueue<WORD> *pAcceptQueue = pThis->m_AcceptQueue;

		// SessionID, ���� ���� ������ �߱��� �� �ĺ��� 
		ULONGLONG SessionID = 0;

		// ���� ���� ���� Ŭ���̾�Ʈ ���� ������
		ULONGLONG *pSessionCount = &(pThis->m_SessionCount);

		// �ִ� Accept�� �� �ִ� ���� ����
		DWORD MaxSessions = pThis->m_MaxSessions;

		// ������ Ŭ���̾�Ʈ �ּ� ���� 
		SOCKADDR_IN ClientAddr;
		int AddrLen = sizeof(ClientAddr);

		// ����͸��� ����
		ULONGLONG *pAcceptTotal = &(pThis->m_AcceptTotal);
		ULONGLONG *pAcceptTPS = &(pThis->m_AcceptTPS);		

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
					return 0;

				pThis->OnError(WSAGetLastError(), _T("Accept Failed [WSAErrorCode :%d]"));
				continue;
			}

			(*pAcceptTotal)++;

			InterlockedIncrement(pAcceptTPS);

			//////////////////////////////////////////////////////////////////////////////////
			// 2. ���ӿ�û�� ������ ��ȿ�� Ȯ��
			//////////////////////////////////////////////////////////////////////////////////

			// ���� ���� ���� ������ �ִ� ���ǿ� �����ߴٸ� ���� ���´�.
			if (*pSessionCount == MaxSessions)
			{
				closesocket(client);				
				pThis->OnError(GGM_ERROR::TOO_MANY_CONNECTION, _T("Too Many Connection Error Code[%d]"));
				continue;
			}

			//////////////////////////////////////////////////////////////////////////////////
			// 3. ��ȿ�� �����̹Ƿ� AcceptQueue�� �߰�
			// CNetServer������ Accept �����尡 ���� ���� ó�� ������ �� RecvPost���� �����ߴ�.
			// ������ CMMOServer������ �̿� �۾��� Auth���� �ѱ��.
			//////////////////////////////////////////////////////////////////////////////////

			// �ش� ���� ������ ������ Session�迭�� �ε����� ����
			WORD SessionSlot;			
			bool bOk = pNextSession->Dequeue(&SessionSlot);

			if (bOk == false)
			{
				// ��ť ���� ( ť ����� 0 )
				closesocket(client);				
				pThis->OnError(GGM_ERROR::INDEX_STACK_POP_FAILED, _T("INDEX_STACK_POP_FAILED [%d]"));
				continue;
			}

			// Accept Thread�� �ּ����� Ŭ���̾�Ʈ ����ó���� ��
			// �������� AuthThread�� �˾Ƽ�
			SessionArr[SessionSlot]->m_socket = client;
			SessionArr[SessionSlot]->m_SessionIP = ClientAddr.sin_addr;
			SessionArr[SessionSlot]->m_SessionPort = ntohs(ClientAddr.sin_port);
			SessionArr[SessionSlot]->m_SessionSlot = SessionSlot;
			SessionArr[SessionSlot]->m_SessionID = ((SessionID << 16) | SessionSlot);	
			SessionID++;
			
			// IOCP �� ���� ���� 
			HANDLE hRet = CreateIoCompletionPort((HANDLE)client, hIOCP, (ULONG_PTR)SessionArr[SessionSlot], 0);

			if (hRet != hIOCP)
			{
				pThis->OnError(GetLastError(), _T("CreateIoCompletionPort Failed %d"));
				closesocket(client);				
				continue;
			}			

			// Auth Thread���� ���ο� Ŭ���̾�Ʈ�� ���������� �˸��� ���� ��ť		
			bOk = pAcceptQueue->Enqueue(SessionSlot);

			if (bOk == false)
			{
				closesocket(client);				
				pThis->OnError(GGM_ERROR::LOCK_FREE_Q_ENQ_FAILED, _T("LOCK_FREE_Q_ENQ_FAILED [%d]"));
				continue;
			}

			// ���� ���� Ŭ���̾�Ʈ �� ����			
			InterlockedIncrement(pSessionCount);
		}

		return 0;

	}

	unsigned int __stdcall CMMOServer::SendThread(LPVOID Param)
	{
		// SendThread�� ������ �ξ��� �� �ڽ��� �ĺ��� ������ȣ
		static LONG SendThreadID = -1;
		LONG MyID = InterlockedIncrement(&SendThreadID);

		// this ���
		CMMOServer *pThis = (CMMOServer*)Param;

		// �ִ� ������ �� ���
		// SendThread ���� �����ڸ� �ɰ�� ó�� 
		DWORD MaxSessions = (pThis->m_MaxSessions / SEND_THREAD_COUNT);

		// SendThread ����Ÿ�� ���
		WORD  SendSleepTime = pThis->m_SendSleepTime;

		// ���� �÷��� ������ ���
		bool  *pIsExit = &pThis->m_IsExit;

		// ���� �迭 ���
		// SendThread�� ���� ID�� ���� ����� ������ ������ ���Ѵ�.
		// ������ 5õ���̰� SendThread�� 2����� ���̵� 0���� ������� 0~2499�� ���Ǳ���, 1���� ������� 2500~4999 ���Ǳ��� ��� 
		CMMOSession **SessionArr = ((pThis->m_SessionArr) + (MyID * ((ULONGLONG)MaxSessions)));	

		HANDLE hEvent = CreateEvent(nullptr, false, false, nullptr);

		if (hEvent == NULL)
		{
			pThis->OnError(GetLastError(), _T("[CMMOServer] SendThread CreateEvent Failed"));
			return 0;
		}

		while (true)
		{
			//////////////////////////////////////////////////////////////
			// Send Thread�� �ӹ�
			// * �����ð� ���� ����� �� ���Ǻ� SendQ�� �ִ°� ���� Send
			// * ���� I/O Count�� 0���� �������� �α׾ƿ� �÷��� ON
			// * �α׾ƿ� �÷��״� CMMOServer�� ������ ����ȭ�� �ٽ�
			//////////////////////////////////////////////////////////////

			//SleepEx(SendSleepTime, false);
				
			WaitForSingleObject(hEvent, SendSleepTime);

			// ���� ������ �ݺ��� Ż��!
			if (*pIsExit == true)
				break;

			// ���� �迭 �ݺ��� ���鼭 SendPost
			for (DWORD SessionIdx = 0; SessionIdx < MaxSessions; SessionIdx++)
			{
				// �α׾ƿ� ���¶�� Send�õ� ���� ����	
				// �α׾ƿ� ������ ��쿡�� ���� Auth�� Game��忡 �־ �̹� I/O Count�� 0���� ������ �����̴�.
				// ���� �������� ������ �� ���̰� SendPost ȣ�� ��ü�� ���ǹ��ϸ� �����̴�.
				// �ѹ� �α׾ƿ� �÷��� ������ ���� ������ �� ������ �ɶ����� ��� ��������				
				if (SessionArr[SessionIdx]->m_IsLogout == true)
					continue;

				if(SessionArr[SessionIdx]->m_SendQ.size() > 0)
				{
					// �α׾ƿ� ���� �ƴ϶�� SendPost!
					// ���ο��� I/O Count 0�� �Ǹ� �α׾ƿ� �÷��� ���� 				
					pThis->SendPost(SessionArr[SessionIdx]);
				}
			}

		}

		return 0;
	}

	unsigned int __stdcall CMMOServer::AuthThread(LPVOID Param)
	{
		// this ���
		CMMOServer *pThis = (CMMOServer*)Param;

		// �ִ� ������ �� ���
		DWORD MaxSessions = pThis->m_MaxSessions;

		// AuthThread ����Ÿ�� ���
		WORD  AuthSleepTime = pThis->m_AuthSleepTime;

		// ���� �÷��� ������ ���
		bool  *pIsExit = &pThis->m_IsExit;

		// ���� �迭 ���
		CMMOSession **SessionArr = pThis->m_SessionArr;

		// AcceptQueue ���		
		CLockFreeQueue<WORD> *pAcceptQueue = pThis->m_AcceptQueue;

		// �� ������ ���Ǻ� ��Ŷ ó����
		WORD AuthPacketPerLoop = pThis->m_AuthPacketPerLoop;

		// �� ������ AcceptQ���� Deq�� ó����
		WORD AcceptPerLoop = pThis->m_AcceptPerLoop;

		// ����͸� ����
		volatile LONG *pAuthLoopPerSec = &pThis->m_AuthLoopPerSec;				
		LONG *pAuthSession = &pThis->m_AuthSession;

		// Ÿ�Ӿƿ� ����		
		ULONGLONG TimeoutLimit = pThis->m_TimeoutLimit;
		bool      IsTimeout    = pThis->m_IsTimeout;

		HANDLE hEvent = CreateEvent(nullptr, false, false, nullptr);

		if (hEvent == NULL)
		{
			pThis->OnError(GetLastError(), _T("[CMMOServer] AuthThread CreateEvent Failed"));
			return 0;
		}

		while (true)
		{
			//////////////////////////////////////////////////////////////////////
			// Auth Thread�� �ӹ�
			// * �����ð� ���� ����� ���� Ŭ���̾�Ʈ ����ó��
			// * �α��� ��Ŷ ó�� �� ������������ �ε� ������ ������ ���� DB���� �ε�
			// * ��Ŷó���̿ܿ� ���־�� �ϴ� ������ ó��, pThis->OnAuth Update()
			// * ����ó�� �Ϸ�� Ŭ���̾�Ʈ�� Game Thread�� �̰�			
			// * �α׾ƿ� �÷��� ���� ���ǿ� ���ؼ� �α׾ƿ� ó��
			// * ���� ������� GameThread������ ó��
			//////////////////////////////////////////////////////////////////////					

			WaitForSingleObject(hEvent, AuthSleepTime);
			
			// ����͸������� Auth Thread ���� ī��Ʈ ����
			InterlockedIncrement(pAuthLoopPerSec);	

			// ���� ���� Ȯ��
			if (*pIsExit == true)
				break;

			// ���� ������ Ŭ���̾�Ʈ�� �ִ��� Ȯ��
			WORD AcceptCount = 0;
			while (pAcceptQueue->size() > 0)
			{				
				// ���� ó���� �̰����� ������ 				
				WORD SessionSlot;
				pAcceptQueue->Dequeue(&SessionSlot);

				// ������ ������ �� ������ �ʱ�ȭ					
				InterlockedIncrement(&SessionArr[SessionSlot]->m_IoCount);				
				SessionArr[SessionSlot]->m_IsAuthToGame = false;
				SessionArr[SessionSlot]->m_IsSendAndDisconnect = false;
				SessionArr[SessionSlot]->m_pDisconnectPacket = nullptr;				
				SessionArr[SessionSlot]->m_PacketQ.clear();				
				SessionArr[SessionSlot]->m_SendQ.clear();
				SessionArr[SessionSlot]->m_RecvQ.ClearRingBuffer();				
				SessionArr[SessionSlot]->m_SentPacketCount = 0;
				SessionArr[SessionSlot]->m_Mode = SESSION_MODE::MODE_AUTH;			
				SessionArr[SessionSlot]->m_HeartbeatTime = GetTickCount64();
				SessionArr[SessionSlot]->m_IsSending = false;				
				SessionArr[SessionSlot]->m_IsCanceled = false;
				SessionArr[SessionSlot]->m_IsLogout = false;

				// Auth Mode ����ó��							
				SessionArr[SessionSlot]->OnAuth_ClientJoin();				

				// ���� RecvPost ȣ��
				pThis->RecvPost(SessionArr[SessionSlot]);	
				SessionArr[SessionSlot]->DecreaseIoCount();

				// AcceptQ���� Deq�ؼ� ó���� Ƚ���� ������Ű�� ����ġ�� �Ǿ����� ���� Ż�� 
				AcceptCount++;
				(*pAuthSession)++;				

				if (AcceptCount >= AcceptPerLoop)
					break;
			}

			// ���� �迭 ���鼭 ���ǿ� ���õ� ó��			
			for (DWORD SessionIdx = 0; SessionIdx < MaxSessions; SessionIdx++)
			{
				//////////////////////////////////////////////////////////////////////
				// 1. ���� ���� ���� ó��
				// 2. �α׾ƿ� ó��
				// 3. ��Ŷ ó��				
				//////////////////////////////////////////////////////////////////////

				// ���� ������ ��尡 AUTH�� ��쿡�� ó���Ѵ�.
				if (SessionArr[SessionIdx]->m_Mode != SESSION_MODE::MODE_AUTH)
					continue;

				// ������ GameMode�� �̵��ؾ� �ϴ��� �˻�
				// AuthToGame �÷��״� ���������� ����
				if (SessionArr[SessionIdx]->m_IsAuthToGame == true)
				{
					// AuthToGame �÷��װ� �����ٸ� ���� ó�� �� ��� ����
					SessionArr[SessionIdx]->OnAuth_ClientLeave(false);
					(*pAuthSession)--;
					SessionArr[SessionIdx]->m_Mode = SESSION_MODE::MODE_AUTH_TO_GAME;
					continue;
				}

				// �α׾ƿ� ������� Ȯ��
				if (SessionArr[SessionIdx]->m_IsLogout == true)
				{
					// ���� Send���̶�� �� ���ǿ� ���ؼ��� ������ �� �� ����
					if (SessionArr[SessionIdx]->m_IsSending == true)
						continue;

					// Send���� �ƴ϶�� AuthThread ���� ó�� �� MODE_WAIT_LOGOUT ��� ����
					// �ش� ���� ����Ǿ ���� ������� ReleaseThread�� ���ؼ� �����
					SessionArr[SessionIdx]->OnAuth_ClientLeave(true);	
					(*pAuthSession)--;
					SessionArr[SessionIdx]->m_Mode = SESSION_MODE::MODE_WAIT_LOGOUT;
				}
				else
				{					
					// �α׾ƿ� ����� �ƴ϶�� ��Ŷ ó�� 
					// ���� ���������� �� ������ ������ ������ ����.
					// ���� ������ �� ������ �� ���� ��Ŷ�� ó������ ���ϰ� �׸�ŭ ó���Ѵ�.
					// ���Ǻ� ��Ŷó���� �����ϰ� �ϱ� ���� ���
					ULONGLONG PacketCount = SessionArr[SessionIdx]->m_PacketQ.size();

					if (PacketCount > AuthPacketPerLoop)
						PacketCount = AuthPacketPerLoop;

					for (WORD Count = 0; Count < PacketCount; Count++)
					{
						// Deq �� ��Ŷ�� ó���� ��, Recv�Ϸ��������� �Ѱ��� �� �ø� RefCnt�� ���̱� ���� Free
						CNetPacket* pPacket;
						SessionArr[SessionIdx]->m_HeartbeatTime = GetTickCount64();
						SessionArr[SessionIdx]->m_PacketQ.Dequeue(&pPacket);
						SessionArr[SessionIdx]->OnAuth_ClientPacket(pPacket);
						CNetPacket::Free(pPacket);
					}

					// Ÿ�Ӿƿ� ó���ؾ��Ѵٸ� ó�� 
					if (IsTimeout == true)
					{
						ULONGLONG Time = GetTickCount64() - SessionArr[SessionIdx]->m_HeartbeatTime;
						if (Time > TimeoutLimit)
						{				
							++(pThis->m_AuthTimeoutDisconnectCount);
							SessionArr[SessionIdx]->Disconnect();
							pThis->OnTimeOut(SessionArr[SessionIdx]->m_SessionID, SessionArr[SessionIdx]->m_SessionSlot, Time);
						}
					}
				}
			}

			// �α׾ƿ� ó��, ��Ŷó��, AuthToGame ó���� ��� �����ٸ� Ŭ���̾�Ʈ ��û�� �������� Update ����
			pThis->OnAuth_Update();
		}

		CloseHandle(hEvent);
		return 0;
	}

	unsigned int __stdcall CMMOServer::GameThread(LPVOID Param)
	{
		// this ���
		CMMOServer *pThis = (CMMOServer*)Param;

		// �ִ� ������ �� ���
		DWORD MaxSessions = pThis->m_MaxSessions;

		// AuthThread ����Ÿ�� ���
		WORD  GameSleepTime = pThis->m_GameSleepTime;

		// ���� �÷��� ������ ���
		bool  *pIsExit = &pThis->m_IsExit;

		// ���� �迭 ���
		CMMOSession **SessionArr = pThis->m_SessionArr;

		// ���� ������ ������ ������ �ִ� ť (����, �ּ�, �ε���)
		// ���� ������� ���⿡ ���� ���� ��ȯ
		CListQueue<WORD> *pNextSession = pThis->m_NextSession;	

		// �� ������ ���Ǻ� ��Ŷ ó����
		WORD GamePacketPerLoop = pThis->m_GamePacketPerLoop;

		// �� ������ AuthToGame ó����
		WORD AuthToGamePerLoop = pThis->m_AuthToGamePerLoop;

		// ����͸��� ����
		volatile LONG *pGameLoopPerSec = &pThis->m_GameLoopPerSec;
		volatile ULONGLONG *pSessionCount = &(pThis->m_SessionCount);
		LONG *pGameSession = &pThis->m_GameSession;		

		// Ÿ�Ӿƿ� ����
		ULONGLONG TimeoutLimit = pThis->m_TimeoutLimit;
		bool      IsTimeout = pThis->m_IsTimeout;

		HANDLE hEvent = CreateEvent(nullptr, false, false, nullptr);

		if (hEvent == NULL)
		{
			pThis->OnError(GetLastError(), _T("[CMMOServer] GameThread CreateEvent Failed"));
			return 0;
		}

		while (true)
		{
			//////////////////////////////////////////////////////////////////////
			// Game Thread�� �ӹ�
			// * �����ð� ���� ����� Game Mode ���� ó�� 			
			// * ������ ���־�� �� ���ǵ� ������ ����
			// * AuthToGame Mode�� �ִ� ���ǵ��� Game Mode�� �̵�
			// * �α׾ƿ� �÷��� ���� ���ǿ� ���ؼ� �α׾ƿ� ó��			
			// * GameMode�� �ִ� ���ǵ� ��Ŷ ó�� 
			// * ��Ŷó���̿ܿ� ���־�� �ϴ� ������ ó��, pThis->Auth Update()							
			// * ���� ������� GameThread������ ó��
			//////////////////////////////////////////////////////////////////////			

			WaitForSingleObject(hEvent, GameSleepTime);
			
			// ����͸������� Game Thread ���� ī��Ʈ ����
			InterlockedIncrement(pGameLoopPerSec);

			// ���� ���� Ȯ��
			if (*pIsExit == true)
				break;

			// ���� �迭 ���鼭 ���ǿ� ���õ� ó��
			WORD AuthToGameCount = 0;
			for (DWORD SessionIdx = 0; SessionIdx < MaxSessions; SessionIdx++)
			{
				//////////////////////////////////////////////////////////////////////
				// 1. AuthToGame Mode�� �ִ� ���ǵ��� Game Mode�� �̵�
				// 2. ���� ������ ����� ���� �����ϱ� 
				// 3. �α׾ƿ� ó�� 
				// 4. ��Ŷ ó��				
				//////////////////////////////////////////////////////////////////////
				
				// �ѹ��� Auth To Game���� Game ���� �ٲ� ���� ���� �����Ѵ�. 
				if ((SessionArr[SessionIdx]->m_Mode == SESSION_MODE::MODE_AUTH_TO_GAME) && (AuthToGameCount < AuthToGamePerLoop))
				{
					// Game Mode�� ���� ó�� �� ��� ����
					SessionArr[SessionIdx]->OnGame_ClientJoin();
					(*pGameSession)++;
					SessionArr[SessionIdx]->m_Mode = SESSION_MODE::MODE_GAME;
					AuthToGameCount++;
				}

				// ���� ������ ������� Ȯ��
				if (SessionArr[SessionIdx]->m_Mode == SESSION_MODE::MODE_WAIT_LOGOUT)
				{					
					pThis->ReleaseSession(SessionArr[SessionIdx]);
					InterlockedDecrement(pSessionCount);
					continue;
				}

				// �� �Ʒ����ʹ� ���� ��尡 Game�� ��쿡�� ó���Ѵ�.				
				if (SessionArr[SessionIdx]->m_Mode != SESSION_MODE::MODE_GAME)
					continue;				

				// �α׾ƿ� ������� Ȯ��
				if (SessionArr[SessionIdx]->m_IsLogout == true)
				{
					// ���� Send���̶�� �� ���ǿ� ���ؼ��� ������ �� �� ����
					if (SessionArr[SessionIdx]->m_IsSending == true)
						continue;

					// Send���� �ƴ϶�� GameThread ���� ó�� �� MODE_WAIT_LOGOUT ��� ����					
					SessionArr[SessionIdx]->OnGame_ClientLeave();
					(*pGameSession)--;
					//SessionArr[SessionIdx]->m_Mode = SESSION_MODE::MODE_WAIT_LOGOUT;
					pThis->ReleaseSession(SessionArr[SessionIdx]);
					InterlockedDecrement(pSessionCount);
					continue;
				}				
				
				// �α׾ƿ� ����� �ƴ϶�� ��Ŷ ó�� 
				// ������� ������ ���� ���������� �� ������ ������ ������ ����.
				// ���� ������ �� ������ �� ���� ��Ŷ�� ó������ ���ϰ� �׸�ŭ ó���Ѵ�.
				// ���Ǻ� ��Ŷó���� �����ϰ� �ϱ� ���� ���
				ULONGLONG PacketCount = SessionArr[SessionIdx]->m_PacketQ.size();

				if (PacketCount > GamePacketPerLoop)
					PacketCount = GamePacketPerLoop;

				for (WORD Count = 0; Count < PacketCount; Count++)
				{
					CNetPacket* pPacket;
					SessionArr[SessionIdx]->m_HeartbeatTime = GetTickCount64();
					SessionArr[SessionIdx]->m_PacketQ.Dequeue(&pPacket);
					SessionArr[SessionIdx]->OnGame_ClientPacket(pPacket);
					CNetPacket::Free(pPacket);
				}	

				// Ÿ�Ӿƿ� ó���ؾ��Ѵٸ� ó�� 
				if (IsTimeout == true)
				{
					ULONGLONG Time = GetTickCount64() - SessionArr[SessionIdx]->m_HeartbeatTime;
					if (Time > TimeoutLimit)
					{
						++(pThis->m_GameTimeoutDisconnectCount);
						SessionArr[SessionIdx]->Disconnect();
						pThis->OnTimeOut(SessionArr[SessionIdx]->m_SessionID, SessionArr[SessionIdx]->m_SessionSlot, Time);
					}
				}
			}

			// �α׾ƿ� ó��, ��Ŷó��, AuthToGame ó���� ��� �����ٸ� Ŭ���̾�Ʈ ��û�� �������� Update ����
			pThis->OnGame_Update();			

		}

		return 0;
	}	

	bool CMMOServer::CheckPacket(CMMOSession * pSession)
	{
		CRingBuffer *pRecvQ = &(pSession->m_RecvQ);

		// ���� RECV ������ ��뷮 üũ
		int CurrentUsage = pRecvQ->GetCurrentUsage();
		int Result;

		// �����ۿ� �ּ��� ������� �̻��� �ִ��� ����Ȯ���Ѵ�.
		// ������� �̻��� ������ �������鼭 �����ϴ� �ϼ��� ��Ŷ �� �̾Ƽ� �ش� ���� ��Ŷť�� ��ť
		LONG PacketCount = 0;
		while (CurrentUsage >= NET_HEADER_LENGTH)
		{
			NET_HEADER Header;

			// �ش� ���� �����ۿ��� �����ŭ PEEK
			Result = pRecvQ->Peek((char*)&Header, NET_HEADER_LENGTH);

			if (Result != NET_HEADER_LENGTH)
			{				
				OnError(GGM_ERROR::BUFFER_READ_FAILED, _T("[CMMOServer] NetHeader Peek Failed!!"));				
				return false;
			}

			CurrentUsage -= NET_HEADER_LENGTH;

			// ��Ŷ �ڵ尡 �ùٸ��� �ʴٸ� �ش� ���� ��������
			if (Header.PacketCode != CNetPacket::PacketCode)
			{
				TCHAR SessionIP[IP_LEN];
				InetNtop(AF_INET, &(pSession->m_SessionIP), SessionIP, IP_LEN);
				CLogger::GetInstance()->Log(_T("CMMOServer Log"), LEVEL::ERR, OUTMODE::FILE, _T("WRONG PACKET CODE[%s:%hd]"), SessionIP,htons(pSession->m_SessionPort));
				pSession->Disconnect();
				return false;
			}

			// ������ ���̷ε� ����� ����ȭ ������ ũ�⸦ �ʰ��Ѵٸ� ������´�.
			if (Header.Length > DEFAULT_BUFFER_SIZE - NET_HEADER_LENGTH)
			{
				TCHAR SessionIP[IP_LEN];
				InetNtop(AF_INET, &(pSession->m_SessionIP), SessionIP, IP_LEN);
				CLogger::GetInstance()->Log(_T("CMMOServer Log"), LEVEL::ERR, OUTMODE::FILE, _T("TOO BIG PACKET RECV[%s:%hd]"), SessionIP, htons(pSession->m_SessionPort));
				pSession->Disconnect();				
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
				pSession->Disconnect();
				CNetPacket::Free(pPacket);
				return false;
			}

			// �ϼ��� ��Ŷ�� �����ϹǷ� �ش� ���� ��Ŷť�� ��ť							
			InterlockedIncrement(&pPacket->m_RefCnt);
			pSession->m_PacketQ.Enqueue(pPacket);			

			// ���� �������� ��� ����� ��Ŷ �����ŭ ����
			CurrentUsage -= Header.Length;

			// Alloc�� ���� ����
			CNetPacket::Free(pPacket);	
			PacketCount++;
		}

		// ����͸��� ����
		InterlockedAdd(&m_RecvTPS, PacketCount);		

		return true;
	}

	bool CMMOServer::SendPost(CMMOSession * pSession)
	{
		///////////////////////////////////////////////////////////////////
		// 0. Send ���� �÷��� üũ 
		///////////////////////////////////////////////////////////////////

		// �̹� �ش� ���ǿ� ���ؼ� Send ���̶�� Send���� �ʴ´�.
		if (pSession->m_IsSending == true)
			return true;

		// MMOServer ���������� ���� SendThread������ �ش� ���ǿ� ���ؼ� SendPost�� ȣ���ϱ� ������ ���Ͷ� ������� �ʾƵ� ���� 
		pSession->m_IsSending = true;		
		
		// �α׾ƿ� �÷��׿� ��带 Ȯ���ϴ� ������ ��ġ�̴�. ���� �α׾ƿ� �÷��� Ȯ����
		// �ڿ� ���� ��Ŷ ī��Ʈ�� Ȯ���ϴ� ������ ���� ����ü�� ��Ŷ ������ �迭�� ����� �ʱ� ����
		if (pSession->m_IsLogout == true || pSession->m_SentPacketCount > 0)
		{
			pSession->m_IsSending = false;
			return true;
		}
		
		CListQueue<CNetPacket*> *pSendQ = &(pSession->m_SendQ);

		// ���� SendQ ��뷮�� Ȯ���ؼ� ���� ���� �ִ��� �ٽ� �ѹ� Ȯ���غ���.
		// ���� ���� �ִ��� �˰� ���Դµ� ���� ���� ���� ���� �ִ�.
		ULONG CurrentUsage = (ULONG)pSendQ->size();

		if(CurrentUsage == 0)
		{
			pSession->m_IsSending = false;
			return true;
		}
		
		///////////////////////////////////////////////////////////////////
		// 1. WSASend�� Overlapped ����ü �ʱ�ȭ
		///////////////////////////////////////////////////////////////////
		ZeroMemory(&(pSession->m_SendOverlapped), sizeof(OVERLAPPED));

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
		CNetPacket **PacketArray = pSession->m_PacketArray;

		// SendQ���� �ѹ��� �۽� ������ ��Ŷ�� �ִ� ������ŭ ����ȭ ������ �����͸� Dequeue�Ѵ�.		
		// ���Ǻ��� ������ �ִ� ����ȭ ���� ������ �迭�� �װ��� �����Ѵ�.
		// Peek�� �� �� �Ϸ����� ���Ŀ� Dequeue�� �ϸ� memcpy�� �߰������� �Ͼ�Ƿ� �޸𸮸� ����ؼ� Ƚ���� �ٿ���.			

		// ���� ť�� ����ִ� �������� ������ �ִ�ġ�� �ʰ��ߴٸ� �������ش�.
		if (CurrentUsage > MAX_PACKET_COUNT)
			CurrentUsage = MAX_PACKET_COUNT;

		for (ULONG i = 0; i < CurrentUsage; i++)
			pSession->m_SendQ.Dequeue(&PacketArray[i]);

		// ���� ��Ŷ�� ������ ����ߴٰ� ���߿� �Ϸ������� ���� �޸𸮸� �����ؾ� �Ѵ�.			
		DWORD PacketCount = pSession->m_SentPacketCount = CurrentUsage;
	
		// ����͸��� ����
		InterlockedAdd(&m_SendTPS, PacketCount);

		// ��Ŷ ������ŭ �ݺ��� ���鼭 ���� ����ȭ ���� ���� ��Ŷ ���� �����͸� WSABUF����ü�� ����
		for (DWORD i = 0; i < PacketCount; i++)
		{
			wsabuf[i].buf = (char*)PacketArray[i]->m_pSerialBuffer;
			wsabuf[i].len = PacketArray[i]->m_Rear;
		}

		///////////////////////////////////////////////////////////////////
		// 3. WSASend ����ϱ� ���� I/O ī��Ʈ ����
		///////////////////////////////////////////////////////////////////

		InterlockedIncrement(&(pSession->m_IoCount));

		///////////////////////////////////////////////////////////////////
		// 4. WSASend ��û
		///////////////////////////////////////////////////////////////////
		DWORD bytesSent = 0;
		int Result = WSASend(pSession->m_socket, wsabuf, PacketCount, &bytesSent, 0, &(pSession->m_SendOverlapped), nullptr);

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
				pSession->m_IsSending = false;
				pSession->DecreaseIoCount();				
			}
		}		

		// Disconnect �Լ��� ȣ��Ǿ I/O�� ��� ��ҵǾ���Ѵٸ� ��� �� ��Ŀ�����尡 ��û�� I/O�� ����Ѵ�.
		if (pSession->m_IsCanceled == true)
			CancelIoEx((HANDLE)pSession->m_socket, nullptr);

		return true;
	}

	bool CMMOServer::RecvPost(CMMOSession * pSession)
	{
		///////////////////////////////////////////////////////////////////
		// 0. I/O ��û�� �ϱ� ���� ���� �������� ���������� �˻��Ѵ�.
		///////////////////////////////////////////////////////////////////

		CRingBuffer *pRecvQ = &(pSession->m_RecvQ);
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

				// I/O Count�� 0���� ���� ������ ������ �ǵ��� shutdown���� �������		
			OnError(GGM_ERROR::BUFFER_FULL, _T("[CNetServer] RecvPost() RecvQ CurrentSpare == 0 ErrorCode[%d]"));
			pSession->Disconnect();

			return true;
		}

		///////////////////////////////////////////////////////////////////
		// 1. WSARecv�� Overlapped ����ü �ʱ�ȭ
		///////////////////////////////////////////////////////////////////

		ZeroMemory(&(pSession->m_RecvOverlapped), sizeof(OVERLAPPED));

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
		InterlockedIncrement(&(pSession->m_IoCount));

		///////////////////////////////////////////////////////////////////
		// 4. I/O ��û
		///////////////////////////////////////////////////////////////////
		DWORD BytesRead = 0;
		DWORD Flags = 0;
		int Result = WSARecv(pSession->m_socket, wsabuf, RecvBufCount, &BytesRead, &Flags, &(pSession->m_RecvOverlapped), nullptr);

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
				pSession->DecreaseIoCount();
			}
		}

		// Disconnect �Լ��� ȣ��Ǿ I/O�� ��� ��ҵǾ���Ѵٸ� ��� �� ��Ŀ�����尡 ��û�� I/O�� ����Ѵ�.
		if (pSession->m_IsCanceled == true)
			CancelIoEx((HANDLE)pSession->m_socket, nullptr);

		return true;
	}

	void CMMOServer::ReleaseSession(CMMOSession * pSession)
	{
		//////////////////////////////////////////////////////////////////
		// ������ �� �ؾ� ����					
		// 1. ���� ��Ŷ �迭, SendQ, CompletePacketQ�� ���� ���� ��Ŷ ó�� 
		// 2. closesocket
		// 3. ���� ��� NONE���� ����
		// 4. ��� ���� ���� �ε��� ��ȯ
		//////////////////////////////////////////////////////////////////

		// ���� ��Ŷ �迭 ����
		ULONG PacketCount = pSession->m_SentPacketCount;
		CNetPacket **PacketArray = pSession->m_PacketArray;
		for (WORD Count = 0; Count < PacketCount; Count++)
			CNetPacket::Free(PacketArray[Count]);

		// SendQ ����
		ULONG GarbagePacketCount = (ULONG)pSession->m_SendQ.size();
		for (ULONG Count = 0; Count < GarbagePacketCount; Count++)
		{
			CNetPacket *pGarbagePacket;
			pSession->m_SendQ.Dequeue(&pGarbagePacket);
			CNetPacket::Free(pGarbagePacket);
		}

		// ���� ��Ŷ ť ����
		GarbagePacketCount = (ULONG)pSession->m_PacketQ.size();
		for (ULONG Count = 0; Count < GarbagePacketCount; Count++)
		{
			CNetPacket *pGarbagePacket;
			pSession->m_PacketQ.Dequeue(&pGarbagePacket);
			CNetPacket::Free(pGarbagePacket);
		}

		// ��庯��
		pSession->m_Mode = SESSION_MODE::MODE_NONE;

		// ���� ����		
		closesocket(pSession->m_socket);
		pSession->m_socket = INVALID_SOCKET;

		// OnRelease
		pSession->OnClientRelease();

		// �ε��� ť�� ��ť
		m_NextSession->Enqueue(pSession->m_SessionSlot);
	}

	bool CMMOServer::WSAInit(TCHAR * BindIP, USHORT port, bool IsNoDelay)
	{		
		WSADATA wsa;	
		if (int SocketError = WSAStartup(MAKEWORD(2, 2), &wsa))
		{
			OnError(SocketError, _T("WSAStartup Failed ErrorCode[%d]"));
			return false;
		}
			
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
			OnError(WSAGetLastError(), _T("Listen socket Creation Failed ErrorCode[%d]"));
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
		
		int BufSize = SEND_BUF_SIZE;
		Result = setsockopt(m_Listen, SOL_SOCKET, SO_SNDBUF, (const char*)&BufSize, sizeof(BufSize));

		if (Result == SOCKET_ERROR)
		{
			OnError(WSAGetLastError(), _T("setsockopt [SO_SNDBUF] Failed"));
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

	bool CMMOServer::SessionInit(WORD MaxSessions, CMMOSession *pPlayerArr, int PlayerSize)
	{
		// �ִ� ������ ��  ����� ���� 
		m_MaxSessions = MaxSessions;

		// �ִ� ���� ���� ��ŭ CMMOSession* �迭 �����Ҵ�
		m_SessionArr = new CMMOSession*[MaxSessions];

		if (m_SessionArr == nullptr)
		{
			OnError(GetLastError(), _T("MEM Alloc Failed ErrorCode[%d]"));
			return false;
		}

		// CNetServer�� �ٸ��� CMMOServer�� Session�� Player�� �Ϻ��ϰ� �и����� �ʴ´�.
		// ������ ��⿡�� CMMOSession Ŭ������ ��ӹ޾� Player�� �����ϰ� �� �迭�� �����ּҸ� ���ڷ� �־��ش�.
		// ��Ʈ��ũ ��⿡���� �� Player�� �����͸� ���̽������ͷ� ����ȯ�� ��Ʈ��ũ ó���� �����Ѵ�.
		// CMMOServer ���忡���� ���������� ������ �÷��̾ ���� ������ �������ϱ� ������ ��ü ũ�⵵ ���� ���ڷ� �޴´�.
		for (WORD i = 0; i < MaxSessions; i++)
		{
			m_SessionArr[i] = (CMMOSession*)((char*)pPlayerArr + (PlayerSize * i));
		}

		// �ش� ������ Accept�����忡�� Ŭ���̾�Ʈ�� ������ �޾��� ��, Enqueue�Ͽ� Auth Thread�� ����
		// ����ü���� Session�� ���� �⺻���� (����, �ּ�, �迭�� �ε���)�� ���ԵǾ� ����
		//m_NextSession = new CListQueue<SessionInfo*>();
		m_NextSession = new CListQueue<WORD>();

		if (m_NextSession == nullptr)
		{
			OnError(GetLastError(), _T("MEM Alloc Failed ErrorCode[%d]"));
			return false;
		}

		// ó���̴ϱ� ��� �ε����� �־��ش�.	
		for (WORD i = 0; i < (MaxSessions); i++)
		{
			m_NextSession->Enqueue(i);
		}

		// AcceptThread�� Auth Thread ������ ������ ť
		// ���ο� Ŭ���̾�Ʈ�� �����ϸ� �� ť�� AcceptThread�� ��ť, Auth Thread�� �� �����Ӹ��� ť�� Ȯ���Ͽ� ����ó��		
		m_AcceptQueue = new CLockFreeQueue<WORD>(0, MAX_LOCK_FREE_Q_SIZE);

		if (m_AcceptQueue == nullptr)
		{
			OnError(GetLastError(), _T("MEM Alloc Failed ErrorCode[%d]"));
			return false;
		}

		return true;
	}

	bool CMMOServer::ThreadInit(DWORD ConcurrentWorkerThreads, DWORD MaxWorkerThreads)
	{
		// ��Ŀ������� ������ IOCP ����
		m_hWorkerIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, ConcurrentWorkerThreads);

		if (m_hWorkerIOCP == NULL)
		{
			OnError(GetLastError(), _T("CreateIoCompletionPort Failed ErrorCode[%d]"));
			return false;
		}

		// ������ ���� �� ���� ����
		m_WorkerThreadHandleArr = new HANDLE[MaxWorkerThreads];

		if (m_WorkerThreadHandleArr == nullptr)
		{
			OnError(GetLastError(), _T("MEM Alloc Failed ErrorCode[%d]"));
			return false;
		}

		m_WorkerThreadIDArr = new DWORD[MaxWorkerThreads];

		if (m_WorkerThreadIDArr == nullptr)
		{
			OnError(GetLastError(), _T("MEM Alloc Failed"));
			return false;
		}

		/////////////////// WorkerThread ���� //////////////////////////////
		// ����ڰ� ��û�� ������ ��Ŀ ������(���ý��� ���� �ƴ�) ����
		for (DWORD i = 0; i < MaxWorkerThreads; i++)
		{
			m_WorkerThreadHandleArr[i] = (HANDLE)_beginthreadex(
				nullptr,
				0,
				CMMOServer::WorkerThread, // ��Ŀ ������ �Լ� Ŭ���� ���ο� private static���� ����Ǿ�����
				this, // ��Ŀ������ �Լ��� static���� ����Ǿ������Ƿ� �ٸ� ��� ������ �Լ��� �����ϱ� ���ؼ� �ش� �ν��Ͻ��� this �����Ͱ� �ʿ���
				0,
				(unsigned int*)&m_WorkerThreadIDArr[i]
			);

			if (m_WorkerThreadIDArr[i] == NULL)
			{
				OnError(GetLastError(), _T("_beginthreadex Failed ErrorCode[%d]"));
				return false;
			}
		}
		/////////////////// WorkerThread ���� //////////////////////////////	

		/////////////////// AcceptThread ���� //////////////////////////////
		m_AcceptThread = (HANDLE)_beginthreadex(
			nullptr,
			0,
			CMMOServer::AcceptThread, // Accept ������ �Լ� Ŭ���� ���ο� private static���� ����Ǿ�����
			this, // Accept ������ �Լ��� static���� ����Ǿ������Ƿ� �ٸ� ��� ������ �Լ��� �����ϱ� ���ؼ� �ش� �ν��Ͻ��� this �����Ͱ� �ʿ���
			0,
			(unsigned int*)&m_AcceptThreadID
		);

		if (m_AcceptThread == NULL)
		{
			OnError(GetLastError(), _T("_beginthreadex Failed  ErrorCode[%d]"));
			return false;
		}
		/////////////////// AcceptThread ���� //////////////////////////////		

		/////////////////// SendThread ���� //////////////////////////////
		for (int i = 0; i < SEND_THREAD_COUNT; i++)
		{
			m_SendThread[i] = (HANDLE)_beginthreadex(
				nullptr,
				0,
				CMMOServer::SendThread, // Accept ������ �Լ� Ŭ���� ���ο� private static���� ����Ǿ�����
				this, // Accept ������ �Լ��� static���� ����Ǿ������Ƿ� �ٸ� ��� ������ �Լ��� �����ϱ� ���ؼ� �ش� �ν��Ͻ��� this �����Ͱ� �ʿ���
				0,
				(unsigned int*)&m_SendThreadID[i]
			);

			if (m_SendThread[i] == NULL)
			{
				OnError(GetLastError(), _T("_beginthreadex Failed  ErrorCode[%d]"));
				return false;
			}
		}
		/////////////////// SendThread ���� //////////////////////////////		

		/////////////////// AuthThread ���� //////////////////////////////
		m_AuthThread = (HANDLE)_beginthreadex(
			nullptr,
			0,
			CMMOServer::AuthThread, // Accept ������ �Լ� Ŭ���� ���ο� private static���� ����Ǿ�����
			this, // Accept ������ �Լ��� static���� ����Ǿ������Ƿ� �ٸ� ��� ������ �Լ��� �����ϱ� ���ؼ� �ش� �ν��Ͻ��� this �����Ͱ� �ʿ���
			0,
			(unsigned int*)&m_AuthThreadID
		);

		if (m_AuthThread == NULL)
		{
			OnError(GetLastError(), _T("_beginthreadex Failed  ErrorCode[%d]"));
			return false;
		}
		/////////////////// AuthThread ���� //////////////////////////////	

		/////////////////// GameThread ���� //////////////////////////////
		m_GameThread = (HANDLE)_beginthreadex(
			nullptr,
			0,
			CMMOServer::GameThread, // Accept ������ �Լ� Ŭ���� ���ο� private static���� ����Ǿ�����
			this, // Accept ������ �Լ��� static���� ����Ǿ������Ƿ� �ٸ� ��� ������ �Լ��� �����ϱ� ���ؼ� �ش� �ν��Ͻ��� this �����Ͱ� �ʿ���
			0,
			(unsigned int*)&m_GameThreadID
		);

		if (m_GameThreadID == NULL)
		{
			OnError(GetLastError(), _T("_beginthreadex Failed  ErrorCode[%d]"));
			return false;
		}
		/////////////////// GameThread ���� //////////////////////////////	

		/////////////////// ReleaseThread ���� //////////////////////////////
		//m_ReleaseThread = (HANDLE)_beginthreadex(
		//	nullptr,
		//	0,
		//	CMMOServer::ReleaseThread, // Accept ������ �Լ� Ŭ���� ���ο� private static���� ����Ǿ�����
		//	this, // Accept ������ �Լ��� static���� ����Ǿ������Ƿ� �ٸ� ��� ������ �Լ��� �����ϱ� ���ؼ� �ش� �ν��Ͻ��� this �����Ͱ� �ʿ���
		//	0,
		//	(unsigned int*)&m_ReleaseThreadID
		//);

		//if (m_ReleaseThreadID == NULL)
		//{
		//	OnError(GetLastError(), _T("_beginthreadex Failed  ErrorCode[%d]"));
		//	return false;
		//}
		/////////////////// ReleaseThread ���� //////////////////////////////		

		return true;
	}
}







