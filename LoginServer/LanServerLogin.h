#pragma once
#include "CLanServer\CLanServer.h"
#include "ConfigLoader\ConfigLoader.h"
#include "Define\GGM_CONSTANTS.h"

namespace GGM
{
	constexpr auto SESSION_KEY_LEN = 64;

	// 서버 구동정보 
	// 외부 CONFIG 파일로부터 파서를 이용해서 가지고 온다.
	class LanServerLoginConfig : public CConfigLoader
	{
	public:

		TCHAR  BindIP[16];
		USHORT Port;		
		DWORD  ConcurrentThreads;
		DWORD  MaxThreads;
		bool   IsNoDelay;
		WORD   MaxSessions;
		int    LogLevel;

	public:

		virtual bool LoadConfig(const TCHAR* ConfigFileName) override;

	};

	////////////////////////////////////////////////////////////////////
	// 게임서버, 채팅서버와 로그인 정보를 주고받는 로그인서버의 Lan파트
	// 로그인서버 (CLanServer) <----> 게임서버, 채팅서버 (CLanClient)
	// 클라이언트는 최초로 로그인서버를 거쳐 게임서버, 채팅서버로 이관됨
	////////////////////////////////////////////////////////////////////

	// NetServerLogin에서 관리하는 User 구조체 전방선언
	struct User;

	// CLanServerLogin이 관리하는 클라이언트 
	// 게임서버와 채팅서버가 된다.	

	class CNetServerLogin;

	class CLanServerLogin : public CLanServer
	{

	public:

		CLanServerLogin() = delete;
		CLanServerLogin(LanServerLoginConfig *pLanLoginConfig, CNetServerLogin *pNetLogin, User *UserArr);
		virtual ~CLanServerLogin();

		// 유저가 로그인 요청을 하면 NetServer가 DB에서 정보를 긁어와 계정정보를 확인하고 이 함수를 호출한다.
		// 이 함수 내부에서는 해당 로그인 요청 정보를 LanClient에게 전송한다.
		void SendToken(INT64 AccountNo, char* SessionKey, ULONGLONG Unique);

	protected:

		// CLanServer의 이벤트 핸들링용 순수가상함수를 이곳에서 구현

		// Accept 성공 후 접속이 확정되면 호출 
		virtual void OnClientJoin(const SOCKADDR_IN& ClientAddr, ULONGLONG SessionID) override;

		// 해당 세션에 대한 모든 리소스가 정리되면 호출  
		virtual void OnClientLeave(ULONGLONG SessionID) override;

		// Accept 직후 호출 
		virtual bool OnConnectionRequest(const SOCKADDR_IN& ClientAddr) override;

		// 패킷 수신 완료 후 호출
		virtual void OnRecv(ULONGLONG SessionID, CSerialBuffer *pPacket) override;

		// 패킷 송신 완료 후 호출
		virtual void OnSend(ULONGLONG SessionID, int SendSize) override;

		// 워커스레드 GQCS 바로 하단에서 호출 
		virtual void OnWorkerThreadBegin() override;

		// 워커스레드 1루프 종료 후 호출
		virtual void OnWorkerThreadEnd() override;

		// 내부적으로 진행되는 함수에서 오류가 발생할 시 본 함수를 호출해서 알려줌 
		virtual void OnError(int ErrorNo, const TCHAR* ErrorMsg) override;

	protected:

		/////////////////////////////////////////////////
		// CLanServerLogin 패킷 프로시저
		/////////////////////////////////////////////////

		// LanClient 접속 요청 패킷 송신 >> SS_LoginServer_Login_Proc
		void SS_LoginServer_Login_Proc(ULONGLONG SessionID, CSerialBuffer *pPacket);

		// LanServer가 인증토큰 LanClient로 송신 >> LanClient가 메모리에 저장 >> LanClient가 LanServer로 응답 패킷 송신 >> SS_Res_New_Client_Proc
		void SS_Res_New_Client_Proc(CSerialBuffer *pPacket);

	protected:

		// NetServerLogin의 포인터, LanClient로부터 응답을 받았을 때 NetServerLogin에게 알려주기 위해 필요함.
		CNetServerLogin  *m_pNetLogin = nullptr;

		// LanServer와 연결된 LanClient를 관리하기 위한 배열
		BYTE              *m_LanClientArr = nullptr;
		
		SRWLOCK           m_lock;
		
		ULONGLONG         m_ChatID = INIT_SESSION_ID;
		ULONGLONG         m_GameID = INIT_SESSION_ID;
		ULONGLONG         m_MonitorID = INIT_SESSION_ID;

		// 게임서버, 채팅서버와 주고받는 유저정보가 저장된 배열
		User             *m_UserArray = nullptr;	

	};
}
