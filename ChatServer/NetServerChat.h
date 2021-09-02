#pragma once
#include "CNetServer\CNetServer.h"
#include "ConfigLoader\ConfigLoader.h"
#include <unordered_map>

namespace GGM
{
	// 채팅 서버용 상수	
	constexpr auto ID_LEN = 20;
	constexpr auto NICK_LEN = 20;
	constexpr auto SESSIONKEY_LEN = 64;
	constexpr auto SECTOR_HEIGHT = 50;
	constexpr auto SECTOR_WIDTH = 50;	
	constexpr auto NUM_CHAT_WORK_TYPE = 3;
	constexpr auto NUM_CHAT_PACKET_TYPE = 7;	
	constexpr auto MAX_WORKLOAD = 5000000;	
	constexpr auto CHAT_MSG_LEN = 512;	
	constexpr auto TOKEN_ERASE_PERIOD = 60000;

	// 서버 구동정보 
	// 외부 CONFIG 파일로부터 파서를 이용해서 가지고 온다.
	class NetServerChatConfig : public CConfigLoader
	{
	public:

		TCHAR  BindIP[16];
		USHORT Port;
		DWORD  ConcurrentThreads;
		DWORD  MaxThreads;
		bool   IsNoDelay;
		WORD   MaxSessions;
		BYTE   PacketCode;
		BYTE   PacketKey1;
		BYTE   PacketKey2;
		int    LogLevel;

	public:

		virtual bool LoadConfig(const TCHAR* ConfigFileName) override;

	};

	// 일감 타입
	enum CHAT_WORK_TYPE
	{
		PACKET = 0,
		JOIN = 1, 		
		LEAVE = 2		
	};

	// 업데이트 스레드로 넘겨주는 일감의 단위
	struct CHAT_WORK
	{
		BYTE       type;
		ULONGLONG  SessionID;
		CNetPacket *pPacket;
	};
	
	// 채팅 서버 관리 대상 플레이어 객체
	struct Sector;	
	struct Player
	{
		ULONGLONG SessionID = INIT_SESSION_ID; // 플레이어 식별자 
		INT64     AccountNo; // 계정번호		
		TCHAR     ID[ID_LEN]; // 플레이어의 아이디와 닉네임
		TCHAR     Nickname[NICK_LEN];
		Sector   *pSector = nullptr; // 현재 플레이어가 속한 섹터
		WORD      SectorIdx; // 현재 플레이어가 속한 섹터의 배열에서 플레이어의 위치
		bool      IsLogIn = false;		
	};

	///////////////////////////////////////////////////////
	// 섹터 구조체
	// 채팅을 입력한 클라이언트의 주변 섹터 9개에 채팅을 보낸다
	///////////////////////////////////////////////////////
	struct Sector
	{
		Player **SectorArr = nullptr;
		size_t   SectorSize = 0;

		// 현재 섹터 기준으로 실제로 유효한 섹터의 개수와 그 포인터
		// 자기자신 + 주변 8개, 최대 9개의 유효섹터가 존재
		BYTE     ValidCount = 0;
		Sector*  ValidSector[9];

		WORD     SectorX; // 섹터 좌표
		WORD     SectorY;		
	};

	// 전방선언

	// 로그인 서버와 연동하기 위한 LanClient와 Config
	class CLanClientChat;
	class LanClientChatConfig;

	// CLanClientChat에서 사용하는 인증토큰
	struct Token;

	// 모니터링 서버와 연동하기 위한 LanClient와 Config
	class CLanClientMonitor;
	class LanClientMonitorConfig;	

	/////////////////////////////////////
	// 채팅서버 : NetServer를 상속받음
	/////////////////////////////////////
	class CNetServerChat : public CNetServer
	{
	public:

		CNetServerChat() = delete;
		CNetServerChat(
			NetServerChatConfig *pNetConfig, 
			LanClientChatConfig *pLanChatConfig, 
			LanClientMonitorConfig *pLanMonitorConfig
		);

