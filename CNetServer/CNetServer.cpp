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
		// 0. AcceptThread 내부에서 사용할 변수 초기화
		//////////////////////////////////////////////////////////////////////////////////

		// Accept 스레드 함수가 static이기 때문에 멤버변수에 접근하거나 멤버함수를 호출하기 위해 
		// this 포인터를 인자로 받아온다.
		CNetServer *pThis = (CNetServer*)Param;

		// 리슨소켓
		SOCKET Listen = pThis->m_Listen;

		// IOCP 핸들
		HANDLE hIOCP = pThis->m_hWorkerIOCP;

		// 세션배열의 포인터 얻어오기
		NetSession *SessionArray = pThis->m_SessionArray;

		// 다음 세션이 들어갈 인덱스를 가지고 있는 스택		
		CLockFreeStack<WORD> *pNextSlot = pThis->m_NextSlot;

		// SessionID, 최종 접속 성공시 발급해 줄 식별자 
		ULONGLONG SessionID = 0;

		// 현재 접속 중인 클라이언트 수의 포인터
		ULONGLONG *pSessionCount = &(pThis->m_SessionCount);

		// 최대 Accept할 수 있는 세션 개수
		DWORD MaxSessions = pThis->m_MaxSessions;

		// 접속한 클라이언트 주소 정보 
		SOCKADDR_IN ClientAddr;		
		int AddrLen = sizeof(ClientAddr);

		// 모니터링용 Accept
		ULONGLONG *pAcceptTotal = &(pThis->m_AcceptTotal);
		ULONGLONG *pAcceptTPS = &(pThis->m_AcceptTPS);		

		while (true)
		{
			//////////////////////////////////////////////////////////////////////////////////
			// 1. Accept 함수 호출하여 클라이언트 접속대기 
			//////////////////////////////////////////////////////////////////////////////////
			SOCKET client = accept(Listen, (SOCKADDR*)&ClientAddr, &AddrLen);		

			// 모니터링용 accept total 변수 ++
			(*pAcceptTotal)++;

			// 모니터링용 accept TPS 변수 ++
			InterlockedIncrement(pAcceptTPS);

			// accept 실패할 경우 에러 처리 함수인 OnError 호출해준다.			
			if (client == INVALID_SOCKET)
			{
				// 서버 종료 절차로 리슨소켓을 close했다면 Accept 스레드 종료
				DWORD error = WSAGetLastError();
				if (error == WSAEINTR || error == WSAENOTSOCK)
				{
					// 테스트용 콘솔 출력 나중에 지워야 함
					_tprintf_s(_T("[SERVER] SERVER OFF! ACCEPT THREAD EXIT [ID %d]\n"), GetCurrentThreadId());

					return 0;
				}

				pThis->OnError(WSAGetLastError(), _T("Accept Failed"));
				continue;
			}

			//////////////////////////////////////////////////////////////////////////////////
			// 2. 접속요청한 세션의 유효성 확인
			//////////////////////////////////////////////////////////////////////////////////

			// 현재 접속 중인 세션이 최대 세션에 도달했다면 연결 끊는다.
			if (*pSessionCount == MaxSessions)
			{
				closesocket(client);
				client = INVALID_SOCKET;
				pThis->OnError(GGM_ERROR::TOO_MANY_CONNECTION, _T("Too Many Connection Error Code[%d]"));
				continue;
			}

			// Accept 직후 접속처리 할 수 있는 클라이언트인지 확인
			if ((pThis->OnConnectionRequest(ClientAddr)) == false)
			{
				// 접속 불가능한 클라이언트라면 closesocket()
				// 아직 비동기 I/O 등록전이므로 바로 closesocket 호출 가능
				closesocket(client);
				client = INVALID_SOCKET;
				pThis->OnError(GGM_ERROR::ONCONNECTION_REQ_FAILED, _T("OnConnectionRequest false Error Code[%d]"));
				continue;
			}

			//////////////////////////////////////////////////////////////////////////////////
			// 3. 유효한 세션이므로 세션 관리 배열에 세션 추가
			//////////////////////////////////////////////////////////////////////////////////		

			// 해당 세션 정보를 삽입할 배열의 인덱스를 얻어온다.
			WORD  SessionSlot;
			bool bSuccess = pNextSlot->Pop(&SessionSlot);

			if (bSuccess == false)
			{
				closesocket(client);
				client = INVALID_SOCKET;
				pThis->OnError(0, _T("Index Stack Pop Failed"));
				continue;
			}			

			// 세션 정보 채운다.				
			// SessionID 정보에 해당 세션의 인덱스를 비트연산으로 저장해둔다.			
			// 삭제되자마자 연결된 세션은 다른 스레드에 의해 지워질 위험이 있기 때문에 그에 대한 방어막으로 I/O Count를 증가시켜준다.	
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

			// IOCP 와 소켓 연결 
			HANDLE hRet = CreateIoCompletionPort((HANDLE)client, hIOCP, (ULONG_PTR)&(SessionArray[SessionSlot]), 0);

			if (hRet != hIOCP)
			{
				pThis->OnError(GetLastError(), _T("CreateIoCompletionPort Failed"));
				closesocket(client);
				client = INVALID_SOCKET;
				continue;
			}

			// 현재 접속 클라이언트 수 증가			
			InterlockedIncrement(pSessionCount);

			//////////////////////////////////////////////////////////////////////////////////
			// 4. 접속완료 후 컨텐츠 계층에서 해야할 작업 수행
			//////////////////////////////////////////////////////////////////////////////////

			// 접속완료 이벤트 핸들링 함수 호출	
			pThis->OnClientJoin(ClientAddr, SessionArray[SessionSlot].SessionID);												

			//////////////////////////////////////////////////////////////////////////////////
			// 5. RecvPost 등록
			//////////////////////////////////////////////////////////////////////////////////

			// 정상적으로 OnClientJoin까지 되면 RecvPost 등록
			// 최초 RecvPost는 AcceptThread에서 등록해주어야 함						
			pThis->RecvPost(&SessionArray[SessionSlot]);
			pThis->SessionReleaseLock(&SessionArray[SessionSlot]);			
		}

		return 0;
	}

	unsigned int __stdcall GGM::CNetServer::WorkerThread(LPVOID Param)
	{
		// Worker 스레드 함수가 static이기 때문에 멤버변수에 접근하거나 멤버함수를 호출하기 위해 
		// this 포인터를 인자로 받아온다.
		CNetServer *pThis = (CNetServer*)Param;

		// IOCP 핸들
		HANDLE hIOCP = pThis->m_hWorkerIOCP;

		// 더미 오버랩드 구조체 포인터
		OVERLAPPED *pDummyOverlapped = &pThis->m_DummyOverlapped;

		LONG64 *pSendTPS = &(pThis->m_SendTPS);				
	
		// 루프 돌면서 작업 한다.
		while (true)
		{
			// 완료통지 후 결과를 받을 변수들을 초기화 한다.
			DWORD BytesTransferred; // 송수신 바이트 수
			NetSession *pSession; // 컴플리션 키로 전달한 세션 구조체 포인터
			OVERLAPPED *pOverlapped; // Overlapped 구조체 포인터의 포인터		

			// GQCS를 호출해서 일감이 오기를 대기한다.			
			bool bOk = GetQueuedCompletionStatus(hIOCP, &BytesTransferred, (PULONG_PTR)&pSession, (LPOVERLAPPED*)&pOverlapped, INFINITE);

			// 워커 스레드 일 시작했음.
			//pThis->OnWorkerThreadBegin();		

			if (bOk == false)
			{
				if (GetLastError() == 121)
				{
					pThis->OnSemError(pSession->SessionID);
				}
			}

			//SendPacket함수 내부에서 PQCS로 SendPost요청 했다면 이 곳에서 처리 
			if (pOverlapped == pDummyOverlapped)
			{
				if (pSession->SendQ.size() > 0)
				{
					pThis->SendPost(pSession);
				}	

				// 세션 동기화를 위해 IOCount올렸던 것을 이 곳에서 차감				
				pThis->SessionReleaseLock(pSession);
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
			if (BytesTransferred == 0 || (pSession->IsCanceled == true))
			{				
				pThis->SessionReleaseLock(pSession);
				continue;
			}

			// 완료된 I/O가 RECV인지 SEND인지 판단한다.
			// 오버랩드 구조체 포인터 비교
			if (pOverlapped == &(pSession->RecvOverlapped))
			{
				//////////////////////////////////////////////////////
				// 완료된 I/O가 RECV 일 경우
				//////////////////////////////////////////////////////

				// 완료통지로 받은 바이트 수만큼 Rear를 옮겨준다.				
				pSession->RecvQ.RelocateWrite(BytesTransferred);				

				// 네트워크 클래스 계층의 완성된 패킷이 있는지 확인
				// CheckPacket 내부에서 모든 완료된 패킷을 OnRecv로 전달
				if (pThis->CheckPacket(pSession) == false)
				{
					// 수신한 패킷을 확인하는 도중에 이상한 패킷 수신을 확인했다면 바로 연결끊고 false 리턴
					// I/O Count 차감하고 Recv 걸지 않는다.
					pThis->SessionReleaseLock(pSession);
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

				ULONG PacketCount = pSession->SentPacketCount;
				InterlockedAdd64(pSendTPS, PacketCount);
				CNetPacket **PacketArray = pSession->PacketArray;

				// 보내고 끊기 확인 
				if (pSession->IsSendAndDisconnect == true)
				{
					CNetPacket* pDisconnectPacket = pSession->DisconnectPacket;					

					// 보내고 끊기 플래그가 true라면 현재 도착한 완료통지에 해당 패킷이 포함되었는지 확인
					for (ULONG i = 0; i < PacketCount; i++)
					{
						if (PacketArray[i] == pDisconnectPacket)
						{
							// 현재 도착한 완료통지에 보내고 끊기에 해당하는 패킷이 존재한다면 해당세션과의 연결종료
							pThis->Disconnect(pSession->SessionID);
							break;
						}
					}					
				}						

				// 비동기 I/O로 요청시 보관해 둔 직렬화 버퍼를 정리해준다.				
				for (ULONG i = 0; i < PacketCount; i++)
					CNetPacket::Free(PacketArray[i]);

				pSession->SentPacketCount = 0;

				// 2. 요청한 비동기 Send가 완료되었으므로 이제 해당 세션에 대해 Send할 수 있게 되었음
				pSession->IsSending = FALSE;

				// 3. 아직 보내지 못한 것이 있는지 확인해서 있다면 보냄
				if (pSession->SendQ.size() > 0)
				{
					pThis->SendPost(pSession);					
				}
					
			}

			// I/O Count는 일반적으로 이곳에서 감소시킨다. ( 오류가 났을 경우는 오류가 발생한 곳에서 감소 시킴 )
			// 감소결과 I/O COUNT가 0이 되면 세션을 릴리즈한다. 				
			// 0으로 만든 스레드가 그 세션을 릴리즈 해야 한다.	
			pThis->SessionReleaseLock(pSession);

			// 워커 스레드 일 끝났음
			//pThis->OnWorkerThreadEnd();
		}

		return 0;
	}

	bool GGM::CNetServer::CheckPacket(NetSession * pSession)
	{
		CRingBuffer *pRecvQ = &(pSession->RecvQ);

		// 현재 RECV 링버퍼 사용량 체크
		int CurrentUsage = pRecvQ->GetCurrentUsage();
		int Result;

		// 링버퍼에 최소한 헤더길이 이상이 있는지 먼저확인한다.
		// 헤더길이 이상이 있으면 루프돌면서 존재하는 완성된 패킷 다 뽑아서 OnRecv에 전달
		LONG64 PacketCount = 0;
		while (CurrentUsage >= NET_HEADER_LENGTH)
		{			
			NET_HEADER Header;

			// 해당 세션 링버퍼에서 헤더만큼 PEEK
			Result = pRecvQ->Peek((char*)&Header, NET_HEADER_LENGTH);

			if (Result != NET_HEADER_LENGTH)
			{				
				OnError(GGM_ERROR::BUFFER_READ_FAILED, _T("[CNetServer] NetHeader Peek Failed!!"));			
				return false;				
			}

			CurrentUsage -= NET_HEADER_LENGTH;

			// 패킷 코드가 올바르지 않다면 해당 세션 연결종료
			if (Header.PacketCode != CNetPacket::PacketCode)
			{				
				TCHAR SessionIP[IP_LEN];
				InetNtop(AF_INET, &(pSession->SessionIP), SessionIP, IP_LEN);
				CLogger::GetInstance()->Log(_T("CNetServer Log"), LEVEL::ERR, OUTMODE::FILE, _T("WRONG PACKET CODE[%s:%hd]"), SessionIP, htons(pSession->SessionPort));
				Disconnect(pSession->SessionID);
				return false;
			}

			// 컨텐츠 페이로드 사이즈가 직렬화 버퍼의 크기를 초과한다면 연결끊는다.
			if (Header.Length > DEFAULT_BUFFER_SIZE - NET_HEADER_LENGTH)
			{
				TCHAR SessionIP[IP_LEN];
				InetNtop(AF_INET, &(pSession->SessionIP), SessionIP, IP_LEN);
				CLogger::GetInstance()->Log(_T("CNetServer Log"), LEVEL::ERR, OUTMODE::FILE, _T("TOO BIG PACKET RECV[%s:%hd]"), SessionIP, htons(pSession->SessionPort));
				Disconnect(pSession->SessionID);
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

				Disconnect(pSession->SessionID);
				CNetPacket::Free(pPacket);
				return false;
			}

			// 완성된 패킷이 존재하므로 OnRecv에 완성 패킷을 전달한다.
			OnRecv(pSession->SessionID, pPacket);

			// 현재 링버퍼의 사용 사이즈를 패킷 사이즈만큼 감소
			CurrentUsage -= Header.Length;

			// Alloc에 대한 프리
			CNetPacket::Free(pPacket);

			PacketCount++;
		}

		InterlockedAdd64(&m_RecvTPS, PacketCount);

		return true;
	}

	bool GGM::CNetServer::SendPost(NetSession * pSession)
	{
		///////////////////////////////////////////////////////////////////
		// 0. Send 가능 플래그 체크 
		///////////////////////////////////////////////////////////////////		
		while (InterlockedExchange(&(pSession->IsSending), TRUE) == FALSE)
		{
			CLockFreeQueue<CNetPacket*> *pSendQ = &(pSession->SendQ);

			// 현재 SendQ 사용량을 확인해서 보낼 것이 있는지 다시 한번 확인해본다.
			// 보낼 것이 있는줄 알고 들어왔는데 보낼 것이 없을 수도 있다.
			ULONG CurrentUsage = pSendQ->size();

			// 보낼것이 없다면 Send 플래그 다시 바꾸어 주고 리턴
			if (CurrentUsage == 0)
			{
				// 만약 이 부분에서 플래그를 바꾸기전에 컨텍스트 스위칭이 일어난다면 문제가 된다.
				// 다른 스레드가 보낼 것이 있었는데 플래그가 잠겨있어서 못 보냈을 수도 있다. 

				pSession->IsSending = FALSE;

				// 만약 위에서 문제가 발생했다면 이 쪽에서 보내야 한다.
				// 그렇지 않으면 아무도 보내지 않는 상황이 발생한다.
				if (pSendQ->size() > 0)
					continue;

				return true;
			}

			///////////////////////////////////////////////////////////////////
			// 1. WSASend용 Overlapped 구조체 초기화
			///////////////////////////////////////////////////////////////////
			ZeroMemory(&(pSession->SendOverlapped), sizeof(OVERLAPPED));

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
			CNetPacket **PacketArray = pSession->PacketArray;

			// SendQ에서 한번에 송신 가능한 패킷외 최대 개수만큼 직렬화 버퍼의 포인터를 Dequeue한다.		
			// 세션별로 가지고 있는 직렬화 버퍼 포인터 배열에 그것을 복사한다.
			// Peek을 한 후 완료통지 이후에 Dequeue를 하면 memcpy가 추가적으로 일어나므로 메모리를 희생해서 횟수를 줄였다.			

			// 현재 큐에 들어있는 포인터의 개수가 최대치를 초과했다면 보정해준다.
			if (CurrentUsage > MAX_PACKET_COUNT)
				CurrentUsage = MAX_PACKET_COUNT;

			for(ULONG i=0;i<CurrentUsage;i++)
				pSession->SendQ.Dequeue(&PacketArray[i]);

			// 보낸 패킷의 개수를 기억했다가 나중에 완료통지가 오면 메모리를 해제해야 한다.			
			DWORD PacketCount = pSession->SentPacketCount = CurrentUsage;		

			// 패킷 개수만큼 반복문 돌면서 실제 직렬화 버퍼 내의 패킷 버퍼 포인터를 WSABUF구조체에 저장
			for (DWORD i = 0; i < PacketCount; i++)
			{
				wsabuf[i].buf = (char*)PacketArray[i]->m_pSerialBuffer;
				wsabuf[i].len = PacketArray[i]->m_Rear;
			}

			///////////////////////////////////////////////////////////////////
			// 3. WSASend 등록하기 전에 I/O 카운트 증가
			///////////////////////////////////////////////////////////////////			
			InterlockedIncrement(&(pSession->IoCount));

			///////////////////////////////////////////////////////////////////
			// 4. WSASend 요청
			///////////////////////////////////////////////////////////////////			
			DWORD bytesSent = 0;
			int Result = WSASend(pSession->socket, wsabuf, PacketCount, &bytesSent, 0, &(pSession->SendOverlapped), nullptr);	
	
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
					SessionReleaseLock(pSession);
				}
			}	

			// Disconnect 함수가 호출되어서 I/O가 모두 취소되어야한다면 방금 이 워커스레드가 요청한 I/O를 취소한다.
			if (pSession->IsCanceled == true)
				CancelIoEx((HANDLE)pSession->socket, nullptr);

			return true;
		}

		return true;
	}

	bool GGM::CNetServer::RecvPost(NetSession * pSession)
	{
		///////////////////////////////////////////////////////////////////
		// 0. I/O 요청을 하기 전에 현재 링버퍼의 여유공간을 검사한다.
		///////////////////////////////////////////////////////////////////

		CRingBuffer *pRecvQ = &(pSession->RecvQ);
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

			// 해당 세션 연결종료
			OnError(GGM_ERROR::BUFFER_FULL, _T("[CNetServer] RecvPost() RecvQ CurrentSpare == 0 ErrorCode[%d]"));
			Disconnect(pSession->SessionID);
			return true;
		}

		///////////////////////////////////////////////////////////////////
		// 1. WSARecv용 Overlapped 구조체 초기화
		///////////////////////////////////////////////////////////////////

		ZeroMemory(&(pSession->RecvOverlapped), sizeof(OVERLAPPED));

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
		InterlockedIncrement(&(pSession->IoCount));

		///////////////////////////////////////////////////////////////////
		// 4. I/O 요청
		///////////////////////////////////////////////////////////////////
		DWORD BytesRead = 0;
		DWORD Flags = 0;		
		int Result = WSARecv(pSession->socket, wsabuf, RecvBufCount, &BytesRead, &Flags, &(pSession->RecvOverlapped), nullptr);

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
				SessionReleaseLock(pSession);
			}
		}

		// Disconnect 함수가 호출되어서 I/O가 모두 취소되어야한다면 방금 이 워커스레드가 요청한 I/O를 취소한다.
		if (pSession->IsCanceled == true)
			CancelIoEx((HANDLE)pSession->socket, nullptr);

		return true;
	}

	void GGM::CNetServer::ReleaseSession(NetSession * pSession)
	{
		// ReleaseFlag 가 TRUE가 되면 어떤 스레드도 이 세션에 접근하거나 릴리즈 시도를 해서는 안된다.
		// IoCount와 ReleaseFlag가 4바이트씩 연달아서 위치해 있으므로 아래와 같이 인터락함수 호출한다.
		// IoCount(LONG) == 0 ReleaseFlag(LONG) == 0 >> 인터락 성공시 >>IoCount(LONG) == 0 ReleaseFlag(LONG) == 1
		if (InterlockedCompareExchange64((volatile LONG64*)&(pSession->IoCount), 0x0000000100000000, FALSE) != FALSE)			
			return;								
		
		ULONGLONG SessionID = pSession->SessionID;			
		pSession->SessionID = INIT_SESSION_ID;		

		// 만약 직렬화 버퍼를 Alloc 후 Send하는 도중에 오류가나서 Release를 해야한다면
		// Free해 주어야 한다.
		ULONG PacketCount = pSession->SentPacketCount;
		CNetPacket **PacketArray = pSession->PacketArray;		
		for (WORD i = 0; i < PacketCount; i++)
			CNetPacket::Free(PacketArray[i]);		

		// SendQ 내에 남은 찌꺼기 제거		
		ULONG GarbagePacketCount = pSession->SendQ.size();		
		for (ULONG i = 0; i < GarbagePacketCount; i++)				
		{
			CNetPacket *pGarbagePacket;
			pSession->SendQ.Dequeue(&pGarbagePacket);
			CNetPacket::Free(pGarbagePacket);
		}
	
		// 소켓 정리		
		closesocket(pSession->socket);
		pSession->socket = INVALID_SOCKET;		
	
		// 해당 세션에 대한 모든 리소스가 정리되었으므로 이벤트 핸들링 함수 호출
		OnClientLeave(SessionID);	

		// 해당 세션의 인덱스를 스택에 넣는다. ( 사용 가능 인덱스가 저장된 스택 )		
		m_NextSlot->Push(pSession->SessionSlot);

		// 현재 접속 중인 클라이언트 수 감소
		InterlockedDecrement(&m_SessionCount);			
	}

	bool GGM::CNetServer::SessionAcquireLock(NetSession * pSession, ULONGLONG LocalSessionID)
	{
		// 이 함수를 호출했다는 것은 이후 로직에서 해당 세션을 사용하겠다는 의미이다.				
		// 이 세션이 현재 어떤 상태인지는 모르지만 세션에 접근한다는 의미로, I/O Count를 증가시킨다.
		// I/O Count를 먼저 증가시켰으니 릴리즈 되는 것을 막을 수 있다.
		// 만약 I/O Count를 증가시켰는데 1이라면 더 이상의 세션 접근은 무의미하다.
		ULONGLONG RetIOCount = InterlockedIncrement(&(pSession->IoCount));	

		if (RetIOCount == 1 || pSession->IsReleased == TRUE || pSession->SessionID != LocalSessionID)
			return false;

		return true;
	}

	void GGM::CNetServer::SessionReleaseLock(NetSession * pSession)
	{
		// 세션에 대한 접근이 모두 끝났으므로 이전에 올린 I/O 카운트를 감소 시킨다.			
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

		// BindIP를 nullptr로 설정하면 INADDR_ANY로 바인딩
		// 바인딩할 주소를 설정했다면 해당주소로 주소 지정
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

		// 최대 세션 개수 만큼 배열 동적할당
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

		// 세션 배열 인덱스를 저장할 스택
		m_NextSlot = new CLockFreeStack<WORD>(MaxSessions);

		if (m_NextSlot == nullptr)
		{
			OnError(GetLastError(), _T("MEM Alloc Failed"));
			return false;
		}

		// 처음이니까 모든 인덱스를 넣어준다.	
		for (WORD i = 0; i < (MaxSessions); i++)
		{
			m_NextSlot->Push(i);
		}
		
		return true;
	}

	bool CNetServer::ThreadInit(DWORD ConcurrentThreads, DWORD MaxThreads)
	{
		// 워커스레드와 연동될 IOCP 생성
		m_hWorkerIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, ConcurrentThreads);

		if (m_hWorkerIOCP == NULL)
		{
			OnError(GetLastError(), _T("CreateIoCompletionPort Failed"));
			return false;
		}

		// 생성된 스레드의 핸들을 저장할 배열 
		m_ThreadHandleArr = new HANDLE[MaxThreads + 1];

		if (m_ThreadHandleArr == nullptr)
		{
			OnError(GetLastError(), _T("MEM Alloc Failed"));
			return false;
		}

		// 생성된 스레드의 아이디를 저장할 배열
		m_ThreadIDArr = new DWORD[MaxThreads + 1];

		if (m_ThreadIDArr == nullptr)
		{
			OnError(GetLastError(), _T("MEM Alloc Failed"));
			return false;
		}		

		// 사용자가 요청한 개수의 워커 스레드(동시실행 개수 아님) 생성
		for (DWORD i = 0; i < MaxThreads; i++)
		{
			m_ThreadHandleArr[i] = (HANDLE)_beginthreadex(
				nullptr,
				0,
				CNetServer::WorkerThread, // 워커 스레드 함수 클래스 내부에 private static으로 선언되어있음
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
		
		m_ThreadHandleArr[MaxThreads] = (HANDLE)_beginthreadex(
			nullptr,
			0,
			CNetServer::AcceptThread, // Accept 스레드 함수 클래스 내부에 private static으로 선언되어있음
			this, // Accept 스레드 함수가 static으로 선언되어있으므로 다른 멤버 변수나 함수에 접근하기 위해서 해당 인스턴스의 this 포인터가 필요함
			0,
			(unsigned int*)&m_ThreadIDArr[MaxThreads]
		);

		if (m_ThreadHandleArr[MaxThreads] == NULL)
		{
			OnError(GetLastError(), _T("_beginthreadex Failed"));
			return false;
		}	

		// 생성한 워커 스레드 개수 저장한다
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
		// 서버 종료 절차 
		// 1. 더 이상 accept를 받을 수 없도록 리슨소켓을 닫는다.
		// 2. 모든 세션에 FIN을 보낸다.		
		// 3. 모든 세션이 I/O COUNT가 0이 되고 Release될 때까지 대기한다.
		// 4. 워커스레드 종료시킨다.
		// 5. 서버에서 사용했던 기타 리소스들을 정리한다.
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

		// 세션이 전부 정리될 때까지 대기한다.
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
		// 각 세션별로 저장된 세션 아이디의 최하위 2바이트에는 해당 세션이 저장된 배열의 인덱스가 셋팅되어있다.	
		WORD SessionSlot = ((WORD)SessionID);
		NetSession *pSession = &m_SessionArray[SessionSlot];			

		// 단순 연결을 끊는 것과 세션을 릴리즈 하는 것은 다른 것이다.
		// 요청된 I/O를 취소하여 자연스럽게 I/O Count가 0이 되도록 유도하여 세션을 릴리즈한다.
		do
		{
			// 세션에 접근을 시도한다.
			// 세션 접근에 실패하면 그냥 나간다.
			if (SessionAcquireLock(pSession, SessionID) == false)
				break;	
			
			pSession->IsCanceled = true;
			CancelIoEx((HANDLE)pSession->socket, nullptr);

		} while (0);

		// 세션에 접근을 완료했음을 알림		
		SessionReleaseLock(pSession);
		return true;
	}

	bool GGM::CNetServer::SendPacket(ULONGLONG SessionID, CNetPacket * pPacket)
	{
		// 각 세션별로 저장된 세션 아이디의 최하위 2바이트에는 해당 세션이 저장된 배열의 인덱스가 셋팅되어있다.	
		WORD SessionSlot = (WORD)SessionID;
		NetSession *pSession = &m_SessionArray[SessionSlot];
		bool Result;						

		do
		{
			// 세션에 접근을 시도한다.
			// 세션 접근에 실패하면 (이미 스레드가 릴리즈 중이라면) 그냥 나간다.
			if (SessionAcquireLock(pSession, SessionID) == false)
			{
				Result = true;
				break;
			}		

			// 패킷을 인코딩한다.
			pPacket->Encode();		

			// SendQ에 패킷의 포인터를 담는다.	
			InterlockedIncrement(&(pPacket->m_RefCnt));	
			Result = pSession->SendQ.Enqueue(pPacket);
			if (Result == false)
			{
				// SendQ에 Enqueue 실패는 있어서는 안되는 일이다. 
				// 바로 서버 죽임.
				CCrashDump::ForceCrash();
			}		

			// SendFlag를 확인해보고 Send중이 아니라면 PQCS로 WSASend 요청
			// 이렇게 함으로써, 컨텐츠에서 WSASend를 호출하여 업데이트가 느려지는 것을 막을 수 있다.
			// 그러나 패킷 응답성은 떨어짐
			if (InterlockedOr(&pSession->IsSending, 0) == TRUE)
				break;			
	
			PostQueuedCompletionStatus(m_hWorkerIOCP, 0, (ULONG_PTR)pSession, &m_DummyOverlapped);	

			// PQCS를 하고나서는 IOCount를 바로 깍지 않고 완료통지에서 깎는다.					
			return true;		

		} while (0);	

		// 세션에 접근을 완료했음을 알림		
		SessionReleaseLock(pSession);

		return Result;
	}

	bool CNetServer::SendPacketAndDisconnect(ULONGLONG SessionID, CNetPacket * pPacket)
	{
		// 패킷을 송신하고 나서 연결을 끊는다.
		// 각 세션별로 저장된 세션 아이디의 최하위 2바이트에는 해당 세션이 저장된 배열의 인덱스가 셋팅되어있다.	
		WORD SessionSlot = (WORD)SessionID;
		NetSession *pSession = &m_SessionArray[SessionSlot];
		bool Result;		

		do
		{
			// 세션에 접근을 시도한다.
			// 세션 접근에 실패하면 (이미 스레드가 릴리즈 중이라면) 그냥 나간다.
			if (SessionAcquireLock(pSession, SessionID) == false)
			{
				Result = true;
				break;
			}

			// 패킷을 인코딩한다.
			pPacket->Encode();

			// SendQ에 패킷의 포인터를 담는다.	
			InterlockedIncrement(&(pPacket->m_RefCnt));
			Result = pSession->SendQ.Enqueue(pPacket);
			if (Result == false)
			{
				// SendQ에 Enqueue 실패는 있어서는 안되는 일이다. 
				// 바로 서버 죽임.
				CCrashDump::ForceCrash();
			}
			
			// 해당 패킷의 완료통지가 도착하면 연결을 끊을 수 있도록 플래그를 켠다.
			// 나중에 완료통지에서 확인할 필요가 있으므로 해당 패킷의 포인터를 저장한다.
			pSession->DisconnectPacket = pPacket;
			pSession->IsSendAndDisconnect = true;	

			// SendFlag를 확인해보고 Send중이 아니라면 PQCS로 WSASend 요청
			// 이렇게 함으로써, 컨텐츠에서 WSASend를 호출하여 업데이트가 느려지는 것을 막을 수 있다.
			// 그러나 패킷 응답성은 떨어짐
			if (InterlockedOr(&pSession->IsSending, 0) == TRUE)
				break;

			PostQueuedCompletionStatus(m_hWorkerIOCP, 0, (ULONG_PTR)pSession, &m_DummyOverlapped);		

			// PQCS를 하고나서는 IOCount를 바로 깍지 않고 완료통지에서 깎는다.			
			return true;		

		} while (0);

		// 세션에 접근을 완료했음을 알림		
		SessionReleaseLock(pSession);

		return Result;
	}
}
