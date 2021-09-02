#include "CLanClient.h"
#include "Logger\Logger.h"
#include "Define\GGM_CONSTANTS.h"
#include "Define\GGM_ERROR.h"

ULONGLONG ReconnectCount = 0;

namespace GGM
{
	bool GGM::CLanClient::Start(
		TCHAR * ConnectIP, 
		USHORT port, 
		DWORD ConcurrentThreads, 
		DWORD MaxThreads, 
		bool IsNoDelay, 
		bool IsReconnect,
		int  ReconnectDelay
	)
	{	
		bool IsSuccess = WSAInit(ConnectIP, port, IsNoDelay);

		if (IsSuccess == false)
			return false;
		
		m_MySession.SendQ.InitLockFreeQueue(0, MAX_LOCK_FREE_Q_SIZE);
		
		IsSuccess = CPacket::CreatePacketPool(0);

		if (IsSuccess == false)
			return false;

		IsSuccess = ConnectToLanServer(IsReconnect, ReconnectDelay, ConcurrentThreads);

		if (IsSuccess == false)
			return false;

		IsSuccess  = ThreadInit(ConcurrentThreads, MaxThreads);

		if (IsSuccess == false)
			return false;
		
		return true;
	}

	void GGM::CLanClient::Stop()
	{
		// ������ off
		m_IsReconnect = false;

		// ��Ŀ ������ IOCP �ڵ��� �ݴ´�. �� ������ ��Ŀ�����尡 ��� ����ȴ�.
		CloseHandle(m_hWorkerIOCP);

		// ��Ŀ�����尡 ��� ����� ������ ����Ѵ�.
		WaitForMultipleObjects(m_IOCPThreadsCount, m_ThreadHandleArr, TRUE, INFINITE);
		for (DWORD i = 0; i < m_IOCPThreadsCount; i++)
			CloseHandle(m_ThreadHandleArr[i]);

		if (m_MySession.socket != INVALID_SOCKET)
		{
			closesocket(m_MySession.socket);
			m_MySession.socket = INVALID_SOCKET;
		}

		// ��Ÿ ���ҽ� ����
		delete[] m_ThreadHandleArr;
		delete[] m_ThreadIDArr;				

		// 
		WSACleanup();		
	}

	bool GGM::CLanClient::Connect()
	{
		// ���� ������ �������� �ʾҰų�, ������ ���ܼ� ���ҽ��� ������ �� �翬�� �ϴ� ���̶�� �ϳ� ��������
		if (m_MySession.socket == INVALID_SOCKET)
		{
			if (CreateSocket() == false)
				return false;
		}	

		// Connect�� �������� ó���ϰ� �� ����� Ȯ���ϱ� ���ؼ� select�� Ȱ���Ѵ�.			
		// Connect �ι� ȣ���ϴ� ����
		// ���࿡ �ι�° connect ȣ�� ���� �� Error ���� WSAEISCONN �� �ָ� Select ���ص� �Ǳ� �����̴�.
		SOCKET sock = m_MySession.socket;
		connect(sock, (SOCKADDR*)&m_ServerAddr, sizeof(m_ServerAddr));
		connect(sock, (SOCKADDR*)&m_ServerAddr, sizeof(m_ServerAddr));

		// ���� ������ ��� connect�� ȣ���ϸ� ���� �� �ִ� ������ ����� ���� ������ ����.
		// 1. ���� ȣ��� �ٷ� WSAEWOULDBLOCK 
		// 2. ���� ������ �Ϸ���� �ʾҴµ� �ٽ� connect ȣ���ϸ� WSAEALREADY
		// 3. �̹� ������ �Ϸ�� ���Ͽ� ���ؼ� �ٽ� connect ȣ���ϸ� WSAEISCONN
		// 4. �� �̿��� ���� WSAECONNREFUSED, WSAENETUNREACH, WSAETIMEDOUT 
		DWORD dwError = WSAGetLastError();

		if (dwError == WSAEWOULDBLOCK || dwError == WSAEALREADY)
		{
			// ����¿� ���� �ִ´�.
			FD_SET WriteSet;
			WriteSet.fd_count = 0;
			WriteSet.fd_array[0] = sock;
			WriteSet.fd_count++;

			// ���ܼ¿� ���� �ִ´�.
			FD_SET ExceptionSet;
			ExceptionSet.fd_count = 0;
			ExceptionSet.fd_array[0] = sock;
			ExceptionSet.fd_count++;
			timeval TimeVal = { 0, 500000 };

			// select�� timeval ����ŭ�� ������ ��ٸ���.
			int iResult = select(0, nullptr, &WriteSet, &ExceptionSet, &TimeVal);

			if (iResult == 0 || ExceptionSet.fd_count == 1)			
				return false;
		}		

		// ������ ���ῡ ������ ���ϰ� ��Ŀ�������� IOCP�� �����Ѵ�.
		HANDLE hRet = CreateIoCompletionPort((HANDLE)sock, m_hWorkerIOCP, 0, 0);

		if (hRet != m_hWorkerIOCP)
		{
			OnError(GetLastError(), _T("Associate Session Socket with Worker IOCP failed %d"));
			return false;
		}

		// ���� ���������� ���� �ʱ�ȭ
		InterlockedIncrement(&m_MySession.IoCount);
		m_MySession.SessionID++;
		m_MySession.SentPacketCount = 0;
		m_MySession.RecvQ.ClearRingBuffer();
		m_MySession.SendQ.ClearLockFreeQueue();
		m_MySession.IsSending = FALSE;
		m_MySession.IsReleased = FALSE;		
		
		// ���� RecvPost ���
		RecvPost(&m_MySession.RecvQ, &m_MySession.RecvOverlapped, &m_MySession.IoCount, sock);
		SessionReleaseLock();

		// ���� ���������� ���� �ؾ� ������ �ִٸ� ���⼭ ��
		OnConnect();

		return true;
	}

