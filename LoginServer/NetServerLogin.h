#pragma once
#include "CNetServer\CNetServer.h"
#include "ConfigLoader\ConfigLoader.h"

namespace GGM
{
	constexpr auto ID_LEN = 20;
	constexpr auto NICK_LEN = 20;
	constexpr auto IP_LEN = 16;

	// 서버 구동정보 
	// 외부 CONFIG 파일로부터 파서를 이용해서 가지고 온다.	
	class NetServerLoginConfig : public CConfigLoader
	{
	public:

		TCHAR  BindIP[16];
		USHORT Port;
		TCHAR  ChatServerIP[16];		
		USHORT ChatServerPort;		
		DWORD  ConcurrentThreads;
		DWORD  MaxThreads;
		bool   IsNoDelay;
		WORD   MaxSessions;
		BYTE   PacketCode;
		BYTE   PacketKey1;
		BYTE   PacketKey2;
		int    LogLevel;
		TCHAR  DB_ip[16];
		TCHAR  DB_username[64];
		TCHAR  DB_password[64];
		TCHAR  DB_dbname[64];
		USHORT DB_port;

	public:

		virtual bool LoadConfig(const TCHAR* ConfigFileName) override;

	};	

	// 로그인 서버 관리 대상 유저 객체	
	struct User
	{
		ULONGLONG SessionID = INIT_SESSION_ID; // 플레이어 식별자 
		INT64     AccountNo; // 계정번호		
		TCHAR     ID[ID_LEN];
		TCHAR     Nick[NICK_LEN];
	};	

	/////////////////////////////////////////////////////////////////////
	// 전방선언 
	/////////////////////////////////////////////////////////////////////

	// 게임서버, 채팅서버등의 LanClient와 통신할 LanServer
	class CLanServerLogin; 	

	// TLS를 활용하여 스레드별 디비 연결을 유지하기 위한 디비 커넥터 
	class CDBConnectorTLS;

	// CLanServerLogin의 초기화를 위한 설정 정보가 담긴 클래스
	class LanServerLoginConfig;

	///////////////////////////////////////////////////////////////////
	// Net서버를 상속받은 로그인 서버 클래스
	// 1. 클라이언트의 로그인 요청을 받음
	// 2. LanServer로서 LanClient인 게임서버, 채팅서버와 통신
	// 3. 클라이언트의 요청을 다른 서버에게 중개하고 클라이언트들을 이관
	///////////////////////////////////////////////////////////////////
	class CNetServerLogin : public CNetServer
	{		
	public:		
		CNetServerLogin() = delete;

		CNetServerLogin(
			NetServerLoginConfig *pNetLoginConfig,
			LanServerLoginConfig *pLanLoginConfig		
		);

		virtual ~CNetServerLogin();	

		//////////////////////////////////////////////////////////////////////////////////////////////////////////
		// 로그인 처리의 흐름 
		// 1. 유저가 로그인 서버에 접속 및 로그인 패킷 송신
		// 2. 로그인 서버가 DB에서 계정정보를 읽어옴 
		// 3. 유저가 보낸 패킷과 DB 계정정보가 일치한다면 채팅서버, 게임서버의 랜 클라이언트에게 인증토큰 전송
		// 4. 인증토큰을 수신받은 쪽은 해당 정보를 메모리에 저장하고 응답 패킷 송신
		// 5. 응답 패킷을 수신한 로그인 서버는 유저에게 최종적으로 로그인 응답 패킷을 보냄 
		// 6. Send 완료통지가 오면 로그인 서버가 유저와의 연결을 끊고 유저는 게임서버, 채팅서버로 접속함		
		//////////////////////////////////////////////////////////////////////////////////////////////////////////

		// 5번에 해당하는 절차를 처리하기 위한 함수 
		// CLanServerLogin에서 호출해야 하기 때문에 public으로 둔다.
		void Res_Login_Proc(INT64 AccountNo, INT64 SessionID, BYTE Status, TCHAR *IP, USHORT Port);

		// 모니터링 정보를 출력
		void PrintInfo();

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

		// 패킷 생성 함수
		void CreatePacket_CS_LOGIN_RES(
			CNetPacket *pPacket, 
			INT64 AccountNo, 
			BYTE Status, 
			TCHAR *ID, 
			TCHAR *Nick, 
			TCHAR *GameServerIP,
			USHORT GameServerPort,
			TCHAR *ChatServerIP,
			USHORT ChatServerPort
		);

	protected:

		// 로그인 유저 관리 배열 
		User            *m_UserArray = nullptr;		

		// 로그인 서버에 접속한 유저의 수 
		size_t           m_UserCount = 0;
		
		// CLanServerLogin의 포인터, 로그인 처리 과정중에 LanClient와 통신하기 위해 사용
	    CLanServerLogin *m_pLanLogin = nullptr;

		// DB 작업용 커넥터
		CDBConnectorTLS *m_pDBConnectorTLS = nullptr;

		// 일단 지금은 로그인 서버가 채팅서버 IP를 로그인 서버가 가지고 있음
		TCHAR            m_ChatServerIP[16];
		USHORT           m_ChatServerPort;
	};
}