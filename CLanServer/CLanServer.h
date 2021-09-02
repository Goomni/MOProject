#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <WS2tcpip.h>
#include <winsock2.h>
#include "RingBuffer\RingBuffer.h"
#include "Packet\Packet.h"
#include "MemoryPool\MemoryPool.h"
#include "LockFreeStack\LockFreeStack.h"
#include "LockFreeQueue\LockFreeQueue.h"
#include "Define\GGM_CONSTANTS.h"
#pragma  comment(lib, "Ws2_32.lib")

namespace GGM
{
	////////////////////////////////////////////////////
	// CLanServer 세션 구조체 LanSession			
	////////////////////////////////////////////////////
	struct LanSession
	{
		SOCKET							socket = INVALID_SOCKET;
		ULONGLONG						SessionID; // 세션 접속시 발급받는 유니크한 아이디				
		LONG			                IoCount = 0;
		LONG							IsReleased = FALSE;
		LONG			                IsSending = FALSE; // 샌드 플래그 		
		WORD							SessionSlot; // 세션 관리 배열의 인덱스 			
		bool                            IsCanceled = false;
		ULONG   					    SentPacketCount = 0; // SendPost 내부에서 Send요청한 패킷 개수							         
		OVERLAPPED		                RecvOverlapped; // 수신용 OVERLAPPED 구조체
		OVERLAPPED		                SendOverlapped; // 송신용 OVERLAPPED 구조체		
		CRingBuffer		                RecvQ; // 수신용 링버퍼				 
		CLockFreeQueue<CPacket*>        SendQ; // 송신용 직렬화 버퍼의 포인터를 담을 락프리 큐
		CPacket		                   *PacketArray[MAX_PACKET_COUNT]; // SendQ에서 뽑아낸 직렬화 버퍼의 포인터를 담을 배열			
	};

	///////////////////////////////////////////////////////////////////
	// 내부 네트워크의 서버간 통신에 사용할 클래스 CLanServer
	// - IOCP 사용
	// - 클래스 내부에 워커스레드를 가짐
	// - 이벤트 핸들링 함수는 순수가상함수로 제작하여 외부에서 상속받아 정의
	// - 내부에서 자체적으로 세션을 관리함
	///////////////////////////////////////////////////////////////////
	class CLanServer
	{	
	public:

		// 모니터링용 변수들 
		volatile LONG RecvTps = 0;
		volatile LONG SendTps = 0;
		ULONGLONG AcceptTotal = 0;
		
	public:

		////////////////////////////////////////////////////
		// 네트워크 라이브러리 public 멤버 함수		
		////////////////////////////////////////////////////

		///////////////////////////////////////////////////////////////////////////////////
		// 생성자 
		// 인자1 : 없음
		// 반환  : 없음
		// 기능  : Start 함수에서 초기화에 필요한 모든 정보를 받을 것이므로 생성자에서 할일이 없다.
		// 참고  : 
		///////////////////////////////////////////////////////////////////////////////////
		CLanServer() = default;

		///////////////////////////////////////////////////////////////////////////////////
		// 소멸자 
		// 인자1 : 없음
		// 반환  : 없음
		// 기능  : Stop 함수에서 모든 정리를 시행할 것이므로 소멸자에서 할일이 없다.
		// 참고  : 
		///////////////////////////////////////////////////////////////////////////////////
		virtual ~CLanServer() = default;

		///////////////////////////////////////////////////////////////////////////////////
		// Start 
		// 인자1 : TCHAR* BindIP [ 서버 초기화시 bind 할 IP ]
		// 인자2 : USHORT port [ 서버 초기화시 bind 할 port ] 
		// 인자3 : DWORD ConcurrentThreads [ IOCP가 동시에 실행할 스레드 개수 ] 
		// 인자4 : DWORD MaxThreads [ 생성할 최대 스레드 개수 ]
		// 인자5 : bool IsNagleOn [ TCP_NODELAY 옵션 ON/OFF 여부 ]
		// 인자6 : DWORD MaxSessions [ 최대 접속자 수 ]
		// 반환  : 성공시 true, 실패시 false 혹은, 에러코드(WSAStartup 에러시)
		// 기능  : 서버 구동 및 초기화
		// 참고  : 외부에서 서버 최초 구동시 호출한다.
		///////////////////////////////////////////////////////////////////////////////////
		bool Start(
			TCHAR* BindIP,
			USHORT port,
			DWORD ConcurrentThreads,
			DWORD MaxThreads,
			bool  IsNoDelay,
			WORD  MaxSessions
		);

