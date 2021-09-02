#include "CMMOSession.h"
#pragma comment(lib, "Winmm.lib")

namespace GGM
{
	//////////////////////////////////////////////////////////////////////////////
	// MMO 게임서버에 특화된 구조의 CMMOServer Class 
	// * CNetServer와 달리 네트워크 송수신 스레드뿐만 아니라 컨텐츠 처리 스레드도 제공
	// * Auth 스레드, Game 스레드가 컨텐츠 처리를 도맡아 진행함
	// * 네트워크의 세션 객체와 컨텐츠의 플레이어 객체가 상속관계로 묶임 
	// * CNetServer와 달리 스레드별로 세션 접근을 제한하여 동기화 로직이 단순해짐
	// * 최종 릴리즈는 한 곳에서만 담당 ( Game Thread )	
	//////////////////////////////////////////////////////////////////////////////
	class CMMOServer
	{
	public:

		// 모니터링용 변수들
		ULONGLONG m_AcceptTotal = 0;
		ULONGLONG m_AcceptTPS = 0;

		// Auth 와 Game Loop per Sec
		LONG      m_AuthLoopPerSec = 0;
		LONG      m_AuthLoopPerSecMonitor = 0;
		LONG      m_GameLoopPerSec = 0;
		LONG      m_GameLoopPerSecMonitor = 0;

		//Recv, Send TPS
		LONG      m_RecvTPS = 0;
		LONG      m_SendTPS = 0;

		// SessionCount		
		LONG      m_AuthSession = 0;		
		LONG      m_GameSession = 0;	
		LONG      m_AuthTimeoutDisconnectCount = 0;
		LONG      m_GameTimeoutDisconnectCount = 0;

	public:

		// 기본적으로 모든 초기화와 정리는 Start와 Stop 함수에서 실시할 것이기 때문에 생성자와 소멸자는 쓰지 않음
		CMMOServer() = default;
		virtual ~CMMOServer() = default;
		
		////////////////////////////////////////////////////////////
		// Start
		// 인자
		// * TCHAR * BindIP [ 서버의 Bind IP ] 
		// * USHORT port    [ 서버 포트 ]
		// * DWORD ConcurrentThreads [ IOCP와 연계된 워커스레드 동시실행 개수 ]
		// * DWORD MaxThreads [ IOCP와 연계된 워커스레드 최대 생성 개수 ]
		// * bool IsNoDelay [ TCP_NODELAY 옵션 플래그 ]
		// * WORD MaxSessions [ 최대 동시 접속자 수 ]
		// * BYTE PacketCode [ 패킷 유효성 검사 코드 ]
		// * BYTE PacketKey [ 패킷 암호화 대칭키 ]		
		// * CMMOSession *pSessionArr [컨텐츠에서 생성한 플레이어 객체배열의 포인터]	
		// * int          PlayerSize  [컨텐츠에서 생성한 플레이어 객체의 사이즈] 
		// * WORD         SendSleepTime [Send Thread Loop Sleep Time]	
		// * WORD         AuthSleepTime [Auth Thread Loop Sleep Time]	
		// * WORD         GameSleepTime [Game Thread Loop Sleep Time]	
		// * WORD         AuthPacketPerLoop [ Auth 스레드 한 루프에 세션당 처리 패킷 개수 ]
		// * WORD         GamePacketPerLoop [ Game 스레드 한 루프에 세션당 처리 패킷 개수 ]
		// * WORD         AcceptPerLoop [ Auth 스레드가 한 루프에 Accept 처리할 개수 ]
		// * WORD         AuthToGamePerLoop [ Auth 스레드가 한 루프에 Game Mode로 이관할 개수 ]
		// * ULONGLONG    TimeoutLimit [ 세션 타임아웃 처리 시간 ]
		// * bool         IsTimeoutOn [ 세션 타임아웃 기능 사용 여부 ]
		// 반환 : 없음
		// 기능 : 서버 구동 
		////////////////////////////////////////////////////////////
		bool    Start(
			TCHAR		*BindIP,
			USHORT		 port,
			DWORD		 ConcurrentWorkerThreads,
			DWORD		 MaxWorkerThreads,
			bool		 IsNoDelay,
			WORD		 MaxSessions,
			BYTE		 PacketCode,
			BYTE		 PacketKey,
			CMMOSession *pPlayerArr,
			int          PlayerSize,
			WORD         SendSleepTime,
			WORD         AuthSleepTime,
			WORD         GameSleepTime,
			WORD         ReleaseSleepTime,
			WORD         AuthPacketPerLoop,
			WORD         GamePacketPerLoop,
			WORD         AcceptPerLoop,
			WORD         AuthToGamePerLoop,
			ULONGLONG    TimeoutLimit,
			bool         IsTimeoutOn
		);

		////////////////////////////////////////////////////////////
		// Stop
		// 인자 : 없음
		// 반환 : 없음
		// 기능 : 서버 구동 정지
		////////////////////////////////////////////////////////////
		void Stop();

		///////////////////////////////////////////////////////////////////////////////////
		// GetSessionCount 
		// 인자  : 없음
		// 반환  : ULONGLONG
		// 기능  : 현재 접속중인 세션 수 반환	
		///////////////////////////////////////////////////////////////////////////////////
		ULONGLONG GetSessionCount() const;

	protected:

		////////////////////////////////////////////////////////////
		// 이벤트 핸들러 순수 가상 함수 
		////////////////////////////////////////////////////////////

		// 기본적으로 세션과 관련된 이벤트 핸들러들은 CMMOSession에 존재
		// 세션과 독립적인 스레드별 Update는 이곳에서 처리 
		// 클라이언트의 요청과는 독립적인 게임 내의 컨텐츠 처리를 위한 함수 
		virtual void OnAuth_Update() = 0;
		virtual void OnGame_Update() = 0;

