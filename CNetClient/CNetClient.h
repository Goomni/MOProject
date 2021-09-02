#ifndef _C_NET_CLIENT_H_
#define _C_NET_CLIENT_H_

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <WS2tcpip.h>
#include <winsock2.h>
#include "RingBuffer\RingBuffer.h"
#include "SerialBuffer\SerialBuffer.h"
#include "MemoryPool\MemoryPool.h"
#include "LockFreeStack\LockFreeStack.h"
#include "LockFreeQueue\LockFreeQueue.h"
#include "Define\GGM_CONSTANTS.h"
#pragma  comment(lib, "Ws2_32.lib")

namespace GGM
{
	////////////////////////////////////////////////////
	// 세션 구조체 Session			
	////////////////////////////////////////////////////
	struct Session
	{
		SOCKET							socket = INVALID_SOCKET;
		LONG			                IoCount = 0;
		LONG							IsReleased = FALSE;
		ULONGLONG                       SessionID = 0;
		ULONG   					    SentPacketCount = 0; // SendPost 내부에서 Send요청한 패킷 개수							         
		OVERLAPPED		                RecvOverlapped; // 수신용 OVERLAPPED 구조체
		OVERLAPPED		                SendOverlapped; // 송신용 OVERLAPPED 구조체		
		CRingBuffer		                RecvQ; // 수신용 링버퍼				 
		CLockFreeQueue<CNetPacket*>     SendQ; // 송신용 직렬화 버퍼의 포인터를 담을 락프리 큐
		CNetPacket  		            *PacketArray[MAX_PACKET_COUNT]; // SendQ에서 뽑아낸 직렬화 버퍼의 포인터를 담을 배열
		LONG			                IsSending = FALSE; // 샌드 플래그 										
	};

	///////////////////////////////////////////////////////////////////
	// 외부 네트워크의 클라이언트의 네트워크 엔진부 CNetClient
	// - IOCP 사용
	// - 클래스 내부에 워커스레드를 가짐
	// - 이벤트 핸들링 함수는 순수가상함수로 제작하여 외부에서 상속받아 정의
	// - 내부에서 자체적으로 세션을 관리함
	///////////////////////////////////////////////////////////////////
	class CNetClient
	{
	public:

		/////////////////////////////////////////////////////////////////
		// * 네트워크 라이브러리 순수 가상함수
		// * 모든 이벤트 핸들링 함수는 순수가상함수로서 상속받는 쪽에서 정의한다.
		/////////////////////////////////////////////////////////////////		

		// NetServer와의 연결이 성공한 직후 호출 
		virtual void OnConnect() = 0;

		// 한 세션에 대해 I/O Count가 정리되고 연결 및 리소스가 정리되었을 때 호출된다. 
		virtual void OnDisconnect() = 0;

		// 패킷 수신 완료 후 호출
		virtual void OnRecv(CNetPacket *Packet) = 0;

		// 패킷 송신 완료 후 호출
		virtual void OnSend(int SendSize) = 0;

		// 워커스레드 GQCS 바로 하단에서 호출 
		virtual void OnWorkerThreadBegin() = 0;

		// 워커스레드 1루프 종료 후 호출
		virtual void OnWorkerThreadEnd() = 0;

		// 내부적으로 진행되는 함수에서 오류가 발생할 시 본 함수를 호출해서 알려줌 
		virtual void OnError(int ErrorNo, const TCHAR* ErrorMsg) = 0;

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
		CNetClient() = default;

		///////////////////////////////////////////////////////////////////////////////////
		// 소멸자 
		// 인자1 : 없음
		// 반환  : 없음
		// 기능  : Stop 함수에서 모든 정리를 시행할 것이므로 소멸자에서 할일이 없다.
		// 참고  : 
		///////////////////////////////////////////////////////////////////////////////////
		virtual ~CNetClient() = default;