		virtual ~CNetServerChat();

		// 채팅서버 종료용 
		void StopChatServer();		

		// 모니터링 정보 출력
		void PrintInfo();

		// 로그인한 유저 수				
		size_t GetLoginPlayer() const;

	protected:

		//////////////////////////////////////////////
		// 각종 이벤트 핸들링 함수
		//////////////////////////////////////////////

		// Accept 성공 후 접속이 확정되면 호출 
		virtual void OnClientJoin(const SOCKADDR_IN& ClientAddr, ULONGLONG SessionID) override;

		// 해당 세션에 대한 모든 리소스가 정리되면 호출  
		virtual void OnClientLeave(ULONGLONG SessionID) override;

		// Accept 직후 호출 
		virtual bool OnConnectionRequest(const SOCKADDR_IN& ClientAddr) override;

		// 패킷 수신 완료 후 호출
		virtual void OnRecv(ULONGLONG SessionID, CNetPacket *pPacket) override;

		// 패킷 송신 완료 후 호출
		virtual void OnSend(ULONGLONG SessionID, int SendSize) override;

		// 워커스레드 GQCS 바로 하단에서 호출 
		virtual void OnWorkerThreadBegin() override;

		// 워커스레드 1루프 종료 후 호출
		virtual void OnWorkerThreadEnd() override;

		// 내부적으로 진행되는 함수에서 오류가 발생할 시 본 함수를 호출해서 알려줌 
		virtual void OnError(int ErrorNo, const TCHAR* ErrorMsg) override;

		///////////////////////////////////////////////////////////
		// UpdateThread 
		// 인자  : LPVOID Param [ this 포인터 ]
		// 반환  : 스레드 함수 반환 
		// 기능  : 채팅서버의 메인 업데이트 스레드
		///////////////////////////////////////////////////////////
		static unsigned int __stdcall UpdateThread(LPVOID Param);

		///////////////////////////////////////////////////////////
		// TokenThread 
		// 인자  : LPVOID Param [ this 포인터 ]
		// 반환  : 스레드 함수 반환 
		// 기능  : 주기적으로 유저의 인증 토큰을 정리한다.
		///////////////////////////////////////////////////////////
		static unsigned int __stdcall TokenThread(LPVOID Param);

		/////////////////////////////////////////////////////////////
		// On [CHAT_WORK_TYPE] Proc 
		// 인자  : 없음
		// 반환  : 정상 처리시 true 실패시, false 
		// 기능  : NetServer로부터 받은 일감을 타입별로 처리
		/////////////////////////////////////////////////////////////

		using CHAT_WORK_PROC = bool(CNetServerChat::*)(CHAT_WORK*);
		bool OnJoinProc(CHAT_WORK*);
		bool OnRecvProc(CHAT_WORK*);
		bool OnLeaveProc(CHAT_WORK*);

		/////////////////////////////////////////////////////////////
		// Req [PACKET_TYPE] Proc 
		// 인자  : CNetPacket* [ 컨텐츠 계층의 패킷 포인터 ]
		// 반환  : 정상 처리시 true 실패 시, false 
		// 기능  : 패킷타입별 패킷처리
		/////////////////////////////////////////////////////////////

		// CHAT_PACKET_PROC별 함수포인터		
		using CHAT_PACKET_PROC = void(CNetServerChat::*)(CNetPacket*, Player*);
		void ReqLoginProc(CNetPacket* pPacket, Player* pPlayer);
		void ReqSectorMoveProc(CNetPacket* pPacket, Player* pPlayer);
		void ReqMsgProc(CNetPacket* pPacket, Player* pPlayer);
		void ReqHeartbeatProc(CNetPacket* pPacket, Player* pPlayer);

