#ifndef _C_NET_SERVER_H_
#define _C_NET_SERVER_H_

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <WS2tcpip.h>
#include <winsock2.h>
#include <Mstcpip.h>
#include "RingBuffer\RingBuffer.h"
#include "Packet\Packet.h"
#include "MemoryPool\MemoryPool.h"
#include "LockFreeStack\LockFreeStack.h"
#include "LockFreeQueue\LockFreeQueue.h"
#include "Define\GGM_CONSTANTS.h"
#include <unordered_set>
#include <unordered_map>
#pragma  comment(lib, "Ws2_32.lib")

namespace GGM
{
	///////////////////////////////////////////////////////////////////
	// CNetClient와의 통신에 사용할 클래스 CNetServer
	// - 이벤트 핸들러를 보유한 추상 클래스 
	// - 내부에서 자체적으로 세션을 관리함
	// - IOCP와 연계된 워커스레드가 네트워크 I/O 처리
	///////////////////////////////////////////////////////////////////

	////////////////////////////////////////////////////
	// CNetServer 세션 구조체 NetSession			
	////////////////////////////////////////////////////
	struct NetSession
	{		
		SOCKET							socket = INVALID_SOCKET;
		ULONGLONG						SessionID; // 세션 접속시 발급받는 유니크한 아이디		
		LONG			                IoCount = 0;
		LONG							IsReleased = FALSE;					 
		LONG			                IsSending = FALSE; // Send 플래그 	
		ULONG   					    SentPacketCount = 0; // SendPost 내부에서 Send요청한 패킷 개수	
		bool                            IsSendAndDisconnect = false; // 보내고 끊기 플래그 
		bool                            IsCanceled = false;
		CNetPacket                      *DisconnectPacket = nullptr;
		OVERLAPPED		                RecvOverlapped; // 수신용 OVERLAPPED 구조체
		OVERLAPPED		                SendOverlapped; // 송신용 OVERLAPPED 구조체		
		CRingBuffer						RecvQ; // 수신용 링버퍼				 
		CLockFreeQueue<CNetPacket*>     SendQ; // 송신용 락프리 큐
		CNetPacket   		            *PacketArray[MAX_PACKET_COUNT]; // SendQ에서 뽑아낸 직렬화 버퍼의 포인터를 담을 배열		
		IN_ADDR                         SessionIP;
		USHORT                          SessionPort;
		WORD							SessionSlot; // 세션 관리 배열의 인덱스 			
	};

	class CNetServer
	{
	public:	
		// Accept Total Count
		ULONGLONG m_AcceptTotal = 0;

		// Accept TPS
		ULONGLONG m_AcceptTPS = 0;

		// Update TPS
		ULONGLONG m_UpdateTPS = 0;		

		// Send TPS
		LONG64 m_SendTPS = 0;

		// Recv TPS
		LONG64 m_RecvTPS = 0;		

		// Session Key Not Found
		ULONGLONG m_SessionKeyNotFound = 0;

		// Invalid Session Key
		ULONGLONG m_InvalidSessionKey = 0;

	public:		

		// 모든 초기화는 Start함수를 통해, 모든 정리는 Stop함수를 통해 진행
		CNetServer() = default;	
		virtual ~CNetServer() = default;

		///////////////////////////////////////////////////////////////////////////////////
		// Start 
		// 인자1 : TCHAR* BindIP [ 서버 초기화시 bind 할 IP ]
		// 인자2 : USHORT port [ 서버 초기화시 bind 할 port ] 
		// 인자3 : DWORD ConcurrentThreads [ IOCP가 동시에 실행할 스레드 개수 ] 
		// 인자4 : DWORD MaxThreads [ 생성할 최대 스레드 개수 ]
		// 인자5 : bool IsNoDelay [ TCP_NODELAY 옵션 ON/OFF 여부 ]
		// 인자6 : DWORD MaxSessions [ 최대 접속자 수 ]
		// 인자7 : BYTE PacketCode [ 패킷 코드 ]
		// 인자8 : BYTE PacketKey1 [ 패킷 XOR 키]		
		// 반환  : 성공시 true, 실패시 false 
		// 기능  : 서버 구동 및 초기화
		// 참고  : 외부에서 서버 최초 구동시 호출한다.
		///////////////////////////////////////////////////////////////////////////////////
		bool Start(
			TCHAR * BindIP,
			USHORT port,
			DWORD ConcurrentThreads,
			DWORD MaxThreads,
			bool IsNoDelay,
			WORD MaxSessions,
			BYTE PacketCode,
			BYTE PacketKey		
		);