		///////////////////////////////////////////////////////////////////////////////////
		// Start 
		// 인자1 : TCHAR* ConnectIP [ 접속할 NetServer IP ]
		// 인자2 : USHORT port [ 접속할 NetServer port ] 
		// 인자3 : DWORD ConcurrentThreads [ IOCP가 동시에 실행할 스레드 개수 ] 
		// 인자4 : DWORD MaxThreads [ 생성할 최대 스레드 개수 ]
		// 인자5 : bool IsNagleOn [ TCP_NODELAY 옵션 ON/OFF 여부 ]	
		// 반환  : 성공시 true, 실패시 false 혹은, 에러코드(WSAStartup 에러시)
		// 기능  : NetClient 구동 및 초기화
		// 참고  : 외부에서 NetClient 최초 구동시 호출한다.
		///////////////////////////////////////////////////////////////////////////////////
		bool Start(
			TCHAR* ConnectIP,
			USHORT port,
			DWORD ConcurrentThreads,
			DWORD MaxThreads,
			bool  IsNoDelay,
			bool  IsReconnect,
			int   ReconnectDelay,
			BYTE PacketCode,
			BYTE PacketKey1			
		);

		///////////////////////////////////////////////////////////////////////////////////
		// Stop 
		// 인자1 : 없음
		// 반환  : 없음
		// 기능  : NetClient 구동 정지, 모든 연결 끊고 리소스 정리		
		///////////////////////////////////////////////////////////////////////////////////
		void Stop();

		///////////////////////////////////////////////////////////////////////////////////
		// Disconnect		
		// 반환  : true [ 성공 ] // false [ 실패 ]	
		// 참고  : 단순 연결 종료, 세션 리소스를 완벽하게 정리하는 것은 ReleaseSession 함수가
		///////////////////////////////////////////////////////////////////////////////////
		bool Disconnect();

		///////////////////////////////////////////////////////////////////////////////////
		// SendPacket		
		// 인자  : CNetPacket *Packet [ 패킷 내용이 담긴 직렬화 버퍼의 포인터 ]
		// 반환  : true [ 성공 ] // false [ 실패 ]
		// 기능  : NetServer에게 패킷을 전송할 때 사용				
		///////////////////////////////////////////////////////////////////////////////////
		bool SendPacket(CNetPacket *Packet);

	private:

		////////////////////////////////////////////////////
		// 네트워크 라이브러리 private 멤버 함수	
		// 서버가 구동되면 적절한 시점에 자동 호출됨 
		////////////////////////////////////////////////////	

		///////////////////////////////////////////////////////////////////////////////////
		// WorkerThread  : 스레드 함수로 등록해야하므로 static
		// 인자1 : LPVOID Param [ CNetClient의 this 포인터 ]
		// 반환  : 스레드 함수 반환 
		// 기능  : I/O를 담당할 워커 스레드 
		// 참고  : 관련 이벤트 핸들링 함수 OnRecv, OnSend
		///////////////////////////////////////////////////////////////////////////////////
		static unsigned int WINAPI WorkerThread(LPVOID Param);

		///////////////////////////////////////////////////////////////////////////////////
		// CheckPacket 
		// 인자  : 없음
		// 반환  : CNetClient 세션이 사용하는 RecvQ의 주소
		// 기능  : 네트워크 클래스 계층의 완성된 패킷이 존재하면 헤더를 떼고 OnRecv에 전달
		// 참고  : 관련 이벤트 핸들링 함수 OnRecv
		///////////////////////////////////////////////////////////////////////////////////
		bool CheckPacket(CRingBuffer *pRecvQ);

		///////////////////////////////////////////////////////////////////////////////////
		// SendPost
		// 인자  : 없음
		// 반환  : 성공시 true, 실패시 false 
		// 기능  : WSASend 등록 
		// 참고  : SendPacket시 호출, OnSend 후 호출 
		///////////////////////////////////////////////////////////////////////////////////		
		bool SendPost(
			LONG                           *pIsSending,
			CLockFreeQueue<CNetPacket*>    *pSendQ,
			LPOVERLAPPED                    pOverlapped,
			CNetPacket                    **PacketArray,
			ULONG                          *pSentPacketCount,
			LONG				           *pIoCount,
			SOCKET                          sock
		);