		// 내부적으로 진행되는 함수에서 오류가 발생할 시 본 함수를 호출해서 알려줌 
		virtual void OnError(int ErrorNo, const TCHAR* ErrorMsg) = 0;	
		virtual void OnSemError(USHORT Slot) = 0;
		virtual void OnTimeOut(ULONGLONG SessionID, WORD SessionIndex, ULONGLONG Timeout) = 0;

	private:

		////////////////////////////////////////////////////////////
		// Worker, Accept, Auth, Game 스레드 프로 시저
		////////////////////////////////////////////////////////////

		static unsigned int __stdcall WorkerThread(LPVOID Param);
		static unsigned int __stdcall AcceptThread(LPVOID Param);
		static unsigned int __stdcall SendThread(LPVOID Param);
		static unsigned int __stdcall AuthThread(LPVOID Param);
		static unsigned int __stdcall GameThread(LPVOID Param);		

		///////////////////////////////////////////////////////////////////////////////////
		// CheckPacket 
		// 인자  : CMMOSession *pSession [ 세션 구조체 포인터 ]
		// 반환  : bool ( 수신받은 패킷처리가 성공했을시 true, 문제가 발견되면 false)
		// 기능 
		// * 네트워크 클래스 계층의 완성된 패킷이 존재하면 헤더를 떼고 패킷큐에 넣음		
		// * 수신한 패킷에서 문제가 발견되면 해당 세션 연결 끊고 릴리즈 절차 진행
		///////////////////////////////////////////////////////////////////////////////////
		bool CheckPacket(CMMOSession *pSession);

		///////////////////////////////////////////////////////////////////////////////////
		// SendPost
		// 인자  : CMMOSession *pSession [ 세션 구조체 포인터 ]
		// 반환  : 성공시 true, 실패시 false 
		// 기능  : WSASend 등록 
		// 참고  : SendThread에서 호출
		///////////////////////////////////////////////////////////////////////////////////		
		bool SendPost(CMMOSession *pSession);

		///////////////////////////////////////////////////////////////////////////////////
		// RecvPost
		// 인자1 : CMMOSession *pSession [ 세션 구조체 포인터 ]
		// 반환  : 성공시 true, 실패시 false 
		// 기능  : WSARecv 등록
		// 참고  : 비동기 Recv는 항상 등록되어 있다.
		///////////////////////////////////////////////////////////////////////////////////		
		bool RecvPost(CMMOSession *pSession);	

		void ReleaseSession(CMMOSession *pSession);

		bool WSAInit(TCHAR * BindIP, USHORT port, bool IsNoDelay);
		bool SessionInit(WORD MaxSessions, CMMOSession *pPlayerArr, int PlayerSize);
		bool ThreadInit(DWORD ConcurrentThreads, DWORD MaxThreads);

	private:

		// 서버 주소 구조체
		SOCKADDR_IN                   m_ServerAddr;

		// CMMOSession 포인터 배열, 컨텐츠 파트에서 Start 함수로 제공해준 정보로 초기화 됨
		CMMOSession					**m_SessionArr = nullptr;	

		// 워커스레드와 연결될 IOCP의 핸들 [ 동시실행 스레드 개수, 생성 스레드 개수 사용자가 결정 ]
		HANDLE						  m_hWorkerIOCP = NULL;

		// 스레드 핸들  
		HANDLE						 *m_WorkerThreadHandleArr = nullptr;
		HANDLE                        m_AcceptThread = nullptr;
		HANDLE                        m_SendThread[SEND_THREAD_COUNT];
		HANDLE                        m_AuthThread = nullptr;
		HANDLE                        m_GameThread = nullptr;
		HANDLE                        m_ReleaseThread = nullptr;

		// 스레드 아이디
		DWORD						 *m_WorkerThreadIDArr = nullptr;		
		DWORD                         m_AcceptThreadID;
		DWORD                         m_SendThreadID[SEND_THREAD_COUNT];
		DWORD                         m_AuthThreadID;
		DWORD                         m_GameThreadID;
		DWORD                         m_ReleaseThreadID;

		// 하트비트 체크시 타임아웃 기준 시간
		ULONGLONG                     m_TimeoutLimit;
		bool                          m_IsTimeout;

	public:

		// Accept Thread와 Auth Thread 사이의 락프리 큐, 클라이언트가 새로 접속하면 그 정보를 AcceptThread가 인큐		
		CLockFreeQueue<WORD>          *m_AcceptQueue = nullptr;	

		// 삽입가능한 세션의 정보를 포함 				
		CListQueue<WORD>             *m_NextSession = nullptr;

		// 리슨 소켓 
		SOCKET						  m_Listen = INVALID_SOCKET;

		// 현재 총 접속 세션 수 
		ULONGLONG					  m_SessionCount = 0;
	
		// 서버의 최대 접속자 수 
		DWORD						  m_MaxSessions;

		// 생성된 워커 스레드의 개수 
		DWORD						  m_WorkerThreadsCount = 0;

		// 서버 Stop여부
		bool                          m_IsExit = false;

		// 각 스레드 SleepTime
		WORD                          m_SendSleepTime;
		WORD                          m_AuthSleepTime;
		WORD                          m_GameSleepTime;
		WORD                          m_ReleaseSleepTime;

		// 한 루프당 세션별 패킷처리 횟수
		WORD                          m_AuthPacketPerLoop;
		WORD                          m_GamePacketPerLoop;

		// 한 루프당 AcceptQueue에서 Dequeue할 세션 개수 
		WORD                          m_AcceptPerLoop;

		// 한 루프당 Auth To Game 에서 Game으로 바꿀 개수
		WORD                          m_AuthToGamePerLoop;
	};
}

