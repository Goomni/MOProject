#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <WS2tcpip.h>
#include <winsock2.h>
#include <Mstcpip.h>
#include "RingBuffer\RingBuffer.h"
#include "Packet\Packet.h"
#include "LockFreeQueue\LockFreeQueue.h"
#include "ListQueue\ListQueue.h"
#include "Define\GGM_CONSTANTS.h"
#pragma  comment(lib, "Ws2_32.lib")

namespace GGM
{
	///////////////////////////////////////////////////////////////////////////////////////////
	// 세션의 현재 담당 스레드를 결정짓는 모드 
	// 세션에 접근하는 것은 해당 모드의 스레드 (Auth, Game 둘중 하나) + SendThread
	// NONE >> AUTH >> GAME >> LOGOUT 의 단방향으로 모드를 설정함으로서 동기화로직을 간단하게 함 
	///////////////////////////////////////////////////////////////////////////////////////////
	enum class SESSION_MODE
	{
		MODE_NONE = 0,
		MODE_AUTH,
		MODE_AUTH_TO_GAME,
		MODE_GAME,		
		MODE_WAIT_LOGOUT
	};

	/////////////////////////////////////////////////////////////////////////////////////////////////
	// CMMOServer의 네트워크 모듈부의 관리 단위가 되는 CMMOSession 클래스
	// 실제 네트워크 처리를 위한 멤버들이 모두 이곳에 위치함
	// CMMOServer를 상속받은 게임서버는 동시에 CMMOSession을 상속받아 플레이어를 생성 및 관리
	// 네트워크 모듈의 세션 관리 자료구조와 컨텐츠 모듈의 플레이어 관리 자료구조가 분리되지 않는것이 특징
	// 세션과 플레이어를 매칭시키는 SessionID를 활용한 검색을 지양하고 바로 접근, 사용할 수 있도록 함 
	/////////////////////////////////////////////////////////////////////////////////////////////////
	class CMMOSession
	{
	public:

		CMMOSession() = default;
		virtual ~CMMOSession() = default;

		///////////////////////////////////////////////////////////////////////////////////////
		// 외부에서 네트워크 모듈에 패킷 송신, 연결 종료, 모드 변경 요청         
		///////////////////////////////////////////////////////////////////////////////////////

		// 해당 세션에게 패킷 송신 요청 
		void SendPacket(CNetPacket *pPacket);

		// 해당 세션에게 패킷 송신 후 연결 끊기 요청
		void SendPacketAndDisconnect(CNetPacket *pPacket);

		// 해당 세션 연결 종료 요청
		void Disconnect();

		// 해당 세션의 모드를 Auth에서 Game으로 변경
		// 외부에서 직접 세션의 모드를 변경하는 것은 불허한다. 
		void SetAuthToGame();		

	protected:

		///////////////////////////////////////////////////////////////////////////////////////
		// 이벤트 핸들링용 순수 가상 함수
		// CMMOSession을 상속받은 컨텐츠 모듈의 플레이어는 아래의 함수들을 구현
		///////////////////////////////////////////////////////////////////////////////////////

		// Auth Thread 
		virtual void OnAuth_ClientJoin() = 0;
		virtual void OnAuth_ClientLeave(bool IsLogOut) = 0;
		virtual void OnAuth_ClientPacket(CNetPacket *pPacket) = 0;

		// Game Thread 
		virtual void OnGame_ClientJoin() = 0;
		virtual void OnGame_ClientLeave() = 0;
		virtual void OnGame_ClientPacket(CNetPacket *pPacket) = 0;

		// Release
		virtual void OnClientRelease() = 0;	

		virtual void OnError(int ErrorNo, const TCHAR* ErrorMsg) = 0;

		friend class CMMOServer;

	private:		

		///////////////////////////////////////////////////////////////////////////////////
		// DecreasIoCount
		// 인자  : 없음
		// 반환  : 없음
		// 기능  : IoCount 1 감소 >> 0이면 해당 로그아웃 플래그 온 		
		///////////////////////////////////////////////////////////////////////////////////	
		void DecreaseIoCount();

	private:

		///////////////////////////////////////////////////////////////////////////////////////
		// 네트워크 모듈만이 접근하는 부분
		///////////////////////////////////////////////////////////////////////////////////////

		volatile LONG               m_IoCount = 0;
		bool			            m_IsSending = false;		
		bool                        m_IsCanceled = false;
		bool                        m_IsSendAndDisconnect = false; // 보내고 끊기 플래그 
		bool                        m_IsLogout = true; // 로그아웃 플래그
		bool                        m_IsAuthToGame = false; // Auth 스레드에서 Game 스레드로 이동 플래그, 외부에서 요청 
		ULONG   					m_SentPacketCount = 0; // SendPost 내부에서 Send요청한 패킷 개수		
		CNetPacket                 *m_pDisconnectPacket = nullptr; // 보내고 끊기에 해당하는 패킷		
		SESSION_MODE                m_Mode = SESSION_MODE::MODE_NONE;	
		SOCKET                      m_socket;
		ULONGLONG                   m_HeartbeatTime;
		ULONGLONG                   m_SessionID;
		USHORT                      m_SessionSlot;				
		OVERLAPPED                  m_RecvOverlapped;
		OVERLAPPED                  m_SendOverlapped;
		CRingBuffer                 m_RecvQ;
		CListQueue<CNetPacket*>     m_SendQ;
		CListQueue<CNetPacket*>     m_PacketQ;
		CNetPacket                 *m_PacketArray[MAX_PACKET_COUNT]; // 완료통지시에 Free할 패킷의 배열
		IN_ADDR					    m_SessionIP;
		USHORT                      m_SessionPort;

	};
}

