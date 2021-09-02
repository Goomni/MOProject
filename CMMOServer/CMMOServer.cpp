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
		
		// 각종 설정 변수 초기화			
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
		// 서버 종료 절차 
		// 1. 더 이상 accept를 받을 수 없도록 리슨소켓을 닫는다. accept 스레드 종료
		// 2. 모든 세션과의 연결을 끊는다.		
		// 3. 모든 세션이 I/O COUNT가 0이 되고 Release될 때까지 대기한다.
		// 4. 세션이 모두 안전하게 정리되면, auth, game 스레드를 종료 시킨다.
		// 5. 워커스레드 종료시킨다.		
		// 6. 서버에서 사용했던 기타 리소스들을 정리한다.
		////////////////////////////////////////////////////////////////////

		// Aceept 스레드 종료
		closesocket(m_Listen);
		m_Listen = INVALID_SOCKET;

		// Accept Thread 종료 대기 
		WaitForSingleObject(m_AcceptThread, INFINITE);
		CloseHandle(m_AcceptThread);

		// 모든 세션에 shutdown을 보낸다.
		CMMOSession **SessionArr = m_SessionArr;
		for (DWORD i = 0; i < m_MaxSessions; i++)
		{
			// 세션 배열을 순회하면서 모든 세션에 shutdown을 보낸다.
			if (SessionArr[i]->m_socket != INVALID_SOCKET)
			{
				SessionArr[i]->Disconnect();
			}
		}

		// 세션이 전부 정리될 때까지 대기한다.
		while (m_SessionCount > 0)
		{
			Sleep(100);
		}

		// auth, game, send 스레드가 종료할 수 있도록 플래그 바꿈
		// auth, game, send 스레드는 매 프레임마다 이 플래그를 체크하며 종료확인한다.
		m_IsExit = true;

		// Auth Thread 종료 대기 
		WaitForSingleObject(m_AuthThread, INFINITE);
		CloseHandle(m_AuthThread);

		// Game Thread 종료 대기 
		WaitForSingleObject(m_GameThread, INFINITE);
		CloseHandle(m_GameThread);

		// Send Thread 종료 대기 
		for (int i = 0; i < SEND_THREAD_COUNT; i++)
		{
			WaitForSingleObject(m_SendThread[i], INFINITE);
			CloseHandle(m_SendThread[i]);
		}		

		// Release Thread 종료 대기 
		WaitForSingleObject(m_ReleaseThread, INFINITE);
		CloseHandle(m_ReleaseThread);

		// 워커 스레드 IOCP 핸들을 닫는다. 이 시점에 워커스레드가 모두 종료된다.
		CloseHandle(m_hWorkerIOCP);

		// 워커스레드가 모두 종료될 때까지 대기한다.
		WaitForMultipleObjects(m_WorkerThreadsCount, m_WorkerThreadHandleArr, TRUE, INFINITE);
		for (DWORD i = 0; i < m_WorkerThreadsCount; i++)
			CloseHandle(m_WorkerThreadHandleArr[i]);

		// 기타 리소스 정리
		delete[] m_WorkerThreadHandleArr;
		delete[] m_WorkerThreadIDArr;
		delete[] m_SessionArr;	

		// 락프리 큐 정리
		delete m_AcceptQueue;
		
		// 세션 인덱스 큐 정리 
		delete m_NextSession;

		// 패킷풀 정리
		CNetPacket::DeletePacketPool();

		// 디버깅용으로 마지막 서버 구동 종료시에 세션 정리가 제대로 되었는지 확인하기 위한 용도
		_tprintf_s(_T("[SERVER OFF LAST INFO] SESSION ALIVE COUNT : %lld\n\n"), m_SessionCount);
	}

	ULONGLONG CMMOServer::GetSessionCount() const
	{
		return m_SessionCount;
	}

	unsigned int __stdcall CMMOServer::WorkerThread(LPVOID Param)
	{
		// Worker 스레드 함수가 static이기 때문에 멤버변수에 접근하거나 멤버함수를 호출하기 위해 
		// this 포인터를 인자로 받아온다.
		CMMOServer *pThis = (CMMOServer*)Param;

		// IOCP 핸들
		HANDLE hIOCP = pThis->m_hWorkerIOCP;		

		// 루프 돌면서 작업 한다.
		while (true)
		{
			// 완료통지 후 결과를 받을 변수들
			DWORD BytesTransferred; // 송수신 바이트 수
			CMMOSession *pSession; // 컴플리션 키로 전달한 CMMOSession 포인터
			OVERLAPPED *pOverlapped; // Overlapped 구조체 포인터의 포인터		

			// GQCS를 호출해서 일감이 오기를 대기한다.			
			bool bOk = GetQueuedCompletionStatus(hIOCP, &BytesTransferred, (PULONG_PTR)&pSession, (LPOVERLAPPED*)&pOverlapped, INFINITE);

			// 워커 스레드 일 시작했음.
			//pThis->OnWorkerThreadBegin();		

			if (bOk == false)
			{
				if (GetLastError() == 121)
				{
					pThis->OnSemError(pSession->m_SessionSlot);
				}
			}

			// Overlapped 구조체가 nullptr이면 문제가 발생한 것이다.
			if (pOverlapped == nullptr)
			{
				DWORD Error = WSAGetLastError();
				if (bOk == false && Error == ERROR_ABANDONED_WAIT_0)
				{
					// 오버랩드 구조체가 널인데 에러코드가 ERROR_ABANDONED_WAIT_0라면 외부에서 현재 사용중인 IOCP 핸들을 닫은 것이다.
					// 스레드 종료처리 로직이므로 종료 해주면 된다.		
					return 0;
				}

				pThis->OnError(WSAGetLastError(), _T("GQCS FAILED / OVERLAPPED NULL"));
				continue;
			}

			// pOverlapped가 nullptr가 아니라면 송수신바이트 수가 0인지 검사해야 한다. 
			// IOCP에서 송수신 바이트수는 0 이나 요청한 바이트 수 이외의 다른 값이 나올 수 없다. 			
			// 따라서 송수신 바이트 수가 0이 아니라면 요청한 비동기 I/O가 정상적으로 처리되었음을 의미한다.		
			// 추가로 외부에서 I/O를 강제로 캔슬했는지 여부도 확인 
			if (BytesTransferred == 0 || pSession->m_IsCanceled == true)
			{				
				if (pOverlapped == &(pSession->m_SendOverlapped))
					pSession->m_IsSending = false;				

				pSession->DecreaseIoCount();
				continue;
			}

			// 완료된 I/O가 RECV인지 SEND인지 판단한다.
			// 오버랩드 구조체 포인터 비교
			if (pOverlapped == &(pSession->m_RecvOverlapped))
			{
				//////////////////////////////////////////////////////
				// 완료된 I/O가 RECV 일 경우
				//////////////////////////////////////////////////////

				// 완료통지로 받은 바이트 수만큼 링버퍼의 Rear를 옮겨준다.				
				pSession->m_RecvQ.RelocateWrite(BytesTransferred);

				// 네트워크 클래스 계층의 완성된 패킷이 있는지 확인
				// CheckPacket 내부에서 모든 완료된 패킷을 세션별 패킷큐에 넣어준다.
				if (pThis->CheckPacket(pSession) == false)
				{
					// 이상한 패킷 수신을 확인했다면 바로 연결끊고 false 리턴
					// I/O Count 차감하고 Recv 걸지 않는다.
					pSession->DecreaseIoCount();
					continue;
				}

				// RecvPost 호출해서 WSARecv 재등록
				pThis->RecvPost(pSession);
			}
			else
			{
				//////////////////////////////////////////////////////
				// 완료된 I/O가 SEND 일 경우
				//////////////////////////////////////////////////////

				//pThis->OnSend(pSession->SessionID, BytesTransferred);				

				ULONG PacketCount = pSession->m_SentPacketCount;				
				CNetPacket **PacketArray = pSession->m_PacketArray;

				// 보내고 끊기 확인 
				if (pSession->m_IsSendAndDisconnect == true)
				{
					CNetPacket* pDisconnectPacket = pSession->m_pDisconnectPacket;				

					// 보내고 끊기 플래그가 true라면 현재 도착한 완료통지에 해당 패킷이 포함되었는지 확인
					for (ULONG i = 0; i < PacketCount; i++)
					{
						if (PacketArray[i] == pDisconnectPacket)
						{
							// 현재 도착한 완료통지에 보내고 끊기에 해당하는 패킷이 존재한다면 해당세션과의 연결종료							
							pSession->Disconnect();
							break;
						}
					}
				}

				// 비동기 I/O로 요청시 보관해 둔 직렬화 버퍼를 정리해준다.				
				for (ULONG i = 0; i < PacketCount; i++)
					CNetPacket::Free(PacketArray[i]);

				pSession->m_SentPacketCount = 0;

				// 2. 요청한 비동기 Send가 완료되었으므로 이제 해당 세션에 대해 Send할 수 있게 되었음				
				pSession->m_IsSending = false;				
			}

			// I/O Count는 일반적으로 이곳에서 감소시킨다. ( 오류가 났을 경우는 오류가 발생한 곳에서 감소 시킴 )
			// 감소결과 I/O COUNT가 0이 되면 로그아웃 플래그를 켠다.		
			// 로그아웃 플래그가 켜지면, Auth와 Game에서 모드 변경 후, 최종적으로 GameThread에서 세션이 릴리즈 된다.
			pSession->DecreaseIoCount();

			// 워커 스레드 일 끝났음
			//pThis->OnWorkerThreadEnd();
		}

		return 0;
	}

	unsigned int __stdcall CMMOServer::AcceptThread(LPVOID Param)
	{
		//////////////////////////////////////////////////////////////////////////////////
		// 0. AcceptThread 내부에서 사용할 변수 초기화
		//////////////////////////////////////////////////////////////////////////////////

		// Accept 스레드 함수가 static 함수이기 때문에 멤버변수에 접근하거나 멤버함수를 호출하기 위해 
		// this 포인터를 인자로 받아온다.
		CMMOServer *pThis = (CMMOServer*)Param;

		// 세션 배열 
		CMMOSession **SessionArr = pThis->m_SessionArr;

		// 리슨소켓
		SOCKET Listen = pThis->m_Listen;

		// IOCP 핸들
		HANDLE hIOCP = pThis->m_hWorkerIOCP;

		// 다음 세션의 정보를 가지고 있는 큐 (소켓, 주소, 인덱스)		
		CListQueue<WORD> *pNextSession = pThis->m_NextSession;

		// Auth Thread에게 세션 접속 정보를 전달하기 위한 큐		
		CLockFreeQueue<WORD> *pAcceptQueue = pThis->m_AcceptQueue;

		// SessionID, 최종 접속 성공시 발급해 줄 식별자 
		ULONGLONG SessionID = 0;

		// 현재 접속 중인 클라이언트 수의 포인터
		ULONGLONG *pSessionCount = &(pThis->m_SessionCount);

		// 최대 Accept할 수 있는 세션 개수
		DWORD MaxSessions = pThis->m_MaxSessions;

		// 접속한 클라이언트 주소 정보 
		SOCKADDR_IN ClientAddr;
		int AddrLen = sizeof(ClientAddr);

		// 모니터링용 변수
		ULONGLONG *pAcceptTotal = &(pThis->m_AcceptTotal);
		ULONGLONG *pAcceptTPS = &(pThis->m_AcceptTPS);		

		while (true)
		{
			//////////////////////////////////////////////////////////////////////////////////
			// 1. Accept 함수 호출하여 클라이언트 접속대기 
			//////////////////////////////////////////////////////////////////////////////////
			SOCKET client = accept(Listen, (SOCKADDR*)&ClientAddr, &AddrLen);			

			// accept 실패할 경우 에러 처리 함수인 OnError 호출해준다.			
			if (client == INVALID_SOCKET)
			{
				// 서버 종료 절차로 리슨소켓을 close했다면 Accept 스레드 종료
				DWORD error = WSAGetLastError();
				if (error == WSAEINTR || error == WSAENOTSOCK)
					return 0;

				pThis->OnError(WSAGetLastError(), _T("Accept Failed [WSAErrorCode :%d]"));
				continue;
			}

			(*pAcceptTotal)++;

			InterlockedIncrement(pAcceptTPS);

			//////////////////////////////////////////////////////////////////////////////////
			// 2. 접속요청한 세션의 유효성 확인
			//////////////////////////////////////////////////////////////////////////////////

			// 현재 접속 중인 세션이 최대 세션에 도달했다면 연결 끊는다.
			if (*pSessionCount == MaxSessions)
			{
				closesocket(client);				
				pThis->OnError(GGM_ERROR::TOO_MANY_CONNECTION, _T("Too Many Connection Error Code[%d]"));
				continue;
			}

			//////////////////////////////////////////////////////////////////////////////////
			// 3. 유효한 세션이므로 AcceptQueue에 추가
			// CNetServer에서는 Accept 스레드가 세션 접속 처리 마무리 후 RecvPost까지 수행했다.
			// 하지만 CMMOServer에서는 이외 작업을 Auth에게 넘긴다.
			//////////////////////////////////////////////////////////////////////////////////

			// 해당 세션 정보를 삽입할 Session배열의 인덱스를 얻어옴
			WORD SessionSlot;			
			bool bOk = pNextSession->Dequeue(&SessionSlot);

			if (bOk == false)
			{
				// 디큐 실패 ( 큐 사이즈가 0 )
				closesocket(client);				
				pThis->OnError(GGM_ERROR::INDEX_STACK_POP_FAILED, _T("INDEX_STACK_POP_FAILED [%d]"));
				continue;
			}

			// Accept Thread는 최소한의 클라이언트 접속처리만 함
			// 나머지는 AuthThread가 알아서
			SessionArr[SessionSlot]->m_socket = client;
			SessionArr[SessionSlot]->m_SessionIP = ClientAddr.sin_addr;
			SessionArr[SessionSlot]->m_SessionPort = ntohs(ClientAddr.sin_port);
			SessionArr[SessionSlot]->m_SessionSlot = SessionSlot;
			SessionArr[SessionSlot]->m_SessionID = ((SessionID << 16) | SessionSlot);	
			SessionID++;
			
			// IOCP 와 소켓 연결 
			HANDLE hRet = CreateIoCompletionPort((HANDLE)client, hIOCP, (ULONG_PTR)SessionArr[SessionSlot], 0);

			if (hRet != hIOCP)
			{
				pThis->OnError(GetLastError(), _T("CreateIoCompletionPort Failed %d"));
				closesocket(client);				
				continue;
			}			

			// Auth Thread에게 새로운 클라이언트가 접속했음을 알리기 위해 인큐		
			bOk = pAcceptQueue->Enqueue(SessionSlot);

			if (bOk == false)
			{
				closesocket(client);				
				pThis->OnError(GGM_ERROR::LOCK_FREE_Q_ENQ_FAILED, _T("LOCK_FREE_Q_ENQ_FAILED [%d]"));
				continue;
			}

			// 현재 접속 클라이언트 수 증가			
			InterlockedIncrement(pSessionCount);
		}

		return 0;

	}

	unsigned int __stdcall CMMOServer::SendThread(LPVOID Param)
	{
		// SendThread를 여러개 두었을 때 자신을 식별할 고유번호
		static LONG SendThreadID = -1;
		LONG MyID = InterlockedIncrement(&SendThreadID);

		// this 얻기
		CMMOServer *pThis = (CMMOServer*)Param;

		// 최대 접속자 수 얻기
		// SendThread 별로 접속자를 쪼개어서 처리 
		DWORD MaxSessions = (pThis->m_MaxSessions / SEND_THREAD_COUNT);

		// SendThread 슬립타임 얻기
		WORD  SendSleepTime = pThis->m_SendSleepTime;

		// 종료 플래그 포인터 얻기
		bool  *pIsExit = &pThis->m_IsExit;

		// 세션 배열 얻기
		// SendThread의 고유 ID에 따라 담당할 세션의 범위를 정한다.
		// 세션이 5천개이고 SendThread가 2개라면 아이디가 0번인 스레드는 0~2499번 세션까지, 1번인 스레드는 2500~4999 세션까지 담당 
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
			// Send Thread의 임무
			// * 일정시간 마다 깨어나서 각 세션별 SendQ에 있는것 전부 Send
			// * 만약 I/O Count가 0으로 떨어지면 로그아웃 플래그 ON
			// * 로그아웃 플래그는 CMMOServer의 릴리즈 동기화의 핵심
			//////////////////////////////////////////////////////////////

			//SleepEx(SendSleepTime, false);
				
			WaitForSingleObject(hEvent, SendSleepTime);

			// 서버 종료라면 반복문 탈출!
			if (*pIsExit == true)
				break;

			// 세션 배열 반복문 돌면서 SendPost
			for (DWORD SessionIdx = 0; SessionIdx < MaxSessions; SessionIdx++)
			{
				// 로그아웃 상태라면 Send시도 하지 않음	
				// 로그아웃 상태인 경우에는 아직 Auth나 Game모드에 있어도 이미 I/O Count가 0으로 떨어진 이후이다.
				// 따라서 언젠가는 릴리즈 될 것이고 SendPost 호출 자체가 무의미하며 낭비이다.
				// 한번 로그아웃 플래그 켜지면 세션 릴리즈 후 재접속 될때까지 계속 켜져있음				
				if (SessionArr[SessionIdx]->m_IsLogout == true)
					continue;

				if(SessionArr[SessionIdx]->m_SendQ.size() > 0)
				{
					// 로그아웃 상태 아니라면 SendPost!
					// 내부에서 I/O Count 0이 되면 로그아웃 플래그 켜짐 				
					pThis->SendPost(SessionArr[SessionIdx]);
				}
			}

		}

		return 0;
	}

	unsigned int __stdcall CMMOServer::AuthThread(LPVOID Param)
	{
		// this 얻기
		CMMOServer *pThis = (CMMOServer*)Param;

		// 최대 접속자 수 얻기
		DWORD MaxSessions = pThis->m_MaxSessions;

		// AuthThread 슬립타임 얻기
		WORD  AuthSleepTime = pThis->m_AuthSleepTime;

		// 종료 플래그 포인터 얻기
		bool  *pIsExit = &pThis->m_IsExit;

		// 세션 배열 얻기
		CMMOSession **SessionArr = pThis->m_SessionArr;

		// AcceptQueue 얻기		
		CLockFreeQueue<WORD> *pAcceptQueue = pThis->m_AcceptQueue;

		// 한 루프당 세션별 패킷 처리량
		WORD AuthPacketPerLoop = pThis->m_AuthPacketPerLoop;

		// 한 루프당 AcceptQ에서 Deq할 처리량
		WORD AcceptPerLoop = pThis->m_AcceptPerLoop;

		// 모니터링 변수
		volatile LONG *pAuthLoopPerSec = &pThis->m_AuthLoopPerSec;				
		LONG *pAuthSession = &pThis->m_AuthSession;

		// 타임아웃 변수		
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
			// Auth Thread의 임무
			// * 일정시간 마다 깨어나서 접속 클라이언트 인증처리
			// * 로그인 패킷 처리 및 인증과정에서 로드 가능한 컨텐츠 정보 DB에서 로드
			// * 패킷처리이외에 해주어야 하는 컨텐츠 처리, pThis->OnAuth Update()
			// * 인증처리 완료된 클라이언트를 Game Thread로 이관			
			// * 로그아웃 플래그 켜진 세션에 대해서 로그아웃 처리
			// * 최종 릴리즈는 GameThread에서만 처리
			//////////////////////////////////////////////////////////////////////					

			WaitForSingleObject(hEvent, AuthSleepTime);
			
			// 모니터링용으로 Auth Thread 루프 카운트 증가
			InterlockedIncrement(pAuthLoopPerSec);	

			// 서버 종료 확인
			if (*pIsExit == true)
				break;

			// 새로 접속한 클라이언트가 있는지 확인
			WORD AcceptCount = 0;
			while (pAcceptQueue->size() > 0)
			{				
				// 접속 처리를 이곳에서 마무리 				
				WORD SessionSlot;
				pAcceptQueue->Dequeue(&SessionSlot);

				// 나머지 정보를 이 곳에서 초기화					
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

				// Auth Mode 입장처리							
				SessionArr[SessionSlot]->OnAuth_ClientJoin();				

				// 최초 RecvPost 호출
				pThis->RecvPost(SessionArr[SessionSlot]);	
				SessionArr[SessionSlot]->DecreaseIoCount();

				// AcceptQ에서 Deq해서 처리한 횟수를 증가시키고 제한치가 되었으면 루프 탈출 
				AcceptCount++;
				(*pAuthSession)++;				

				if (AcceptCount >= AcceptPerLoop)
					break;
			}

			// 세션 배열 돌면서 세션에 관련된 처리			
			for (DWORD SessionIdx = 0; SessionIdx < MaxSessions; SessionIdx++)
			{
				//////////////////////////////////////////////////////////////////////
				// 1. 게임 모드로 변경 처리
				// 2. 로그아웃 처리
				// 3. 패킷 처리				
				//////////////////////////////////////////////////////////////////////

				// 현재 세션의 모드가 AUTH인 경우에만 처리한다.
				if (SessionArr[SessionIdx]->m_Mode != SESSION_MODE::MODE_AUTH)
					continue;

				// 세션이 GameMode로 이동해야 하는지 검사
				// AuthToGame 플래그는 컨텐츠에서 조절
				if (SessionArr[SessionIdx]->m_IsAuthToGame == true)
				{
					// AuthToGame 플래그가 켜졌다면 퇴장 처리 후 모드 변경
					SessionArr[SessionIdx]->OnAuth_ClientLeave(false);
					(*pAuthSession)--;
					SessionArr[SessionIdx]->m_Mode = SESSION_MODE::MODE_AUTH_TO_GAME;
					continue;
				}

				// 로그아웃 대상인지 확인
				if (SessionArr[SessionIdx]->m_IsLogout == true)
				{
					// 아직 Send중이라면 이 세션에 대해서는 릴리즈 할 수 없음
					if (SessionArr[SessionIdx]->m_IsSending == true)
						continue;

					// Send중이 아니라면 AuthThread 퇴장 처리 후 MODE_WAIT_LOGOUT 모드 변경
					// 해당 모드로 변경되어도 최종 릴리즈는 ReleaseThread에 의해서 수행됨
					SessionArr[SessionIdx]->OnAuth_ClientLeave(true);	
					(*pAuthSession)--;
					SessionArr[SessionIdx]->m_Mode = SESSION_MODE::MODE_WAIT_LOGOUT;
				}
				else
				{					
					// 로그아웃 대상이 아니라면 패킷 처리 
					// 다음 루프전까지 이 세션이 릴리즈 될일은 없다.
					// 서버 구동시 한 루프에 몇 개의 패킷을 처리할지 정하고 그만큼 처리한다.
					// 세션별 패킷처리를 공정하게 하기 위한 기능
					ULONGLONG PacketCount = SessionArr[SessionIdx]->m_PacketQ.size();

					if (PacketCount > AuthPacketPerLoop)
						PacketCount = AuthPacketPerLoop;

					for (WORD Count = 0; Count < PacketCount; Count++)
					{
						// Deq 한 패킷을 처리한 후, Recv완료통지에서 넘겨줄 때 올린 RefCnt를 줄이기 위해 Free
						CNetPacket* pPacket;
						SessionArr[SessionIdx]->m_HeartbeatTime = GetTickCount64();
						SessionArr[SessionIdx]->m_PacketQ.Dequeue(&pPacket);
						SessionArr[SessionIdx]->OnAuth_ClientPacket(pPacket);
						CNetPacket::Free(pPacket);
					}

					// 타임아웃 처리해야한다면 처리 
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

			// 로그아웃 처리, 패킷처리, AuthToGame 처리가 모두 끝났다면 클라이언트 요청과 독립적인 Update 수행
			pThis->OnAuth_Update();
		}

		CloseHandle(hEvent);
		return 0;
	}

	unsigned int __stdcall CMMOServer::GameThread(LPVOID Param)
	{
		// this 얻기
		CMMOServer *pThis = (CMMOServer*)Param;

		// 최대 접속자 수 얻기
		DWORD MaxSessions = pThis->m_MaxSessions;

		// AuthThread 슬립타임 얻기
		WORD  GameSleepTime = pThis->m_GameSleepTime;

		// 종료 플래그 포인터 얻기
		bool  *pIsExit = &pThis->m_IsExit;

		// 세션 배열 얻기
		CMMOSession **SessionArr = pThis->m_SessionArr;

		// 다음 세션의 정보를 가지고 있는 큐 (소켓, 주소, 인덱스)
		// 최종 릴리즈시 여기에 세션 정보 반환
		CListQueue<WORD> *pNextSession = pThis->m_NextSession;	

		// 한 루프당 세션별 패킷 처리량
		WORD GamePacketPerLoop = pThis->m_GamePacketPerLoop;

		// 한 루프당 AuthToGame 처리량
		WORD AuthToGamePerLoop = pThis->m_AuthToGamePerLoop;

		// 모니터링용 변수
		volatile LONG *pGameLoopPerSec = &pThis->m_GameLoopPerSec;
		volatile ULONGLONG *pSessionCount = &(pThis->m_SessionCount);
		LONG *pGameSession = &pThis->m_GameSession;		

		// 타임아웃 변수
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
			// Game Thread의 임무
			// * 일정시간 마다 깨어나서 Game Mode 세션 처리 			
			// * 릴리즈 해주어야 할 세션들 릴리즈 해줌
			// * AuthToGame Mode에 있는 세션들을 Game Mode로 이동
			// * 로그아웃 플래그 켜진 세션에 대해서 로그아웃 처리			
			// * GameMode에 있는 세션들 패킷 처리 
			// * 패킷처리이외에 해주어야 하는 컨텐츠 처리, pThis->Auth Update()							
			// * 최종 릴리즈는 GameThread에서만 처리
			//////////////////////////////////////////////////////////////////////			

			WaitForSingleObject(hEvent, GameSleepTime);
			
			// 모니터링용으로 Game Thread 루프 카운트 증가
			InterlockedIncrement(pGameLoopPerSec);

			// 서버 종료 확인
			if (*pIsExit == true)
				break;

			// 세션 배열 돌면서 세션에 관련된 처리
			WORD AuthToGameCount = 0;
			for (DWORD SessionIdx = 0; SessionIdx < MaxSessions; SessionIdx++)
			{
				//////////////////////////////////////////////////////////////////////
				// 1. AuthToGame Mode에 있는 세션들을 Game Mode로 이동
				// 2. 최종 릴리즈 대상인 세션 정리하기 
				// 3. 로그아웃 처리 
				// 4. 패킷 처리				
				//////////////////////////////////////////////////////////////////////
				
				// 한번에 Auth To Game에서 Game 모드로 바꿀 유저 수를 조절한다. 
				if ((SessionArr[SessionIdx]->m_Mode == SESSION_MODE::MODE_AUTH_TO_GAME) && (AuthToGameCount < AuthToGamePerLoop))
				{
					// Game Mode로 입장 처리 및 모드 변경
					SessionArr[SessionIdx]->OnGame_ClientJoin();
					(*pGameSession)++;
					SessionArr[SessionIdx]->m_Mode = SESSION_MODE::MODE_GAME;
					AuthToGameCount++;
				}

				// 최종 릴리즈 대상인지 확인
				if (SessionArr[SessionIdx]->m_Mode == SESSION_MODE::MODE_WAIT_LOGOUT)
				{					
					pThis->ReleaseSession(SessionArr[SessionIdx]);
					InterlockedDecrement(pSessionCount);
					continue;
				}

				// 이 아래부터는 실제 모드가 Game인 경우에만 처리한다.				
				if (SessionArr[SessionIdx]->m_Mode != SESSION_MODE::MODE_GAME)
					continue;				

				// 로그아웃 대상인지 확인
				if (SessionArr[SessionIdx]->m_IsLogout == true)
				{
					// 아직 Send중이라면 이 세션에 대해서는 릴리즈 할 수 없음
					if (SessionArr[SessionIdx]->m_IsSending == true)
						continue;

					// Send중이 아니라면 GameThread 퇴장 처리 후 MODE_WAIT_LOGOUT 모드 변경					
					SessionArr[SessionIdx]->OnGame_ClientLeave();
					(*pGameSession)--;
					//SessionArr[SessionIdx]->m_Mode = SESSION_MODE::MODE_WAIT_LOGOUT;
					pThis->ReleaseSession(SessionArr[SessionIdx]);
					InterlockedDecrement(pSessionCount);
					continue;
				}				
				
				// 로그아웃 대상이 아니라면 패킷 처리 
				// 여기까지 왔으면 다음 루프전까지 이 세션이 릴리즈 될일은 없다.
				// 서버 구동시 한 루프에 몇 개의 패킷을 처리할지 정하고 그만큼 처리한다.
				// 세션별 패킷처리를 공정하게 하기 위한 기능
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

				// 타임아웃 처리해야한다면 처리 
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

			// 로그아웃 처리, 패킷처리, AuthToGame 처리가 모두 끝났다면 클라이언트 요청과 독립적인 Update 수행
			pThis->OnGame_Update();			

		}

		return 0;
	}	

	bool CMMOServer::CheckPacket(CMMOSession * pSession)
	{
		CRingBuffer *pRecvQ = &(pSession->m_RecvQ);

		// 현재 RECV 링버퍼 사용량 체크
		int CurrentUsage = pRecvQ->GetCurrentUsage();
		int Result;

		// 링버퍼에 최소한 헤더길이 이상이 있는지 먼저확인한다.
		// 헤더길이 이상이 있으면 루프돌면서 존재하는 완성된 패킷 다 뽑아서 해당 세션 패킷큐에 인큐
		LONG PacketCount = 0;
		while (CurrentUsage >= NET_HEADER_LENGTH)
		{
			NET_HEADER Header;

			// 해당 세션 링버퍼에서 헤더만큼 PEEK
			Result = pRecvQ->Peek((char*)&Header, NET_HEADER_LENGTH);

			if (Result != NET_HEADER_LENGTH)
			{				
				OnError(GGM_ERROR::BUFFER_READ_FAILED, _T("[CMMOServer] NetHeader Peek Failed!!"));				
				return false;
			}

			CurrentUsage -= NET_HEADER_LENGTH;

			// 패킷 코드가 올바르지 않다면 해당 세션 연결종료
			if (Header.PacketCode != CNetPacket::PacketCode)
			{
				TCHAR SessionIP[IP_LEN];
				InetNtop(AF_INET, &(pSession->m_SessionIP), SessionIP, IP_LEN);
				CLogger::GetInstance()->Log(_T("CMMOServer Log"), LEVEL::ERR, OUTMODE::FILE, _T("WRONG PACKET CODE[%s:%hd]"), SessionIP,htons(pSession->m_SessionPort));
				pSession->Disconnect();
				return false;
			}

			// 컨텐츠 페이로드 사이즈가 직렬화 버퍼의 크기를 초과한다면 연결끊는다.
			if (Header.Length > DEFAULT_BUFFER_SIZE - NET_HEADER_LENGTH)
			{
				TCHAR SessionIP[IP_LEN];
				InetNtop(AF_INET, &(pSession->m_SessionIP), SessionIP, IP_LEN);
				CLogger::GetInstance()->Log(_T("CMMOServer Log"), LEVEL::ERR, OUTMODE::FILE, _T("TOO BIG PACKET RECV[%s:%hd]"), SessionIP, htons(pSession->m_SessionPort));
				pSession->Disconnect();				
				return false;
			}

			// 현재 링버퍼에 완성된 패킷만큼 없으면 반복문 탈출 
			if (CurrentUsage < Header.Length)
				break;

			// 완성된 패킷이 있다면 HEADER는 RecvQ에서 지워준다.
			pRecvQ->EraseData(NET_HEADER_LENGTH);

			CNetPacket *pPacket = CNetPacket::Alloc();

			// 완성된 패킷을 뽑아서 직렬화 버퍼에 담는다.
			Result = pRecvQ->Dequeue(pPacket->m_pSerialBuffer, Header.Length);

			if (Result != Header.Length)
			{
				OnError(GGM_ERROR::BUFFER_READ_FAILED, _T("[CNetServer] Payload Dequeue Failed!!"));
				break;				
			}		

			// 패킷에 카피한만큼 쓰기 포인터 반영
			pPacket->RelocateWrite(Header.Length - NET_HEADER_LENGTH);

			// 현재 전달된 패킷을 복호화 하고 유효성을 검사한다.
			if (pPacket->Decode(&Header) == false)
			{
				// 복호화된 패킷이 이상하다면 연결을 끊는다.	

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

			// 완성된 패킷이 존재하므로 해당 세션 패킷큐에 인큐							
			InterlockedIncrement(&pPacket->m_RefCnt);
			pSession->m_PacketQ.Enqueue(pPacket);			

			// 현재 링버퍼의 사용 사이즈를 패킷 사이즈만큼 감소
			CurrentUsage -= Header.Length;

			// Alloc에 대한 프리
			CNetPacket::Free(pPacket);	
			PacketCount++;
		}

		// 모니터링용 변수
		InterlockedAdd(&m_RecvTPS, PacketCount);		

		return true;
	}

	bool CMMOServer::SendPost(CMMOSession * pSession)
	{
		///////////////////////////////////////////////////////////////////
		// 0. Send 가능 플래그 체크 
		///////////////////////////////////////////////////////////////////

		// 이미 해당 세션에 대해서 Send 중이라면 Send하지 않는다.
		if (pSession->m_IsSending == true)
			return true;

		// MMOServer 구조에서는 오직 SendThread에서만 해당 세션에 대해서 SendPost를 호출하기 때문에 인터락 사용하지 않아도 안전 
		pSession->m_IsSending = true;		
		
		// 로그아웃 플래그와 모드를 확인하는 행위는 동치이다. 따라서 로그아웃 플래그 확인함
		// 뒤에 보낸 패킷 카운트를 확인하는 이유는 세션 구조체의 패킷 포인터 배열을 덮어쓰지 않기 위함
		if (pSession->m_IsLogout == true || pSession->m_SentPacketCount > 0)
		{
			pSession->m_IsSending = false;
			return true;
		}
		
		CListQueue<CNetPacket*> *pSendQ = &(pSession->m_SendQ);

		// 현재 SendQ 사용량을 확인해서 보낼 것이 있는지 다시 한번 확인해본다.
		// 보낼 것이 있는줄 알고 들어왔는데 보낼 것이 없을 수도 있다.
		ULONG CurrentUsage = (ULONG)pSendQ->size();

		if(CurrentUsage == 0)
		{
			pSession->m_IsSending = false;
			return true;
		}
		
		///////////////////////////////////////////////////////////////////
		// 1. WSASend용 Overlapped 구조체 초기화
		///////////////////////////////////////////////////////////////////
		ZeroMemory(&(pSession->m_SendOverlapped), sizeof(OVERLAPPED));

		///////////////////////////////////////////////////////////////////
		//2. WSABUF 구조체 초기화
		///////////////////////////////////////////////////////////////////

		// SendQ에는 패킷단위의 직렬화버퍼 포인터가 저장되어 있다.
		// 포인터를 SendQ에서 디큐해서 해당 직렬화 버퍼가 가지고 있는 패킷의 포인터를 WSABUF에 전달하고, 모아서 보낸다.
		// 혼란방지용 [SendQ : 직렬화 버퍼의 포인터 (CNetPacket*)] [ WSABUF : 직렬화버퍼내의 버퍼포인터 (char*)]
		// WSABUF에 한번에 모아 보낼 패킷의 개수는 정적으로 100 ~ 500개 사이로 정한다.
		// WSABUF의 개수를 너무 많이 잡으면 시스템이 해당 메모리를 락 걸기 때문에 메모리가 이슈가 발생할 수 있다. 
		// 직렬화버퍼의 포인터와 송신한 개수를 보관했다가 완료통지가 뜨면 메모리 해제 

		// 직렬화 버퍼내의 패킷 메모리 포인터(char*)를 담을 WSABUF 구조체 배열
		WSABUF        wsabuf[MAX_PACKET_COUNT];

		// SendQ에서 뽑아낸 직렬화 버퍼의 포인터(CPacket*)를 담을 배열
		CNetPacket **PacketArray = pSession->m_PacketArray;

		// SendQ에서 한번에 송신 가능한 패킷외 최대 개수만큼 직렬화 버퍼의 포인터를 Dequeue한다.		
		// 세션별로 가지고 있는 직렬화 버퍼 포인터 배열에 그것을 복사한다.
		// Peek을 한 후 완료통지 이후에 Dequeue를 하면 memcpy가 추가적으로 일어나므로 메모리를 희생해서 횟수를 줄였다.			

		// 현재 큐에 들어있는 포인터의 개수가 최대치를 초과했다면 보정해준다.
		if (CurrentUsage > MAX_PACKET_COUNT)
			CurrentUsage = MAX_PACKET_COUNT;

		for (ULONG i = 0; i < CurrentUsage; i++)
			pSession->m_SendQ.Dequeue(&PacketArray[i]);

		// 보낸 패킷의 개수를 기억했다가 나중에 완료통지가 오면 메모리를 해제해야 한다.			
		DWORD PacketCount = pSession->m_SentPacketCount = CurrentUsage;
	
		// 모니터링용 변수
		InterlockedAdd(&m_SendTPS, PacketCount);

		// 패킷 개수만큼 반복문 돌면서 실제 직렬화 버퍼 내의 패킷 버퍼 포인터를 WSABUF구조체에 저장
		for (DWORD i = 0; i < PacketCount; i++)
		{
			wsabuf[i].buf = (char*)PacketArray[i]->m_pSerialBuffer;
			wsabuf[i].len = PacketArray[i]->m_Rear;
		}

		///////////////////////////////////////////////////////////////////
		// 3. WSASend 등록하기 전에 I/O 카운트 증가
		///////////////////////////////////////////////////////////////////

		InterlockedIncrement(&(pSession->m_IoCount));

		///////////////////////////////////////////////////////////////////
		// 4. WSASend 요청
		///////////////////////////////////////////////////////////////////
		DWORD bytesSent = 0;
		int Result = WSASend(pSession->m_socket, wsabuf, PacketCount, &bytesSent, 0, &(pSession->m_SendOverlapped), nullptr);

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
				pSession->m_IsSending = false;
				pSession->DecreaseIoCount();				
			}
		}		

		// Disconnect 함수가 호출되어서 I/O가 모두 취소되어야한다면 방금 이 워커스레드가 요청한 I/O를 취소한다.
		if (pSession->m_IsCanceled == true)
			CancelIoEx((HANDLE)pSession->m_socket, nullptr);

		return true;
	}

	bool CMMOServer::RecvPost(CMMOSession * pSession)
	{
		///////////////////////////////////////////////////////////////////
		// 0. I/O 요청을 하기 전에 현재 링버퍼의 여유공간을 검사한다.
		///////////////////////////////////////////////////////////////////

		CRingBuffer *pRecvQ = &(pSession->m_RecvQ);
		int CurrentSpare = pRecvQ->GetCurrentSpare();

		// 현재 RecvQ의 여유공간이 0이라면 비정상적인 상황이다.
		// 정상적인 상황이라면 세션의 링버퍼가 가득차는 상황은 발생하지 않을 것이다.
		// 그러나 누군가가 악의적인 의도를 가지고 링버퍼가 수용가능한 것 이상으로 헤더의 페이로드 사이즈를 조작하고 거대한 페이로드를 보내버린다면 꽉 찰 수 있다.
		// 이런 경우 이 세션은 끊어야 한다.
		if (CurrentSpare == 0)
		{
			// 에러 전달		

			/*TCHAR ErrMsg[512];

				StringCbPrintf(
					ErrMsg,
					512,
					_T("RECV BUFFER FULL [%s:%hd]"),
					InetNtop(AF_INET, &(pSession->SessionIP), ErrMsg, sizeof(ErrMsg)),
					htons(pSession->SessionPort)
				);

				OnError(GGM_ERROR_RECV_BUFFER_FULL, ErrMsg);*/

				// I/O Count를 0으로 만들어서 세션이 릴리즈 되도록 shutdown으로 연결끊음		
			OnError(GGM_ERROR::BUFFER_FULL, _T("[CNetServer] RecvPost() RecvQ CurrentSpare == 0 ErrorCode[%d]"));
			pSession->Disconnect();

			return true;
		}

		///////////////////////////////////////////////////////////////////
		// 1. WSARecv용 Overlapped 구조체 초기화
		///////////////////////////////////////////////////////////////////

		ZeroMemory(&(pSession->m_RecvOverlapped), sizeof(OVERLAPPED));

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
		// 3. WSARecv 등록전에 I/O 카운트 증가
		//////////////////////////////////////////////////////////////////////
		InterlockedIncrement(&(pSession->m_IoCount));

		///////////////////////////////////////////////////////////////////
		// 4. I/O 요청
		///////////////////////////////////////////////////////////////////
		DWORD BytesRead = 0;
		DWORD Flags = 0;
		int Result = WSARecv(pSession->m_socket, wsabuf, RecvBufCount, &BytesRead, &Flags, &(pSession->m_RecvOverlapped), nullptr);

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
				pSession->DecreaseIoCount();
			}
		}

		// Disconnect 함수가 호출되어서 I/O가 모두 취소되어야한다면 방금 이 워커스레드가 요청한 I/O를 취소한다.
		if (pSession->m_IsCanceled == true)
			CancelIoEx((HANDLE)pSession->m_socket, nullptr);

		return true;
	}

	void CMMOServer::ReleaseSession(CMMOSession * pSession)
	{
		//////////////////////////////////////////////////////////////////
		// 릴리즈 시 해야 할일					
		// 1. 세션 패킷 배열, SendQ, CompletePacketQ에 남은 잔존 패킷 처리 
		// 2. closesocket
		// 3. 세션 모드 NONE으로 변경
		// 4. 사용 가능 세션 인덱스 반환
		//////////////////////////////////////////////////////////////////

		// 세션 패킷 배열 정리
		ULONG PacketCount = pSession->m_SentPacketCount;
		CNetPacket **PacketArray = pSession->m_PacketArray;
		for (WORD Count = 0; Count < PacketCount; Count++)
			CNetPacket::Free(PacketArray[Count]);

		// SendQ 정리
		ULONG GarbagePacketCount = (ULONG)pSession->m_SendQ.size();
		for (ULONG Count = 0; Count < GarbagePacketCount; Count++)
		{
			CNetPacket *pGarbagePacket;
			pSession->m_SendQ.Dequeue(&pGarbagePacket);
			CNetPacket::Free(pGarbagePacket);
		}

		// 세션 패킷 큐 정리
		GarbagePacketCount = (ULONG)pSession->m_PacketQ.size();
		for (ULONG Count = 0; Count < GarbagePacketCount; Count++)
		{
			CNetPacket *pGarbagePacket;
			pSession->m_PacketQ.Dequeue(&pGarbagePacket);
			CNetPacket::Free(pGarbagePacket);
		}

		// 모드변경
		pSession->m_Mode = SESSION_MODE::MODE_NONE;

		// 소켓 정리		
		closesocket(pSession->m_socket);
		pSession->m_socket = INVALID_SOCKET;

		// OnRelease
		pSession->OnClientRelease();

		// 인덱스 큐에 인큐
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

		// BindIP를 nullptr로 설정하면 INADDR_ANY로 바인딩
		// 바인딩할 주소를 설정했다면 해당주소로 주소 지정
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
		// 최대 접속자 수  멤버에 저장 
		m_MaxSessions = MaxSessions;

		// 최대 세션 개수 만큼 CMMOSession* 배열 동적할당
		m_SessionArr = new CMMOSession*[MaxSessions];

		if (m_SessionArr == nullptr)
		{
			OnError(GetLastError(), _T("MEM Alloc Failed ErrorCode[%d]"));
			return false;
		}

		// CNetServer와 다르게 CMMOServer는 Session과 Player가 완벽하게 분리되지 않는다.
		// 컨텐츠 모듈에서 CMMOSession 클래스를 상속받아 Player를 정의하고 그 배열의 시작주소를 인자로 넣어준다.
		// 네트워크 모듈에서는 각 Player의 포인터를 베이스포인터로 형변환해 네트워크 처리를 수행한다.
		// CMMOServer 입장에서는 컨텐츠에서 생성한 플레이어에 대한 정보를 알지못하기 때문에 객체 크기도 같이 인자로 받는다.
		for (WORD i = 0; i < MaxSessions; i++)
		{
			m_SessionArr[i] = (CMMOSession*)((char*)pPlayerArr + (PlayerSize * i));
		}

		// 해당 정보는 Accept스레드에서 클라이언트의 접속을 받았을 때, Enqueue하여 Auth Thread로 전달
		// 구조체에는 Session에 대한 기본정보 (소켓, 주소, 배열의 인덱스)가 포함되어 있음
		//m_NextSession = new CListQueue<SessionInfo*>();
		m_NextSession = new CListQueue<WORD>();

		if (m_NextSession == nullptr)
		{
			OnError(GetLastError(), _T("MEM Alloc Failed ErrorCode[%d]"));
			return false;
		}

		// 처음이니까 모든 인덱스를 넣어준다.	
		for (WORD i = 0; i < (MaxSessions); i++)
		{
			m_NextSession->Enqueue(i);
		}

		// AcceptThread와 Auth Thread 사이의 락프리 큐
		// 새로운 클라이언트가 접속하면 이 큐에 AcceptThread가 인큐, Auth Thread는 매 프레임마다 큐를 확인하여 접속처리		
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
		// 워커스레드와 연동될 IOCP 생성
		m_hWorkerIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, ConcurrentWorkerThreads);

		if (m_hWorkerIOCP == NULL)
		{
			OnError(GetLastError(), _T("CreateIoCompletionPort Failed ErrorCode[%d]"));
			return false;
		}

		// 스레드 생성 및 정보 저장
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

		/////////////////// WorkerThread 생성 //////////////////////////////
		// 사용자가 요청한 개수의 워커 스레드(동시실행 개수 아님) 생성
		for (DWORD i = 0; i < MaxWorkerThreads; i++)
		{
			m_WorkerThreadHandleArr[i] = (HANDLE)_beginthreadex(
				nullptr,
				0,
				CMMOServer::WorkerThread, // 워커 스레드 함수 클래스 내부에 private static으로 선언되어있음
				this, // 워커스레드 함수가 static으로 선언되어있으므로 다른 멤버 변수나 함수에 접근하기 위해서 해당 인스턴스의 this 포인터가 필요함
				0,
				(unsigned int*)&m_WorkerThreadIDArr[i]
			);

			if (m_WorkerThreadIDArr[i] == NULL)
			{
				OnError(GetLastError(), _T("_beginthreadex Failed ErrorCode[%d]"));
				return false;
			}
		}
		/////////////////// WorkerThread 생성 //////////////////////////////	

		/////////////////// AcceptThread 생성 //////////////////////////////
		m_AcceptThread = (HANDLE)_beginthreadex(
			nullptr,
			0,
			CMMOServer::AcceptThread, // Accept 스레드 함수 클래스 내부에 private static으로 선언되어있음
			this, // Accept 스레드 함수가 static으로 선언되어있으므로 다른 멤버 변수나 함수에 접근하기 위해서 해당 인스턴스의 this 포인터가 필요함
			0,
			(unsigned int*)&m_AcceptThreadID
		);

		if (m_AcceptThread == NULL)
		{
			OnError(GetLastError(), _T("_beginthreadex Failed  ErrorCode[%d]"));
			return false;
		}
		/////////////////// AcceptThread 생성 //////////////////////////////		

		/////////////////// SendThread 생성 //////////////////////////////
		for (int i = 0; i < SEND_THREAD_COUNT; i++)
		{
			m_SendThread[i] = (HANDLE)_beginthreadex(
				nullptr,
				0,
				CMMOServer::SendThread, // Accept 스레드 함수 클래스 내부에 private static으로 선언되어있음
				this, // Accept 스레드 함수가 static으로 선언되어있으므로 다른 멤버 변수나 함수에 접근하기 위해서 해당 인스턴스의 this 포인터가 필요함
				0,
				(unsigned int*)&m_SendThreadID[i]
			);

			if (m_SendThread[i] == NULL)
			{
				OnError(GetLastError(), _T("_beginthreadex Failed  ErrorCode[%d]"));
				return false;
			}
		}
		/////////////////// SendThread 생성 //////////////////////////////		

		/////////////////// AuthThread 생성 //////////////////////////////
		m_AuthThread = (HANDLE)_beginthreadex(
			nullptr,
			0,
			CMMOServer::AuthThread, // Accept 스레드 함수 클래스 내부에 private static으로 선언되어있음
			this, // Accept 스레드 함수가 static으로 선언되어있으므로 다른 멤버 변수나 함수에 접근하기 위해서 해당 인스턴스의 this 포인터가 필요함
			0,
			(unsigned int*)&m_AuthThreadID
		);

		if (m_AuthThread == NULL)
		{
			OnError(GetLastError(), _T("_beginthreadex Failed  ErrorCode[%d]"));
			return false;
		}
		/////////////////// AuthThread 생성 //////////////////////////////	

		/////////////////// GameThread 생성 //////////////////////////////
		m_GameThread = (HANDLE)_beginthreadex(
			nullptr,
			0,
			CMMOServer::GameThread, // Accept 스레드 함수 클래스 내부에 private static으로 선언되어있음
			this, // Accept 스레드 함수가 static으로 선언되어있으므로 다른 멤버 변수나 함수에 접근하기 위해서 해당 인스턴스의 this 포인터가 필요함
			0,
			(unsigned int*)&m_GameThreadID
		);

		if (m_GameThreadID == NULL)
		{
			OnError(GetLastError(), _T("_beginthreadex Failed  ErrorCode[%d]"));
			return false;
		}
		/////////////////// GameThread 생성 //////////////////////////////	

		/////////////////// ReleaseThread 생성 //////////////////////////////
		//m_ReleaseThread = (HANDLE)_beginthreadex(
		//	nullptr,
		//	0,
		//	CMMOServer::ReleaseThread, // Accept 스레드 함수 클래스 내부에 private static으로 선언되어있음
		//	this, // Accept 스레드 함수가 static으로 선언되어있으므로 다른 멤버 변수나 함수에 접근하기 위해서 해당 인스턴스의 this 포인터가 필요함
		//	0,
		//	(unsigned int*)&m_ReleaseThreadID
		//);

		//if (m_ReleaseThreadID == NULL)
		//{
		//	OnError(GetLastError(), _T("_beginthreadex Failed  ErrorCode[%d]"));
		//	return false;
		//}
		/////////////////// ReleaseThread 생성 //////////////////////////////		

		return true;
	}
}