		///////////////////////////////////////////////////////////////////////////////////
		// Stop 
		// 인자1 : 없음
		// 반환  : 없음
		// 기능  : 서버 구동 정지, 모든 연결 끊고 리소스 정리
		// 참고  : 
		///////////////////////////////////////////////////////////////////////////////////
		void Stop();

		///////////////////////////////////////////////////////////////////////////////////
		// GetSessionCount 
		// 인자1 : 없음
		// 반환  : ULONGLONG
		// 기능  : 현재 접속중인 클라이언트의 수 반환
		// 참고  : 
		///////////////////////////////////////////////////////////////////////////////////
		ULONGLONG GetSessionCount() const;

		///////////////////////////////////////////////////////////////////////////////////
		// Disconnect
		// 인자1 : ULONGLONG SessionID [ 연결을 해제할 세션의 식별자 ]
		// 반환  : true [ 성공 ] // false [ 실패 ]
		// 기능  : 외부에서 SessionID 세션의 연결 종료가 필요할 때 사용 
		// 참고  : 단순 연결 종료, 세션 리소스를 완벽하게 정리하는 것은 ReleaseSession 함수가
		///////////////////////////////////////////////////////////////////////////////////
		bool Disconnect(ULONGLONG SessionID);

		///////////////////////////////////////////////////////////////////////////////////
		// SendPacket
		// 인자1 : ULONGLONG SessionID [ 패킷을 송신할 세션의 식별자 ]
		// 인자2 : CPacket *Packet [ 패킷 내용이 담긴 직렬화 버퍼의 포인터 ]
		// 반환  : true [ 성공 ] // false [ 실패 ]
		// 기능  : 해당 세션에게 패킷을 전송할 때 사용		
		// 참고  : 세션 전용 송신 링버퍼로 패킷 내용 복사가 완료된 이후에는 해당 버퍼 메모리 해제 
		///////////////////////////////////////////////////////////////////////////////////
		bool SendPacket(ULONGLONG SessionID, CPacket *Packet);	

	protected:

		/////////////////////////////////////////////////////////////////
		// * 네트워크 I/O 이벤트 핸들러 		
		/////////////////////////////////////////////////////////////////

		// Accept 성공 후 접속이 확정되면 호출 
		virtual void OnClientJoin(const SOCKADDR_IN& ClientAddr, ULONGLONG SessionID) = 0;

		// 해당 세션에 대한 모든 리소스가 정리되면 호출  
		virtual void OnClientLeave(ULONGLONG SessionID) = 0;

		// Accept 직후 호출 
		virtual bool OnConnectionRequest(const SOCKADDR_IN& ClientAddr) = 0;

		// 패킷 수신 완료 후 호출
		virtual void OnRecv(ULONGLONG SessionID, CPacket *Packet) = 0;

		// 패킷 송신 완료 후 호출
		virtual void OnSend(ULONGLONG SessionID, int SendSize) = 0;

		// 워커스레드 GQCS 바로 하단에서 호출 
		virtual void OnWorkerThreadBegin() = 0;

		// 워커스레드 1루프 종료 후 호출
		virtual void OnWorkerThreadEnd() = 0;

		// 내부적으로 진행되는 함수에서 오류가 발생할 시 본 함수를 호출해서 알려줌 
		virtual void OnError(int ErrorNo, const TCHAR* ErrorMsg) = 0;

	private:

		////////////////////////////////////////////////////
		// 네트워크 라이브러리 private 멤버 함수	
		// 서버가 구동되면 적절한 시점에 자동 호출됨 
		////////////////////////////////////////////////////	

		///////////////////////////////////////////////////////////////////////////////////
		// AcceptThread : 스레드 함수로 등록해야하므로 static
		// 인자1 : LPVOID Param [ CLanServer의 this 포인터 ]
		// 반환  : 스레드 함수 반환 
		// 기능  : accept, 연결 완료 시 세션테이블에 세션 등록
		// 참고  : 관련 이벤트 핸들링 함수 OnConnectionRequest, OnClientJoin
		///////////////////////////////////////////////////////////////////////////////////
		static unsigned int WINAPI AcceptThread(LPVOID Param);

		///////////////////////////////////////////////////////////////////////////////////
		// WorkerThread  : 스레드 함수로 등록해야하므로 static
		// 인자1 : LPVOID Param [ CLanServer의 this 포인터 ]
		// 반환  : 스레드 함수 반환 
		// 기능  : I/O를 담당할 워커 스레드 
		// 참고  : 관련 이벤트 핸들링 함수 OnRecv, OnSend
		///////////////////////////////////////////////////////////////////////////////////
		static unsigned int WINAPI WorkerThread(LPVOID Param);			