	bool CLanClient::WSAInit(TCHAR * ConnectIP, USHORT port, bool IsNoDelay)
	{
		// WSADATA ����ü �ʱ�ȭ
		WSADATA wsa;
		if (int SocketError = WSAStartup(MAKEWORD(2, 2), &wsa))
		{
			OnError(SocketError, _T("WSAStartup Failed"));
			return false;
		}

		ZeroMemory(&m_ServerAddr, sizeof(m_ServerAddr));
		m_ServerAddr.sin_port = htons(port);
		m_ServerAddr.sin_family = AF_INET;
		if (ConnectIP == nullptr)
		{
			OnError(GGM_ERROR::INVALID_SERVER_IP, _T("Invalid Server IP"));
			return false;
		}

		if (InetPton(AF_INET, ConnectIP, &(m_ServerAddr.sin_addr)) != TRUE)
		{
			OnError(WSAGetLastError(), _T("InetPton Failed"));
			return false;
		}

		m_IsNoDelay = IsNoDelay;
		
		if (CreateSocket() == false)
			return false;

		return true;
	}

	bool CLanClient::ConnectToLanServer(bool IsReconnect, int ReconnectDelay, DWORD ConcurrentThreads)
	{
		// ��Ŀ������� ������ IOCP ����
		m_hWorkerIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, ConcurrentThreads);

		if (m_hWorkerIOCP == NULL)
		{
			OnError(GetLastError(), _T("CreateIoCompletionPort Failed"));
			return false;
		}

		// �������� ������ ����� ������ ���ο� ������ ����
		m_IsReconnect = IsReconnect;
		m_ReconnectDelay = ReconnectDelay;

		// LanServer�� ���� ������ ������ ����õ�
		do
		{
			if (Connect() == true)
				break;

			Sleep(ReconnectDelay);

		} while (IsReconnect);