		///////////////////////////////////////////////////////////////////////////////////
		// Stop 
		// 인자1 : 없음
		// 반환  : 없음
		// 기능  : 서버 구동 정지, 모든 연결 끊고 리소스 정리		
		///////////////////////////////////////////////////////////////////////////////////
		void Stop();

		///////////////////////////////////////////////////////////////////////////////////
		// GetSessionCount 
		// 인자1 : 없음
		// 반환  : ULONGLONG
		// 기능  : 현재 접속중인 클라이언트의 수 반환	
		///////////////////////////////////////////////////////////////////////////////////
		ULONGLONG GetSessionCount() const;

		///////////////////////////////////////////////////////////////////////////////////
		// Disconnect
		// 인자1 : ULONGLONG SessionID [ 연결을 해제할 세션의 식별자 ]
		// 반환  : true [ 성공 ] // false [ 실패 ]
		// 기능  : 세션의 연결 종료 요청
		///////////////////////////////////////////////////////////////////////////////////
		bool Disconnect(ULONGLONG SessionID);

		///////////////////////////////////////////////////////////////////////////////////
		// SendPacket
		// 인자1 : ULONGLONG SessionID [ 패킷을 송신할 세션의 식별자 ]
		// 인자2 : CNetPacket *Packet [ 패킷 내용이 담긴 직렬화 버퍼의 포인터 ]
		// 반환  : true [ 성공 ] // false [ 실패 ]
		// 기능  : 해당 세션에게 패킷을 전송할 때 사용			
		///////////////////////////////////////////////////////////////////////////////////
		bool SendPacket(ULONGLONG SessionID, CNetPacket *Packet);		

		///////////////////////////////////////////////////////////////////////////////////
		// SendPacketAndDisconnect
		// 인자1 : ULONGLONG SessionID [ 패킷을 송신할 세션의 식별자 ]
		// 인자2 : CNetPacket *Packet [ 패킷 내용이 담긴 직렬화 버퍼의 포인터 ]
		// 반환  : true [ 성공 ] // false [ 실패 ]
		// 기능  : 해당 세션에게 패킷을 전송하고 연결을 끊는다.		
		///////////////////////////////////////////////////////////////////////////////////
		bool SendPacketAndDisconnect(ULONGLONG SessionID, CNetPacket *Packet);

	protected:	

		// Accept 성공 후 접속이 확정되면 호출 
		virtual void OnClientJoin(const SOCKADDR_IN& ClientAddr, ULONGLONG SessionID) = 0;

		// 해당 세션이 릴리즈되면 호출 
		virtual void OnClientLeave(ULONGLONG SessionID) = 0;

		// Accept 직후 호출, 컨텐츠 레벨에서 클라이언트의 유효성 검사시 활용
		virtual bool OnConnectionRequest(const SOCKADDR_IN& ClientAddr) = 0;

		// 패킷 수신 완료 후 호출
		virtual void OnRecv(ULONGLONG SessionID, CNetPacket *Packet) = 0;

		// 패킷 송신 완료 후 호출
		virtual void OnSend(ULONGLONG SessionID, int SendSize) = 0;

		// 워커스레드 GQCS 바로 하단에서 호출 
		virtual void OnWorkerThreadBegin() = 0;

		// 워커스레드 1루프 종료 후 호출
		virtual void OnWorkerThreadEnd() = 0;

		// 내부적으로 진행되는 함수에서 오류가 발생할 시 본 함수를 호출해서 알려줌 
		virtual void OnError(int ErrorNo, const TCHAR* ErrorMsg) = 0;

		virtual void OnSemError(ULONGLONG SessionID) = 0;


	private:

		////////////////////////////////////////////////////
		// 네트워크 라이브러리 private 멤버 함수			
		// 서버가 구동되면 적절한 시점에 자동 호출됨 
		////////////////////////////////////////////////////

		///////////////////////////////////////////////////////////////////////////////////
		// AcceptThread 
		// 인자  : LPVOID Param [ CNetServer의 this 포인터 ]
		// 반환  : 스레드 함수 반환 
		// 기능  : accept, 연결 완료 시 세션테이블에 세션 등록
		// 참고  : 관련 이벤트 핸들링 함수 OnConnectionRequest, OnClientJoin
		///////////////////////////////////////////////////////////////////////////////////
		static unsigned int WINAPI AcceptThread(LPVOID Param);

		///////////////////////////////////////////////////////////////////////////////////
		// WorkerThread
		// 인자  : LPVOID Param [ CNetServer의 this 포인터 ]
		// 반환  : 스레드 함수 반환 
		// 기능  : I/O를 담당할 워커 스레드 
		// 참고  : 관련 이벤트 핸들링 함수 OnRecv, OnSend
		///////////////////////////////////////////////////////////////////////////////////
		static unsigned int WINAPI WorkerThread(LPVOID Param);

