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
		// 재접속 off
		m_IsReconnect = false;

		// 워커 스레드 IOCP 핸들을 닫는다. 이 시점에 워커스레드가 모두 종료된다.
		CloseHandle(m_hWorkerIOCP);

		// 워커스레드가 모두 종료될 때까지 대기한다.
		WaitForMultipleObjects(m_IOCPThreadsCount, m_ThreadHandleArr, TRUE, INFINITE);
		for (DWORD i = 0; i < m_IOCPThreadsCount; i++)
			CloseHandle(m_ThreadHandleArr[i]);

		if (m_MySession.socket != INVALID_SOCKET)
		{
			closesocket(m_MySession.socket);
			m_MySession.socket = INVALID_SOCKET;
		}

		// 기타 리소스 정리
		delete[] m_ThreadHandleArr;
		delete[] m_ThreadIDArr;				

		// 
		WSACleanup();		
	}

	bool GGM::CLanClient::Connect()
	{
		// 아직 소켓이 생성되지 않았거나, 이전에 끊겨서 리소스가 정리된 후 재연결 하는 것이라면 하나 생성해줌
		if (m_MySession.socket == INVALID_SOCKET)
		{
			if (CreateSocket() == false)
				return false;
		}	

		// Connect를 논블락으로 처리하고 그 결과를 확인하기 위해서 select를 활용한다.			
		// Connect 두번 호출하는 이유
		// 만약에 두번째 connect 호출 했을 때 Error 값이 WSAEISCONN 떠 주면 Select 안해도 되기 때문이다.
		SOCKET sock = m_MySession.socket;
		connect(sock, (SOCKADDR*)&m_ServerAddr, sizeof(m_ServerAddr));
		connect(sock, (SOCKADDR*)&m_ServerAddr, sizeof(m_ServerAddr));

		// 논블락 소켓일 경우 connect를 호출하면 나올 수 있는 에러의 경우의 수는 다음과 같다.
		// 1. 최초 호출시 바로 WSAEWOULDBLOCK 
		// 2. 아직 연결이 완료되지 않았는데 다시 connect 호출하면 WSAEALREADY
		// 3. 이미 연결이 완료된 소켓에 대해서 다시 connect 호출하면 WSAEISCONN
		// 4. 그 이외의 경우는 WSAECONNREFUSED, WSAENETUNREACH, WSAETIMEDOUT 
		DWORD dwError = WSAGetLastError();

		if (dwError == WSAEWOULDBLOCK || dwError == WSAEALREADY)
		{
			// 쓰기셋에 소켓 넣는다.
			FD_SET WriteSet;
			WriteSet.fd_count = 0;
			WriteSet.fd_array[0] = sock;
			WriteSet.fd_count++;

			// 예외셋에 소켓 넣는다.
			FD_SET ExceptionSet;
			ExceptionSet.fd_count = 0;
			ExceptionSet.fd_array[0] = sock;
			ExceptionSet.fd_count++;
			timeval TimeVal = { 0, 500000 };

			// select로 timeval 값만큼만 연결을 기다린다.
			int iResult = select(0, nullptr, &WriteSet, &ExceptionSet, &TimeVal);

			if (iResult == 0 || ExceptionSet.fd_count == 1)			
				return false;
		}		

		// 서버와 연결에 성공한 소켓과 워커스레드의 IOCP를 연결한다.
		HANDLE hRet = CreateIoCompletionPort((HANDLE)sock, m_hWorkerIOCP, 0, 0);

		if (hRet != m_hWorkerIOCP)
		{
			OnError(GetLastError(), _T("Associate Session Socket with Worker IOCP failed %d"));
			return false;
		}

		// 연결 성공했으면 세션 초기화
		InterlockedIncrement(&m_MySession.IoCount);
		m_MySession.SessionID++;
		m_MySession.SentPacketCount = 0;
		m_MySession.RecvQ.ClearRingBuffer();
		m_MySession.SendQ.ClearLockFreeQueue();
		m_MySession.IsSending = FALSE;
		m_MySession.IsReleased = FALSE;		
		
		// 최초 RecvPost 등록
		RecvPost(&m_MySession.RecvQ, &m_MySession.RecvOverlapped, &m_MySession.IoCount, sock);
		SessionReleaseLock();

		// 연결 성공했을때 무언가 해야 할일이 있다면 여기서 함
		OnConnect();

		return true;
	}

	bool CLanClient::WSAInit(TCHAR * ConnectIP, USHORT port, bool IsNoDelay)
	{
		// WSADATA 구조체 초기화
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
		// 워커스레드와 연동될 IOCP 생성
		m_hWorkerIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, ConcurrentThreads);

		if (m_hWorkerIOCP == NULL)
		{
			OnError(GetLastError(), _T("CreateIoCompletionPort Failed"));
			return false;
		}

		// 랜서버와 연결이 끊길시 재접속 여부와 딜레이 설정
		m_IsReconnect = IsReconnect;
		m_ReconnectDelay = ReconnectDelay;

		// LanServer와 연결 성공할 때까지 연결시도
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
		// 생성된 스레드의 핸들을 저장할 배열 
		m_ThreadHandleArr = new HANDLE[MaxThreads];

		if (m_ThreadHandleArr == nullptr)
		{
			OnError(GetLastError(), _T("MEM Alloc Failed"));
			return false;
		}

		// 생성된 스레드의 아이디를 저장할 배열
		m_ThreadIDArr = new DWORD[MaxThreads];

		if (m_ThreadIDArr == nullptr)
		{
			OnError(GetLastError(), _T("MEM Alloc Failed"));
			return false;
		}

		/////////////////// WorkerThread 생성 //////////////////////////////

		// 사용자가 요청한 개수의 워커 스레드(동시실행 개수 아님) 생성
		for (DWORD i = 0; i < MaxThreads; i++)
		{
			m_ThreadHandleArr[i] = (HANDLE)_beginthreadex(
				nullptr,
				0,
				CLanClient::WorkerThread, // 워커 스레드 함수 클래스 내부에 private static으로 선언되어있음
				this, // 워커스레드 함수가 static으로 선언되어있으므로 다른 멤버 변수나 함수에 접근하기 위해서 해당 인스턴스의 this 포인터가 필요함
				0,
				(unsigned int*)&m_ThreadIDArr[i]
			);

			if (m_ThreadHandleArr[i] == NULL)
			{
				OnError(GetLastError(), _T("_beginthreadex Failed"));
				return false;
			}
		}

		/////////////////// WorkerThread 생성 //////////////////////////////	

		return true;
	}

	bool GGM::CLanClient::Disconnect()
	{
		// 단순 연결을 끊는 것과 세션을 릴리즈 하는 것은 다른 것이다.
		// 여기서는 shutdown을 통해 연결만 끊는다.
		// shutdown을 통해 연결이 끊은 후 자연스럽게 I/O Count가 0이 되도록 유도하여 세션을 릴리즈한다.
		do
		{
			// 세션에 접근을 시도한다.
			// 세션 접근에 실패하면 그냥 나간다.
			if (SessionAcquireLock(m_MySession.SessionID) == false)
				break;

			shutdown(m_MySession.socket, SD_BOTH);			

		} while (0);

		// 세션에 접근을 완료했음을 알림
		SessionReleaseLock();
		return true;
	}

	bool GGM::CLanClient::SendPacket(CPacket * pPacket)
	{		
		bool Result;
		do
		{
			// 세션에 접근을 시도한다.
			// 세션 접근에 실패하면 (이미 스레드가 릴리즈 중이라면) 그냥 나간다.
			if (SessionAcquireLock(m_MySession.SessionID) == false)
			{
				Result = true;
				break;
			}
			
			// 네트워크 레벨에서는 직렬화 버퍼의 제일 앞에 네트워크 계층의 헤더를 붙인다.
			LAN_HEADER Header;			
			Header.size = pPacket->GetCurrentUsage() - LAN_HEADER_LENGTH;
			pPacket->EnqueueHeader((char*)&Header);			

			// SendQ에 네트워크 패킷의 포인터를 담는다.	
			pPacket->AddRefCnt();
			Result = m_MySession.SendQ.Enqueue(pPacket);
			if (Result == false)
			{
				OnError(GGM_ERROR::BUFFER_WRITE_FAILED, _T("SESSION BUFFER WRITE FAILED"));
				break;
			}

			// SendFlag를 확인해보고 Send중이 아니라면 PQCS로 WSASend 요청
			// 이렇게 함으로써, 컨텐츠에서 WSASend를 호출하여 업데이트가 느려지는 것을 막을 수 있다.
			// 그러나 패킷 응답성은 떨어짐
			if (InterlockedOr(&m_MySession.IsSending, 0) == TRUE)
				break;

			PostQueuedCompletionStatus(m_hWorkerIOCP, 0, 0, &m_DummyOverlapped);

			// PQCS를 하고나서는 IOCount를 바로 깍지 않고 완료통지에서 깎는다.
			return true;

		} while (0);	

		// 세션에 접근을 완료했음을 알림
		SessionReleaseLock();

		return true;
	}

	unsigned int __stdcall GGM::CLanClient::WorkerThread(LPVOID Param)
	{
		// Worker 스레드 함수가 static이기 때문에 멤버변수에 접근하거나 멤버함수를 호출하기 위해 
		// this 포인터를 인자로 받아온다.
		CLanClient *pThis = (CLanClient*)Param;

		// IOCP 핸들
		HANDLE hIOCP = pThis->m_hWorkerIOCP;

		// LanClient의 워커스레드가 접근하는 세션 구조체의 주소는 매번 동일하므로 필요한 변수의 주소를 모두 로컬로 받아둔다.
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

		// 루프 돌면서 작업 한다.
		while (true)
		{
			// 완료통지 후 결과를 받을 변수들을 초기화 한다.
			DWORD BytesTransferred; // 송수신 바이트 수
			Session *pSession; // 컴플리션 키로 전달한 세션 구조체 포인터
			OVERLAPPED *pOverlapped; // Overlapped 구조체 포인터의 포인터		

			// GQCS를 호출해서 일감이 오기를 대기한다.			
			bool bOk = GetQueuedCompletionStatus(hIOCP, &BytesTransferred, (PULONG_PTR)&pSession, (LPOVERLAPPED*)&pOverlapped, INFINITE);

			// 워커 스레드 일 시작했음.
			//pThis->OnWorkerThreadBegin();

			// SendPacket함수 내부에서 PQCS로 SendPost요청 했다면 이 곳에서 처리 
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

				// 세션 동기화를 위해 IOCount올렸던 것을 이 곳에서 차감
				pThis->SessionReleaseLock();
				continue;
			}

			// Overlapped 구조체가 nullptr이면 문제가 발생한 것이다.
			if (pOverlapped == nullptr)
			{
				DWORD Error = WSAGetLastError();
				if (bOk == FALSE && Error == ERROR_ABANDONED_WAIT_0)
				{
					// 오버랩드 구조체가 널인데 에러코드가 ERROR_ABANDONED_WAIT_0라면 외부에서 현재 사용중인 IOCP 핸들을 닫은 것이다.
					// 스레드 종료처리 로직이므로 종료 해주면 된다.			

					// 테스트용 콘솔 출력 나중에 지워야 함
					_tprintf_s(_T("[SERVER] SERVER OFF! WORKER THREAD EXIT [ID %d]\n"), GetCurrentThreadId());

					return 0;
				}

				pThis->OnError(WSAGetLastError(), _T("GQCS FAILED / OVERLAPPED NULL"));
				continue;
			}

			// pOverlapped가 nullptr가 아니라면 송수신바이트 수가 0인지 검사해야 한다. 
			// IOCP에서 송수신 바이트수는 (0 이나 요청한 바이트 수) 이외의 다른 값이 나올 수 없다. 			
			// 따라서 송수신 바이트 수가 0이 아니라면 요청한 비동기 I/O가 정상적으로 처리되었음을 의미한다.			
			if (BytesTransferred == 0)
			{
				//0이 나오면 저쪽에서 FIN을 보냈거나 에러가 난 경우이다.
				//I/O Count를 0으로 유도해서 세션이 자연스럽게 정리될 수 있도록 한다.				
				pThis->SessionReleaseLock();
				continue;
			}

			// 완료된 I/O가 RECV인지 SEND인지 판단한다.
			// 오버랩드 구조체 포인터 비교
			if (pOverlapped == pRecvOverlapped)
			{
				// 완료통지로 받은 바이트 수만큼 Rear를 옮겨준다.				
				pRecvQ->RelocateWrite(BytesTransferred);

				// 네트워크 클래스 계층의 완성된 패킷이 있는지 확인
				// CheckPacket 내부에서 모든 완료된 패킷을 OnRecv로 전달
				pThis->CheckPacket(pRecvQ);

				// RecvPost 호출해서 WSARecv 재등록
				pThis->RecvPost(pRecvQ, pRecvOverlapped, pIoCount, *pSocket);
			}
			else
			{
				// 완료된 I/O가 SEND라면 OnSend 호출
				//pThis->OnSend(pSession->SessionID, BytesTransferred);

				// OnSend후에는 송신완료 작업을 해주어야 함	

				// 1. 비동기 I/O로 요청시 보관해 둔 직렬화 버퍼를 정리해준다.
				ULONG PacketCount = *pSentPacketCount;				
				for (ULONG i = 0; i < PacketCount; i++)
				{
					CPacket::Free(PacketArray[i]);
				}

				*pSentPacketCount = 0;

				// 2. 요청한 비동기 Send가 완료되었으므로 이제 해당 세션에 대해 Send할 수 있게 되었음			
				*pIsSending = FALSE;

				// 3. 아직 보내지 못한 것이 있는지 확인해서 있다면 보냄
				if (pSendQ->size() > 0)
					pThis->SendPost(pIsSending, pSendQ, pSendOverlapped, PacketArray, pSentPacketCount, pIoCount, *pSocket);
			}

			// I/O Count는 일반적으로 이곳에서 감소시킨다. ( 오류가 났을 경우는 오류가 발생한 곳에서 감소 시킴 )
			// 감소결과 I/O COUNT가 0이 되면 세션을 릴리즈한다. 				
			// 0으로 만든 스레드가 그 세션을 릴리즈 해야 한다.			
			pThis->SessionReleaseLock();

			// 워커 스레드 일 끝났음
			//pThis->OnWorkerThreadEnd();
		}

		return 0;
	}

	void GGM::CLanClient::CheckPacket(CRingBuffer *pRecvQ)
	{
		// 현재 RECV 링버퍼 사용량 체크
		int CurrentUsage = pRecvQ->GetCurrentUsage();
		int Result;

		CPacket Packet(0);
		char *pPacketbuf = (char*)Packet.GetBufferPtr();
		LAN_HEADER Header;

		// 링버퍼에 최소한 헤더길이 이상이 있는지 먼저확인한다.
		// 헤더길이 이상이 있으면 루프돌면서 존재하는 완성된 패킷 다 뽑아서 OnRecv에 전달
		while (CurrentUsage >= LAN_HEADER_LENGTH)
		{
			char *pPacket = pPacketbuf;

			// 해당 세션 링버퍼에서 헤더만큼 PEEK
			Result = pRecvQ->Peek((char*)&Header, LAN_HEADER_LENGTH);

			if (Result != LAN_HEADER_LENGTH)
			{
				// 이것은 서버가 더 이상 진행하면 안되는 상황 덤프를 남겨서 문제를 확인하자
				CCrashDump::ForceCrash();
				break;
			}

			CurrentUsage -= LAN_HEADER_LENGTH;

			// 현재 링버퍼에 완성된 패킷만큼 없으면 반복문 탈출 			
			if (CurrentUsage < Header.size)
				break;

			// 완성된 패킷이 있다면 HEADER는 RecvQ에서 지워준다.
			pRecvQ->EraseData(LAN_HEADER_LENGTH);

			// 완성된 패킷을 뽑아서 직렬화 버퍼에 담는다.
			Result = pRecvQ->Dequeue(pPacket, Header.size);			

			if (Result != Header.size)
			{
				// 이것은 서버가 더 이상 진행하면 안되는 상황 덤프를 남겨서 문제를 확인하자
				CCrashDump::ForceCrash();
				break;
			}

			Packet.RelocateWrite(Header.size);

			// 완성된 패킷이 존재하므로 OnRecv에 완성 패킷을 전달한다.
			OnRecv(&Packet);

			// 현재 링버퍼의 사용 사이즈를 패킷 사이즈만큼 감소
			CurrentUsage -= Header.size;

			// 직렬화 버퍼를 재활용하기 위해 Rear와 Front 초기화
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
		// 0. Send 가능 플래그 체크 
		///////////////////////////////////////////////////////////////////
		while (InterlockedExchange(pIsSending, TRUE) == FALSE)
		{
			// 현재 SendQ 사용량을 확인해서 보낼 것이 있는지 다시 한번 확인해본다.
			// 보낼 것이 있는줄 알고 들어왔는데 보낼 것이 없을 수도 있다.
			ULONG CurrentUsage = pSendQ->size();

			// 보낼것이 없다면 Send 플래그 다시 바꾸어 주고 리턴
			if (CurrentUsage == 0)
			{
				// 만약 이 부분에서 플래그를 바꾸기전에 컨텍스트 스위칭이 일어난다면 문제가 된다.
				// 다른 스레드가 보낼 것이 있었는데 플래그가 잠겨있어서 못 보냈을 수도 있다. 

				//InterlockedBitTestAndReset(pIsSending, 0);

				*pIsSending = FALSE;

				// 만약 위에서 문제가 발생했다면 이 쪽에서 보내야 한다.
				// 그렇지 않으면 아무도 보내지 않는 상황이 발생한다.
				if (pSendQ->size() > 0)
					continue;

				return true;
			}

			///////////////////////////////////////////////////////////////////
			// 1. WSASend용 Overlapped 구조체 초기화
			///////////////////////////////////////////////////////////////////
			ZeroMemory(pOverlapped, sizeof(OVERLAPPED));

			///////////////////////////////////////////////////////////////////
			//2. WSABUF 구조체 초기화
			///////////////////////////////////////////////////////////////////

			// SendQ에는 패킷단위의 직렬화버퍼 포인터가 저장되어 있다.
			// 포인터를 SendQ에서 디큐해서 해당 직렬화 버퍼가 가지고 있는 패킷의 포인터를 WSABUF에 전달하고, 모아서 보낸다.
			// 혼란방지용 [SendQ : 직렬화 버퍼의 포인터 (CPacket*)] [ WSABUF : 직렬화버퍼내의 버퍼포인터 (char*)]
			// WSABUF에 한번에 모아 보낼 패킷의 개수는 정적으로 100 ~ 500개 사이로 정한다.
			// WSABUF의 개수를 너무 많이 잡으면 시스템이 해당 메모리를 락 걸기 때문에 메모리가 이슈가 발생할 수 있다. 
			// 직렬화버퍼의 포인터와 송신한 개수를 보관했다가 완료통지가 뜨면 메모리 해제 

			// 직렬화 버퍼내의 패킷 메모리 포인터(char*)를 담을 WSABUF 구조체 배열
			WSABUF        wsabuf[MAX_PACKET_COUNT];			

			// SendQ에서 한번에 송신 가능한 패킷외 최대 개수만큼 직렬화 버퍼의 포인터를 Dequeue한다.		
			// 세션별로 가지고 있는 직렬화 버퍼 포인터 배열에 그것을 복사한다.
			// Peek을 한 후 완료통지 이후에 Dequeue를 하면 memcpy가 추가적으로 일어나므로 메모리를 희생해서 횟수를 줄였다.			

			// 현재 큐에 들어있는 포인터의 개수가 최대치를 초과했다면 보정해준다.
			if (CurrentUsage > MAX_PACKET_COUNT)
				CurrentUsage = MAX_PACKET_COUNT;

			for (ULONG i = 0; i < CurrentUsage; i++)
				pSendQ->Dequeue(&PacketArray[i]);

			// 보낸 패킷의 개수를 기억했다가 나중에 완료통지가 오면 메모리를 해제해야 한다.			
			DWORD PacketCount = *pSentPacketCount = CurrentUsage;

			// 패킷 개수만큼 반복문 돌면서 실제 직렬화 버퍼 내의 패킷 버퍼 포인터를 WSABUF구조체에 저장
			for (DWORD i = 0; i < PacketCount; i++)
			{
				wsabuf[i].buf = (char*)PacketArray[i]->GetBufferPtr();
				wsabuf[i].len = PacketArray[i]->GetCurrentUsage();
			}

			///////////////////////////////////////////////////////////////////
			// 3. WSASend 등록하기 전에 I/O 카운트 증가
			///////////////////////////////////////////////////////////////////

			InterlockedIncrement(pIoCount);

			///////////////////////////////////////////////////////////////////
			// 4. WSASend 요청
			///////////////////////////////////////////////////////////////////
			DWORD bytesSent = 0;
			int Result = WSASend(sock, wsabuf, PacketCount, &bytesSent, 0, pOverlapped, nullptr);

			///////////////////////////////////////////////////////////////////
			// 5. WSASend 요청에 대한 예외처리
			///////////////////////////////////////////////////////////////////
			DWORD Error = WSAGetLastError();

			if (Result == SOCKET_ERROR)
			{
				if (Error != WSA_IO_PENDING)
				{
					// Error 코드가 WSAENOBUFS라면 에러 전달
					if (Error == WSAENOBUFS)
						OnError(WSAENOBUFS, _T("WSAENOBUFS"));

					// WSASend가 WSA_IO_PENDING 이외의 에러를 내면 함수 호출이 실패한것이다.
					// I/O Count 감소시킨다.					
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
		// 0. I/O 요청을 하기 전에 현재 링버퍼의 여유공간을 검사한다.
		///////////////////////////////////////////////////////////////////		
		int CurrentSpare = pRecvQ->GetCurrentSpare();

		///////////////////////////////////////////////////////////////////
		// 1. WSARecv용 Overlapped 구조체 초기화
		///////////////////////////////////////////////////////////////////

		ZeroMemory(pOverlapped, sizeof(OVERLAPPED));

		///////////////////////////////////////////////////////////////////
		//2. WSABUF 구조체 초기화
		///////////////////////////////////////////////////////////////////

		// WSABUF를 두개 사용하는 이유는 링버퍼가 중간에 단절되었을 때에도 한번에 다 받기 위함이다.
		WSABUF wsabuf[2];
		int RecvBufCount = 1;
		int RecvSize = pRecvQ->GetSizeWritableAtOnce();

		// 첫번째 버퍼에 대한 정보를 구조체에 등록한다.
		wsabuf[0].buf = pRecvQ->GetWritePtr();
		wsabuf[0].len = RecvSize;

		// 만약 링버퍼가 잘린 상태라면 두개의 버퍼에 나눠서 받는다.
		if (CurrentSpare > RecvSize)
		{
			RecvBufCount = 2;
			wsabuf[1].buf = pRecvQ->GetBufferPtr();
			wsabuf[1].len = CurrentSpare - RecvSize;
		}

		//////////////////////////////////////////////////////////////////////
		// 3. WSARecv 등록전에 I/O 카운트 증가, 다른 스레드에서도 접근하는 멤버이므로 락이 필요하다.
		//////////////////////////////////////////////////////////////////////
		InterlockedIncrement(pIoCount);

		///////////////////////////////////////////////////////////////////
		// 4. I/O 요청
		///////////////////////////////////////////////////////////////////
		DWORD BytesRead = 0;
		DWORD Flags = 0;
		int Result = WSARecv(sock, wsabuf, RecvBufCount, &BytesRead, &Flags, pOverlapped, nullptr);

		///////////////////////////////////////////////////////////////////
		// 5. I/O 요청에 대한 예외 처리
		///////////////////////////////////////////////////////////////////	
		if (Result == SOCKET_ERROR)
		{
			DWORD Error = WSAGetLastError();
			if (Error != WSA_IO_PENDING)
			{
				// 에러코드가 WSAENOBUFS라면 에러전달
				if (Error == WSAENOBUFS)
					OnError(WSAENOBUFS, _T("WSAENOBUFS"));

				// WSARecv가 WSA_IO_PENDING 이외의 에러를 내면 함수 호출이 실패한것이다.
				// I/O Count 감소시킨다.						
				SessionReleaseLock();
			}
		}

		return true;

	}

	void GGM::CLanClient::ReleaseSession()
	{
		// ReleaseFlag 가 TRUE가 되면 어떤 스레드도 이 세션에 접근하거나 릴리즈 시도를 해서는 안된다.
		// IoCount와 ReleaseFlag가 4바이트씩 연달아서 위치해 있으므로 아래와 같이 인터락함수 호출한다.
		// IoCount(LONG) == 0 ReleaseFlag(LONG) == 0 >> 인터락 성공시 >>IoCount(LONG) == 0 ReleaseFlag(LONG) == 1
		Session *pSession = &m_MySession;
		if (InterlockedCompareExchange64((volatile LONG64*)&(pSession->IoCount), 0x0000000100000000, FALSE) != FALSE)
			return;

		ULONGLONG SessionID = pSession->SessionID++;		

		// 만약 직렬화 버퍼를 동적할당 후 Send하는 도중에 오류가나서 Release를 해야한다면
		// 메모리를 해제해 주어야 한다.
		ULONG PacketCount = pSession->SentPacketCount;
		CPacket **PacketArray = pSession->PacketArray;	
		for (WORD i = 0; i < PacketCount; i++)
			CPacket::Free(PacketArray[i]);		

		// 링버퍼 내에 남은 찌꺼기 제거	
		ULONG GarbagePacketCount = pSession->SendQ.size();		
		for (ULONG i = 0; i < GarbagePacketCount; i++)
		{
			CPacket *pGarbagePacket;
			pSession->SendQ.Dequeue(&pGarbagePacket);
			CPacket::Free(pGarbagePacket);
		}		
		
		// 소켓 리소스 정리
		closesocket(pSession->socket);
		pSession->socket = INVALID_SOCKET;	

		// 해당 세션에 대한 모든 리소스가 정리되었으므로 이벤트 핸들링 함수 호출
		OnDisconnect();

		// 연결 끊겼을 때 재접속 옵션이 켜져있다면 재접속 시도
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
		// 이 함수를 호출했다는 것은 이후 로직에서 해당 세션을 사용하겠다는 의미이다.				
		// 이 세션이 현재 어떤 상태인지는 모르지만 세션에 접근한다는 의미로, I/O Count를 증가시킨다.
		// I/O Count를 먼저 증가시켰다면 릴리즈 되는 것을 막을 수 있다.
		// 만약 I/O Count를 증가시켰는데 1이라면 더 이상의 세션 접근은 무의미하다.
		ULONGLONG RetIOCount = InterlockedIncrement(&(m_MySession.IoCount));

		if (RetIOCount == 1 || m_MySession.IsReleased == TRUE || m_MySession.SessionID != LocalSessionID)
			return false;

		return true;
	}

	void GGM::CLanClient::SessionReleaseLock()
	{		
		// 세션에 대한 접근이 모두 끝났으므로 이전에 올린 I/O 카운트를 감소 시킨다.	
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
		// 소켓 생성
		m_MySession.socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

		if (m_MySession.socket == INVALID_SOCKET)
		{
			OnError(WSAGetLastError(), _T("Connect socket Creation Failed"));
			return false;
		}

		// connect 할 때 논블락 소켓을 이용한다.
		u_long NonBlockOn = 1;
		int iResult = ioctlsocket(m_MySession.socket, FIONBIO, &NonBlockOn);

		if (iResult == SOCKET_ERROR)
		{
			OnError(WSAGetLastError(), _T("ioctlsocket Failed"));
			return false;
		}

		// TCP_NODELAY 플래그 확인해서 켜주기
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

		// LINGER옵션 설정
		LINGER linger = { 1,0 };
		int Result = setsockopt(m_MySession.socket, SOL_SOCKET, SO_LINGER, (const char*)&linger, sizeof(linger));

		if (Result == SOCKET_ERROR)
		{
			OnError(WSAGetLastError(), _T("setsockopt [SO_LINGER] Failed"));
			return false;
		}

		// WSASend를 항상 비동기 I/O로 만들기 위해 소켓 송신 버퍼의 사이즈를 0으로 만든다.
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