		///////////////////////////////////////////////////////////////////////////////////
		// RecvPost
		// 인자  : 없음
		// 반환  : 성공시 true, 실패시 false 
		// 기능  : WSARecv 등록
		// 참고  : 비동기 Recv는 항상 등록되어 있다.
		///////////////////////////////////////////////////////////////////////////////////		
		bool RecvPost(
			CRingBuffer  *pRecvQ,
			LPOVERLAPPED  pOverlapped,
			LONG         *pIoCount,
			SOCKET        sock
		);

		///////////////////////////////////////////////////////////////////////////////////
		// ReleaseSession
		// 인자  : 없음
		// 반환  : 없음
		// 기능  : 세션 정리
		// 참고  : 이 함수는 I/O Count가 0일 때만 호출되어야 한다.
		///////////////////////////////////////////////////////////////////////////////////	
		void ReleaseSession();

		///////////////////////////////////////////////////////////////////////////////////
		// SessionAcquireLock
		// 인자  : ULONGLONG LocalSessionID [ 해당 세션을 얻으려고 시도한 시점의 세션 아이디 ] 
		// 반환  : bool (성공시 true, 실패시 false)
		// 기능  : 멀티 스레드 환경에서 세션을 보호하는 락을 획득 (인터락)		
		///////////////////////////////////////////////////////////////////////////////////	
		bool SessionAcquireLock(ULONGLONG LocalSessionID);

		///////////////////////////////////////////////////////////////////////////////////
		// SessionReleaseLock
		// 인자1 : Session *pSession [ 세션 구조체 포인터 ]
		// 반환  : bool (성공시 true, 실패시 false)
		// 기능  : 멀티 스레드 환경에서 세션을 보호하는 락을 반환 (인터락)	
		///////////////////////////////////////////////////////////////////////////////////	
		void SessionReleaseLock();

		///////////////////////////////////////////////////////////////////////////////////
		// CreateSocket
		// 인자  : 없음
		// 반환  : bool (성공시 true, 실패시 false)
		// 기능  : 소켓 생성
		///////////////////////////////////////////////////////////////////////////////////	
		bool CreateSocket();

		///////////////////////////////////////////////////////////////////////////////////
		// Connect
		// 인자  : 없음
		// 반환  : bool (성공시 true, 실패시 false)
		// 기능  : LanServer와 연결
		///////////////////////////////////////////////////////////////////////////////////	
		bool Connect();

	private:

		////////////////////////////////////////////////////
		// 네트워크 라이브러리 private 멤버 변수		
		////////////////////////////////////////////////////

		// 서버 연결 세션
		Session m_MySession;

		// NO_DELAY 옵션
		bool    m_IsNoDelay;

		// 재연결 옵션
		bool    m_IsReconnect;

		// 재연결 딜레이
		int     m_ReconnectDelay;

		// start 함수 호출 여부		
		LONG    m_IsStarted = FALSE;

		// CLanServer의 주소 구조체
		SOCKADDR_IN m_ServerAddr;

		// 워커스레드와 연결될 IOCP의 핸들 [ 동시실행 스레드 개수, 생성 스레드 개수 사용자가 결정 ]
		HANDLE  m_hWorkerIOCP = NULL;

		// 워커스레드 핸들 배열 [스레드 개수는 사용자가 결정] 
		HANDLE *m_ThreadHandleArr = nullptr;

		// 워커스레드 아이디 배열 [스레드 개수는 사용자가 결정]
		DWORD *m_ThreadIDArr = nullptr;

		// 생성된 스레드의 개수 
		DWORD     m_IOCPThreadsCount = 0;
	};
}

#endif