		///////////////////////////////////////////////////////////////////////////////////
		// CheckPacket 
		// 인자1 : Session *pSession [ 세션 구조체 포인터 ]
		// 반환  : 없음
		// 기능  : 네트워크 클래스 계층의 완성된 패킷이 존재하면 헤더를 떼고 OnRecv에 전달
		// 참고  : 관련 이벤트 핸들링 함수 OnRecv
		///////////////////////////////////////////////////////////////////////////////////
		void CheckPacket(LanSession *pSession);

		///////////////////////////////////////////////////////////////////////////////////
		// SendPost
		// 인자1 : Session *pSession [ 세션 구조체 포인터 ]
		// 반환  : 성공시 true, 실패시 false 
		// 기능  : WSASend 등록 
		// 참고  : SendPacket시 호출, OnSend 후 호출 
		///////////////////////////////////////////////////////////////////////////////////		
		bool SendPost(LanSession *pSession);

		///////////////////////////////////////////////////////////////////////////////////
		// RecvPost
		// 인자1 : Session *pSession [ 세션 구조체 포인터 ]
		// 반환  : 성공시 true, 실패시 false 
		// 기능  : WSARecv 등록
		// 참고  : 비동기 Recv는 항상 등록되어 있다.
		///////////////////////////////////////////////////////////////////////////////////		
		bool RecvPost(LanSession *pSession);

		///////////////////////////////////////////////////////////////////////////////////
		// ReleaseSession
		// 인자1 : Session *pSession [ 세션 구조체 포인터 ]
		// 반환  : 없음
		// 기능  : 세션 정리
		// 참고  : 이 함수는 I/O Count가 0일 때만 호출되어야 한다.
		///////////////////////////////////////////////////////////////////////////////////	
		void ReleaseSession(LanSession *pSession);

		///////////////////////////////////////////////////////////////////////////////////
		// SessionAcquireLock
		// 인자1 : Session *pSession [ 세션 구조체 포인터 ]
		// 반환  : bool (성공시 true, 실패시 false)
		// 기능  : 멀티 스레드 환경에서 세션을 보호하는 락을 획득 (인터락)		
		///////////////////////////////////////////////////////////////////////////////////	
		bool SessionAcquireLock(LanSession *pSession, ULONGLONG LocalSessionID);

		///////////////////////////////////////////////////////////////////////////////////
		// SessionReleaseLock
		// 인자1 : Session *pSession [ 세션 구조체 포인터 ]
		// 반환  : bool (성공시 true, 실패시 false)
		// 기능  : 멀티 스레드 환경에서 세션을 보호하는 락을 반환 (인터락)	
		///////////////////////////////////////////////////////////////////////////////////	
		void SessionReleaseLock(LanSession *pSession);

		bool WSAInit(TCHAR * BindIP, USHORT port, bool IsNoDelay);
		bool SessionInit(WORD MaxSessions);
		bool ThreadInit(DWORD ConcurrentThreads, DWORD MaxThreads);
	
	private:	

		////////////////////////////////////////////////////
		// 네트워크 라이브러리 private 멤버 변수		
		////////////////////////////////////////////////////
		
		// 워커스레드와 연결될 IOCP의 핸들 [ 동시실행 스레드 개수, 생성 스레드 개수 사용자가 결정 ]
		HANDLE  m_hWorkerIOCP = NULL;	

		// 워커스레드 핸들 배열 [스레드 개수는 사용자가 결정] 
		HANDLE *m_ThreadHandleArr = nullptr;

		// 워커스레드 아이디 배열 [스레드 개수는 사용자가 결정]
		DWORD *m_ThreadIDArr = nullptr;

		// 세션 관리 배열		
		LanSession * m_SessionArray = nullptr;

		// 세션 삽입가능한 배열의 인덱스를 보관하고 있는 스택
		CLockFreeStack<WORD> *m_NextSlot;		

		// 리슨 소켓 
		SOCKET     m_Listen = INVALID_SOCKET;		

		// 현재 총 접속 세션 수 
		ULONGLONG  m_SessionCount = 0;

		// 최대 접속 가능 클라이언트의 수 
		DWORD      m_MaxSessions = 0;

		// 생성된 스레드의 개수 
		DWORD      m_IOCPThreadsCount = 0;	

		// 더미 오버랩드
		OVERLAPPED m_DummyOverlapped;

	};
}