		///////////////////////////////////////////////////////////////////////////////////
		// CheckPacket 
		// 인자  : NetSession *pSession [ 세션 구조체 포인터 ]
		// 반환  : bool ( 수신받은 패킷처리가 성공했을시 true, 이상한 패킷이 전달되면 false)
		// 기능  : 네트워크 클래스 계층의 완성된 패킷이 존재하면 헤더를 떼고 OnRecv에 전달
		// 참고  : 관련 이벤트 핸들링 함수 OnRecv
		///////////////////////////////////////////////////////////////////////////////////
		bool CheckPacket(NetSession *pSession);

		///////////////////////////////////////////////////////////////////////////////////
		// SendPost
		// 인자1 : NetSession *pSession [ 세션 구조체 포인터 ]
		// 반환  : 성공시 true, 실패시 false 
		// 기능  : WSASend 등록 
		// 참고  : SendPacket시 호출, OnSend 후 호출 
		///////////////////////////////////////////////////////////////////////////////////		
		bool SendPost(NetSession *pSession);

		///////////////////////////////////////////////////////////////////////////////////
		// RecvPost
		// 인자1 : NetSession *pSession [ 세션 구조체 포인터 ]
		// 반환  : 성공시 true, 실패시 false 
		// 기능  : WSARecv 등록
		// 참고  : 비동기 Recv는 항상 등록되어 있다.
		///////////////////////////////////////////////////////////////////////////////////		
		bool RecvPost(NetSession *pSession);

		///////////////////////////////////////////////////////////////////////////////////
		// ReleaseSession
		// 인자1 : NetSession *pSession [ 세션 구조체 포인터 ]
		// 반환  : 없음
		// 기능  : 세션 정리
		// 참고  : 이 함수는 I/O Count가 0일 때만 호출되어야 한다.
		///////////////////////////////////////////////////////////////////////////////////	
		void ReleaseSession(NetSession *pSession);

		///////////////////////////////////////////////////////////////////////////////////
		// SessionAcquireLock
		// 인자1 : NetSession *pSession [ 세션 구조체 포인터 ]
		// 반환  : bool (성공시 true, 실패시 false)
		// 기능  : 멀티 스레드 환경에서 세션을 보호하는 기능
		///////////////////////////////////////////////////////////////////////////////////	
		bool SessionAcquireLock(NetSession * pSession, ULONGLONG LocalSessionID);

		///////////////////////////////////////////////////////////////////////////////////
		// SessionReleaseLock
		// 인자1 : NetSession *pSession [ 세션 구조체 포인터 ]
		// 반환  : 없음
		// 기능  : 멀티 스레드 환경에서 세션을 보호하는 기능
		///////////////////////////////////////////////////////////////////////////////////	
		void SessionReleaseLock(NetSession *pSession);

		// Start함수의 가독성을 위한 초기화 함수들
		bool WSAInit(TCHAR * BindIP, USHORT port, bool IsNoDelay);
		bool SessionInit(WORD MaxSessions);
		bool ThreadInit(DWORD ConcurrentThreads, DWORD MaxThreads);

	private:

		////////////////////////////////////////////////////
		// 네트워크 라이브러리 private 멤버 변수		
		////////////////////////////////////////////////////

		// 서버 주소 구조체
		SOCKADDR_IN           m_ServerAddr;	

		// 워커스레드와 연결될 IOCP의 핸들 [ 동시실행 스레드 개수, 생성 스레드 개수 사용자가 결정 ]
		HANDLE                m_hWorkerIOCP = NULL;

		// 워커스레드 핸들 배열 [스레드 개수는 사용자가 결정] 
		HANDLE               *m_ThreadHandleArr = nullptr;

		// 워커스레드 아이디 배열 [스레드 개수는 사용자가 결정]
		DWORD                *m_ThreadIDArr = nullptr;

		// 세션 관리 배열		
		NetSession           *m_SessionArray = nullptr;

		// 세션 삽입가능한 배열의 인덱스를 보관하고 있는 스택
		CLockFreeStack<WORD> *m_NextSlot;

		// 리슨 소켓 
		SOCKET                m_Listen = INVALID_SOCKET;

		// 현재 총 접속 세션 수 
		ULONGLONG             m_SessionCount = 0;

		// 최대 접속 가능 클라이언트의 수 
		DWORD                 m_MaxSessions = 0;

		// 생성된 스레드의 개수 
		DWORD                 m_IOCPThreadsCount = 0;	

		// 더미 오버랩드
		OVERLAPPED            m_DummyOverlapped;
	};
}

#endif