		return true;
	}

	bool CLanClient::ThreadInit(DWORD ConcurrentThreads, DWORD MaxThreads)
	{
		// ������ �������� �ڵ��� ������ �迭 
		m_ThreadHandleArr = new HANDLE[MaxThreads];

		if (m_ThreadHandleArr == nullptr)
		{
			OnError(GetLastError(), _T("MEM Alloc Failed"));
			return false;
		}

		// ������ �������� ���̵� ������ �迭
		m_ThreadIDArr = new DWORD[MaxThreads];

		if (m_ThreadIDArr == nullptr)
		{
			OnError(GetLastError(), _T("MEM Alloc Failed"));
			return false;
		}

		/////////////////// WorkerThread ���� //////////////////////////////

		// ����ڰ� ��û�� ������ ��Ŀ ������(���ý��� ���� �ƴ�) ����
		for (DWORD i = 0; i < MaxThreads; i++)
		{
			m_ThreadHandleArr[i] = (HANDLE)_beginthreadex(
				nullptr,
				0,
				CLanClient::WorkerThread, // ��Ŀ ������ �Լ� Ŭ���� ���ο� private static���� ����Ǿ�����
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

		/////////////////// WorkerThread ���� //////////////////////////////	

		return true;
	}

	bool GGM::CLanClient::Disconnect()
	{
		// �ܼ� ������ ���� �Ͱ� ������ ������ �ϴ� ���� �ٸ� ���̴�.
		// ���⼭�� shutdown�� ���� ���Ḹ ���´�.
		// shutdown�� ���� ������ ���� �� �ڿ������� I/O Count�� 0�� �ǵ��� �����Ͽ� ������ �������Ѵ�.
		do
		{
			// ���ǿ� ������ �õ��Ѵ�.
			// ���� ���ٿ� �����ϸ� �׳� ������.
			if (SessionAcquireLock(m_MySession.SessionID) == false)
				break;

			shutdown(m_MySession.socket, SD_BOTH);			

		} while (0);

		// ���ǿ� ������ �Ϸ������� �˸�
		SessionReleaseLock();
		return true;
	}

	bool GGM::CLanClient::SendPacket(CPacket * pPacket)
	{		
		bool Result;
		do
		{
			// ���ǿ� ������ �õ��Ѵ�.
			// ���� ���ٿ� �����ϸ� (�̹� �����尡 ������ ���̶��) �׳� ������.
			if (SessionAcquireLock(m_MySession.SessionID) == false)
			{
				Result = true;
				break;
			}
			
			// ��Ʈ��ũ ���������� ����ȭ ������ ���� �տ� ��Ʈ��ũ ������ ����� ���δ�.
			LAN_HEADER Header;			
			Header.size = pPacket->GetCurrentUsage() - LAN_HEADER_LENGTH;
			pPacket->EnqueueHeader((char*)&Header);			

			// SendQ�� ��Ʈ��ũ ��Ŷ�� �����͸� ��´�.	
			pPacket->AddRefCnt();
			Result = m_MySession.SendQ.Enqueue(pPacket);
			if (Result == false)
			{
				OnError(GGM_ERROR::BUFFER_WRITE_FAILED, _T("SESSION BUFFER WRITE FAILED"));
				break;
			}

			// SendFlag�� Ȯ���غ��� Send���� �ƴ϶�� PQCS�� WSASend ��û
			// �̷��� �����ν�, ���������� WSASend�� ȣ���Ͽ� ������Ʈ�� �������� ���� ���� �� �ִ�.
			// �׷��� ��Ŷ ���伺�� ������
			if (InterlockedOr(&m_MySession.IsSending, 0) == TRUE)
				break;

			PostQueuedCompletionStatus(m_hWorkerIOCP, 0, 0, &m_DummyOverlapped);

			// PQCS�� �ϰ����� IOCount�� �ٷ� ���� �ʰ� �Ϸ��������� ��´�.
			return true;

		} while (0);	

		// ���ǿ� ������ �Ϸ������� �˸�
		SessionReleaseLock();

		return true;
	}

	unsigned int __stdcall GGM::CLanClient::WorkerThread(LPVOID Param)
	{
		// Worker ������ �Լ��� static�̱� ������ ��������� �����ϰų� ����Լ��� ȣ���ϱ� ���� 
		// this �����͸� ���ڷ� �޾ƿ´�.
		CLanClient *pThis = (CLanClient*)Param;

		// IOCP �ڵ�
		HANDLE hIOCP = pThis->m_hWorkerIOCP;

		// LanClient�� ��Ŀ�����尡 �����ϴ� ���� ����ü�� �ּҴ� �Ź� �����ϹǷ� �ʿ��� ������ �ּҸ� ��� ���÷� �޾Ƶд�.
		LONG							 *pIoCount = &(pThis->m_MySession.IoCount);
		LONG							 *pIsSending = &(pThis->m_MySession.IsSending);
		OVERLAPPED						 *pRecvOverlapped = &(pThis->m_MySession.RecvOverlapped);		
		OVERLAPPED						 *pSendOverlapped = &(pThis->m_MySession.SendOverlapped);
		OVERLAPPED                       *pDummyOverlapped = &pThis->m_DummyOverlapped;
		ULONG							 *pSentPacketCount = &(pThis->m_MySession.SentPacketCount);
		CPacket					        **PacketArray = pThis->m_MySession.PacketArray;
		CRingBuffer                      *pRecvQ = &(pThis->m_MySession.RecvQ);
		CLockFreeQueue<CPacket*>         *pSendQ = &(pThis->m_MySession.SendQ);
		SOCKET                           *pSocket = &(pThis->m_MySession.socket);

		// ���� ���鼭 �۾� �Ѵ�.
		while (true)
		{
			// �Ϸ����� �� ����� ���� �������� �ʱ�ȭ �Ѵ�.
			DWORD BytesTransferred; // �ۼ��� ����Ʈ ��
			Session *pSession; // ���ø��� Ű�� ������ ���� ����ü ������
			OVERLAPPED *pOverlapped; // Overlapped ����ü �������� ������		

			// GQCS�� ȣ���ؼ� �ϰ��� ���⸦ ����Ѵ�.			
			bool bOk = GetQueuedCompletionStatus(hIOCP, &BytesTransferred, (PULONG_PTR)&pSession, (LPOVERLAPPED*)&pOverlapped, INFINITE);

			// ��Ŀ ������ �� ��������.
			//pThis->OnWorkerThreadBegin();

			// SendPacket�Լ� ���ο��� PQCS�� SendPost��û �ߴٸ� �� ������ ó�� 
			if (pOverlapped == pDummyOverlapped)
			{
				if (pSendQ->size() > 0)
					pThis->SendPost(
						pIsSending, 
						pSendQ, 
						pSendOverlapped, 
						PacketArray, 
						pSentPacketCount, 
						pIoCount, 
						*pSocket
					);

				// ���� ����ȭ�� ���� IOCount�÷ȴ� ���� �� ������ ����
				pThis->SessionReleaseLock();
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
			if (BytesTransferred == 0)
			{
				//0�� ������ ���ʿ��� FIN�� ���°ų� ������ �� ����̴�.
				//I/O Count�� 0���� �����ؼ� ������ �ڿ������� ������ �� �ֵ��� �Ѵ�.				
				pThis->SessionReleaseLock();
				continue;
			}

			// �Ϸ�� I/O�� RECV���� SEND���� �Ǵ��Ѵ�.
			// �������� ����ü ������ ��
			if (pOverlapped == pRecvOverlapped)
			{
				// �Ϸ������� ���� ����Ʈ ����ŭ Rear�� �Ű��ش�.				
				pRecvQ->RelocateWrite(BytesTransferred);

				// ��Ʈ��ũ Ŭ���� ������ �ϼ��� ��Ŷ�� �ִ��� Ȯ��
				// CheckPacket ���ο��� ��� �Ϸ�� ��Ŷ�� OnRecv�� ����
				pThis->CheckPacket(pRecvQ);

				// RecvPost ȣ���ؼ� WSARecv ����
				pThis->RecvPost(pRecvQ, pRecvOverlapped, pIoCount, *pSocket);
			}
			else
			{
				// �Ϸ�� I/O�� SEND��� OnSend ȣ��
				//pThis->OnSend(pSession->SessionID, BytesTransferred);

				// OnSend�Ŀ��� �۽ſϷ� �۾��� ���־�� ��	

				// 1. �񵿱� I/O�� ��û�� ������ �� ����ȭ ���۸� �������ش�.
				ULONG PacketCount = *pSentPacketCount;				
				for (ULONG i = 0; i < PacketCount; i++)
				{
					CPacket::Free(PacketArray[i]);
				}

				*pSentPacketCount = 0;

				// 2. ��û�� �񵿱� Send�� �Ϸ�Ǿ����Ƿ� ���� �ش� ���ǿ� ���� Send�� �� �ְ� �Ǿ���			
				*pIsSending = FALSE;

				// 3. ���� ������ ���� ���� �ִ��� Ȯ���ؼ� �ִٸ� ����
				if (pSendQ->size() > 0)
					pThis->SendPost(pIsSending, pSendQ, pSendOverlapped, PacketArray, pSentPacketCount, pIoCount, *pSocket);
			}

			// I/O Count�� �Ϲ������� �̰����� ���ҽ�Ų��. ( ������ ���� ���� ������ �߻��� ������ ���� ��Ŵ )
			// ���Ұ�� I/O COUNT�� 0�� �Ǹ� ������ �������Ѵ�. 				
			// 0���� ���� �����尡 �� ������ ������ �ؾ� �Ѵ�.			
			pThis->SessionReleaseLock();

			// ��Ŀ ������ �� ������
			//pThis->OnWorkerThreadEnd();
		}

		return 0;
	}

	void GGM::CLanClient::CheckPacket(CRingBuffer *pRecvQ)
	{
		// ���� RECV ������ ��뷮 üũ
		int CurrentUsage = pRecvQ->GetCurrentUsage();
		int Result;

		CPacket Packet(0);
		char *pPacketbuf = (char*)Packet.GetBufferPtr();
		LAN_HEADER Header;

		// �����ۿ� �ּ��� ������� �̻��� �ִ��� ����Ȯ���Ѵ�.
		// ������� �̻��� ������ �������鼭 �����ϴ� �ϼ��� ��Ŷ �� �̾Ƽ� OnRecv�� ����
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
			OnRecv(&Packet);

			// ���� �������� ��� ����� ��Ŷ �����ŭ ����
			CurrentUsage -= Header.size;

			// ����ȭ ���۸� ��Ȱ���ϱ� ���� Rear�� Front �ʱ�ȭ
			Packet.InitBuffer();
		}
	}

	bool GGM::CLanClient::SendPost(
		LONG                           *pIsSending,
		CLockFreeQueue<CPacket*> *pSendQ,
		LPOVERLAPPED                    pOverlapped,
		CPacket                 **PacketArray,
		ULONG                          *pSentPacketCount,
		LONG				           *pIoCount,
		SOCKET                          sock
	)
	{
		///////////////////////////////////////////////////////////////////
		// 0. Send ���� �÷��� üũ 
		///////////////////////////////////////////////////////////////////
		while (InterlockedExchange(pIsSending, TRUE) == FALSE)
		{
			// ���� SendQ ��뷮�� Ȯ���ؼ� ���� ���� �ִ��� �ٽ� �ѹ� Ȯ���غ���.
			// ���� ���� �ִ��� �˰� ���Դµ� ���� ���� ���� ���� �ִ�.
			ULONG CurrentUsage = pSendQ->size();

			// �������� ���ٸ� Send �÷��� �ٽ� �ٲپ� �ְ� ����
			if (CurrentUsage == 0)
			{
				// ���� �� �κп��� �÷��׸� �ٲٱ����� ���ؽ�Ʈ ����Ī�� �Ͼ�ٸ� ������ �ȴ�.
				// �ٸ� �����尡 ���� ���� �־��µ� �÷��װ� ����־ �� ������ ���� �ִ�. 

				//InterlockedBitTestAndReset(pIsSending, 0);

				*pIsSending = FALSE;

				// ���� ������ ������ �߻��ߴٸ� �� �ʿ��� ������ �Ѵ�.
				// �׷��� ������ �ƹ��� ������ �ʴ� ��Ȳ�� �߻��Ѵ�.
				if (pSendQ->size() > 0)
					continue;

				return true;
			}

			///////////////////////////////////////////////////////////////////
			// 1. WSASend�� Overlapped ����ü �ʱ�ȭ
			///////////////////////////////////////////////////////////////////
			ZeroMemory(pOverlapped, sizeof(OVERLAPPED));

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

			// SendQ���� �ѹ��� �۽� ������ ��Ŷ�� �ִ� ������ŭ ����ȭ ������ �����͸� Dequeue�Ѵ�.		
			// ���Ǻ��� ������ �ִ� ����ȭ ���� ������ �迭�� �װ��� �����Ѵ�.
			// Peek�� �� �� �Ϸ����� ���Ŀ� Dequeue�� �ϸ� memcpy�� �߰������� �Ͼ�Ƿ� �޸𸮸� ����ؼ� Ƚ���� �ٿ���.			

			// ���� ť�� ����ִ� �������� ������ �ִ�ġ�� �ʰ��ߴٸ� �������ش�.
			if (CurrentUsage > MAX_PACKET_COUNT)
				CurrentUsage = MAX_PACKET_COUNT;

			for (ULONG i = 0; i < CurrentUsage; i++)
				pSendQ->Dequeue(&PacketArray[i]);

			// ���� ��Ŷ�� ������ ����ߴٰ� ���߿� �Ϸ������� ���� �޸𸮸� �����ؾ� �Ѵ�.			
			DWORD PacketCount = *pSentPacketCount = CurrentUsage;

			// ��Ŷ ������ŭ �ݺ��� ���鼭 ���� ����ȭ ���� ���� ��Ŷ ���� �����͸� WSABUF����ü�� ����
			for (DWORD i = 0; i < PacketCount; i++)
			{
				wsabuf[i].buf = (char*)PacketArray[i]->GetBufferPtr();
				wsabuf[i].len = PacketArray[i]->GetCurrentUsage();
			}

			///////////////////////////////////////////////////////////////////
			// 3. WSASend ����ϱ� ���� I/O ī��Ʈ ����
			///////////////////////////////////////////////////////////////////

			InterlockedIncrement(pIoCount);

			///////////////////////////////////////////////////////////////////
			// 4. WSASend ��û
			///////////////////////////////////////////////////////////////////
			DWORD bytesSent = 0;
			int Result = WSASend(sock, wsabuf, PacketCount, &bytesSent, 0, pOverlapped, nullptr);

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
					SessionReleaseLock();
				}
			}

			return true;
		}

		return true;
	}

	bool GGM::CLanClient::RecvPost(
		CRingBuffer  *pRecvQ,
		LPOVERLAPPED  pOverlapped,
		LONG         *pIoCount,
		SOCKET        sock
	)
	{
		///////////////////////////////////////////////////////////////////
		// 0. I/O ��û�� �ϱ� ���� ���� �������� ���������� �˻��Ѵ�.
		///////////////////////////////////////////////////////////////////		
		int CurrentSpare = pRecvQ->GetCurrentSpare();

		///////////////////////////////////////////////////////////////////
		// 1. WSARecv�� Overlapped ����ü �ʱ�ȭ
		///////////////////////////////////////////////////////////////////

		ZeroMemory(pOverlapped, sizeof(OVERLAPPED));

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
		InterlockedIncrement(pIoCount);

		///////////////////////////////////////////////////////////////////
		// 4. I/O ��û
		///////////////////////////////////////////////////////////////////
		DWORD BytesRead = 0;
		DWORD Flags = 0;
		int Result = WSARecv(sock, wsabuf, RecvBufCount, &BytesRead, &Flags, pOverlapped, nullptr);

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
				SessionReleaseLock();
			}
		}

		return true;

	}

	void GGM::CLanClient::ReleaseSession()
	{
		// ReleaseFlag �� TRUE�� �Ǹ� � �����嵵 �� ���ǿ� �����ϰų� ������ �õ��� �ؼ��� �ȵȴ�.
		// IoCount�� ReleaseFlag�� 4����Ʈ�� ���޾Ƽ� ��ġ�� �����Ƿ� �Ʒ��� ���� ���Ͷ��Լ� ȣ���Ѵ�.
		// IoCount(LONG) == 0 ReleaseFlag(LONG) == 0 >> ���Ͷ� ������ >>IoCount(LONG) == 0 ReleaseFlag(LONG) == 1
		Session *pSession = &m_MySession;
		if (InterlockedCompareExchange64((volatile LONG64*)&(pSession->IoCount), 0x0000000100000000, FALSE) != FALSE)
			return;

		ULONGLONG SessionID = pSession->SessionID++;		

		// ���� ����ȭ ���۸� �����Ҵ� �� Send�ϴ� ���߿� ���������� Release�� �ؾ��Ѵٸ�
		// �޸𸮸� ������ �־�� �Ѵ�.
		ULONG PacketCount = pSession->SentPacketCount;
		CPacket **PacketArray = pSession->PacketArray;	
		for (WORD i = 0; i < PacketCount; i++)
			CPacket::Free(PacketArray[i]);		

		// ������ ���� ���� ��� ����	
		ULONG GarbagePacketCount = pSession->SendQ.size();		
		for (ULONG i = 0; i < GarbagePacketCount; i++)
		{
			CPacket *pGarbagePacket;
			pSession->SendQ.Dequeue(&pGarbagePacket);
			CPacket::Free(pGarbagePacket);
		}		
		
		// ���� ���ҽ� ����
		closesocket(pSession->socket);
		pSession->socket = INVALID_SOCKET;	

		// �ش� ���ǿ� ���� ��� ���ҽ��� �����Ǿ����Ƿ� �̺�Ʈ �ڵ鸵 �Լ� ȣ��
		OnDisconnect();

		// ���� ������ �� ������ �ɼ��� �����ִٸ� ������ �õ�
		while (m_IsReconnect)
		{
			if (Connect() == true)
			{
				InterlockedIncrement(&ReconnectCount);
				break;
			}				

			Sleep(m_ReconnectDelay);
		}
	}

	bool GGM::CLanClient::SessionAcquireLock(ULONGLONG LocalSessionID)
	{
		// �� �Լ��� ȣ���ߴٴ� ���� ���� �������� �ش� ������ ����ϰڴٴ� �ǹ��̴�.				
		// �� ������ ���� � ���������� ������ ���ǿ� �����Ѵٴ� �ǹ̷�, I/O Count�� ������Ų��.
		// I/O Count�� ���� �������״ٸ� ������ �Ǵ� ���� ���� �� �ִ�.
		// ���� I/O Count�� �������״µ� 1�̶�� �� �̻��� ���� ������ ���ǹ��ϴ�.
		ULONGLONG RetIOCount = InterlockedIncrement(&(m_MySession.IoCount));

		if (RetIOCount == 1 || m_MySession.IsReleased == TRUE || m_MySession.SessionID != LocalSessionID)
			return false;

		return true;
	}

	void GGM::CLanClient::SessionReleaseLock()
	{		
		// ���ǿ� ���� ������ ��� �������Ƿ� ������ �ø� I/O ī��Ʈ�� ���� ��Ų��.	
		LONG IoCount = InterlockedDecrement(&(m_MySession.IoCount));
		if (IoCount <= 0)
		{
			if (IoCount < 0)
			{
				OnError(GGM_ERROR::NEGATIVE_IO_COUNT, _T("[CNetServer] SessionReleaseLock() IoCount Negative"));
				CCrashDump::ForceCrash();
			}

			ReleaseSession();
		}
	}

	bool CLanClient::CreateSocket()
	{
		// ���� ����
		m_MySession.socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

		if (m_MySession.socket == INVALID_SOCKET)
		{
			OnError(WSAGetLastError(), _T("Connect socket Creation Failed"));
			return false;
		}

		// connect �� �� ���� ������ �̿��Ѵ�.
		u_long NonBlockOn = 1;
		int iResult = ioctlsocket(m_MySession.socket, FIONBIO, &NonBlockOn);

		if (iResult == SOCKET_ERROR)
		{
			OnError(WSAGetLastError(), _T("ioctlsocket Failed"));
			return false;
		}

		// TCP_NODELAY �÷��� Ȯ���ؼ� ���ֱ�
		if (m_IsNoDelay == true)
		{
			int iResult = setsockopt(
				m_MySession.socket,
				IPPROTO_TCP,
				TCP_NODELAY,
				(const char*)&m_IsNoDelay,
				sizeof(m_IsNoDelay)
			);

			if (iResult == SOCKET_ERROR)
			{
				OnError(WSAGetLastError(), _T("setsockopt [TCP_NODELAY] Failed"));
				return false;
			}
		}

		// LINGER�ɼ� ����
		LINGER linger = { 1,0 };
		int Result = setsockopt(m_MySession.socket, SOL_SOCKET, SO_LINGER, (const char*)&linger, sizeof(linger));

		if (Result == SOCKET_ERROR)
		{
			OnError(WSAGetLastError(), _T("setsockopt [SO_LINGER] Failed"));
			return false;
		}

		// WSASend�� �׻� �񵿱� I/O�� ����� ���� ���� �۽� ������ ����� 0���� �����.
	/*	int BufSize = 0;
		Result = setsockopt(m_MySession.socket, SOL_SOCKET, SO_SNDBUF, (const char*)&BufSize, sizeof(BufSize));

		if (Result == SOCKET_ERROR)
		{
			OnError(WSAGetLastError(), _T("setsockopt [SO_SNDBUF] Failed"));
			return false;
		}*/

		return true;
	}
}