		/////////////////////////////////////////////////////////////
		// Res [PACKET_TYPE] Proc 			
		// 기능  : Req 패킷에 대한 Res 패킷 처리
		/////////////////////////////////////////////////////////////
		void ResLoginProc(ULONGLONG SessionID, BYTE Result, INT64 AccountNo);
		void ResSectorMoveProc(ULONGLONG SessionID, INT64 AccountNo, WORD SectorX, WORD SectorY);
		void ResMsgProc(ULONGLONG SessionID, INT64 AccountNo, Player* pPlayer, WORD MsgLen, TCHAR *Msg);

		/////////////////////////////////////////////////////////////
		// CreatePacket [PACKET_TYPE] 	
		// 기능  : Req 패킷에 대한 Res 패킷 생성 
		/////////////////////////////////////////////////////////////	
		void CreatePacketResLogin(CNetPacket* pPacket, BYTE Result, INT64 AccountNo);
		void CreatePacketResSectorMove(CNetPacket* pPacket, INT64 AccountNo, WORD SectorX, WORD SectorY);
		void CreatePacketResMsg(CNetPacket*, INT64 AccountNo, TCHAR *ID, TCHAR *NickName, WORD MsgLen, TCHAR *Msg);

		/////////////////////////////////////////////////////////////
		// InitSector
		// 인자  : 없음
		// 반환  : 없음
		// 기능  : 섹터 초기화 
		/////////////////////////////////////////////////////////////
		void InitSector();

		////////////////////////////////////////////////////////////////////
		// ExitFunc
		// 인자  : 없음
		// 반환  : 없음
		// 기능  : 종료용 더미 함수 업데이트 스레드에 할당된 APC Queue에 들어감
		///////////////////////////////////////////////////////////////////
		static void __stdcall NetChatExitFunc(ULONG_PTR Param);		

	protected:

		// 모든 플레이어를 메인으로 관리할 자료구조
		Player                                 *m_PlayerArr = nullptr;
		size_t                                  m_PlayerCount = 0;		
		size_t                                  m_LoginPlayer = 0;

		// 채팅서버가 담당하는 한개의 맵을 의미하는 섹터의 집합, 2차원 배열로 구성
		Sector                                 m_GameMap[SECTOR_HEIGHT][SECTOR_WIDTH];		

		// 일감 할당해줄 메모리 풀 
		CTlsMemoryPool<CHAT_WORK>              *m_pWorkPool;

		// 네트워크 계층의 워커스레드가 컨텐츠 계층의 업데이트 스레드에게 일감을 전달할 큐		
		CLockFreeQueue<CHAT_WORK*>             m_WorkQ;

		// 큐에 대한 생산자 스레드(NetServer의 스레드)와 소비자 스레드(ChatServer)간 통신용 이벤트 객체
		HANDLE                                 m_WorkEvent;	

		// Update 스레드 핸들
		HANDLE                                 m_hUpdateThread;	

		// 토큰 정리 스레드 핸들           
		// 인증 토큰을 보내놓고 일정시간 채팅서버로 들어오지 않는 유저의 토큰 삭제처리 
		HANDLE                                 m_hTokenCollector;

		// LanClientChat 의 포인터
		// 로그인 서버와 통신하는데 사용한다.
		CLanClientChat                        *m_pLanChat;

		// 인증 토큰 저장소
		std::unordered_map<INT64, Token*>     m_TokenTable;

		// 토큰 테이블 락
		SRWLOCK                               m_lock;

		// 토큰 할당해줄 메모리 풀 
		CTlsMemoryPool<Token>                 *m_pTokenPool;

		// LanClientMonitor의 포인터
		CLanClientMonitor                     *m_pLanMonitor;		

		// CHAT_WORK_TYPE (Join, Leave, Packet)에 따른 프로시저
		CHAT_WORK_PROC                         m_ChatWorkProc[NUM_CHAT_WORK_TYPE];

		// CHAT_PACKET_TYPE에 따른 프로시저
		CHAT_PACKET_PROC                       m_PacketProc[NUM_CHAT_PACKET_TYPE+1];
	
	};
}

