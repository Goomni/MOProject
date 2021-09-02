#include "LoginServer\LanServerLogin.h"
#include "LoginServer\NetServerLogin.h"
#include "CommonProtocol\CommonProtocol.h"
#include "Define\GGM_ERROR.h"
#include "Logger\Logger.h"

namespace GGM
{
	CLanServerLogin::CLanServerLogin(LanServerLoginConfig *pLanLoginConfig, CNetServerLogin *pNetLogin, User *UserArr)
		: m_pNetLogin(pNetLogin), m_UserArray(UserArr)
	{		
		// 랜 클라이언트 관리하기 위한 배열
		m_LanClientArr = new BYTE[pLanLoginConfig->MaxSessions];
		memset(m_LanClientArr, 0xff, pLanLoginConfig->MaxSessions);

		if (m_LanClientArr == nullptr)
		{
			OnError(GetLastError(), _T("[CLanServerLogin] Mem Alloc Failed %d"));			
		}	

		// CLanServerLogin 서버 구동
		bool bOk = Start(
			pLanLoginConfig->BindIP,
			pLanLoginConfig->Port,
			pLanLoginConfig->ConcurrentThreads,
			pLanLoginConfig->MaxThreads,
			pLanLoginConfig->IsNoDelay,
			pLanLoginConfig->MaxSessions
		);

		// 서버 구동실패시 크래쉬
		if (bOk == false)
			OnError(GGM_ERROR::STARTUP_FAILED, _T("[CLanServerLogin] Start Failed %d"));

		// 동기화 객체 초기화
		InitializeSRWLock(&m_lock);
	}

	CLanServerLogin::~CLanServerLogin()
	{
		// 각종 리소스 정리
		delete m_LanClientArr;		

		// 서버 구동 중지
		Stop();
	}

	void GGM::CLanServerLogin::SendToken(INT64 AccountNo, char* SessionKey, ULONGLONG Unique)
	{
		// CLanServerLogin에서 채팅서버와 게임서버로 유저의 로그인 정보를 보내줌
		CSerialBuffer *pPacket = CSerialBuffer::Alloc();		

		// 패킷 조립
		WORD PacketType = (WORD)en_PACKET_SS_REQ_NEW_CLIENT_LOGIN;
		pPacket->Enqueue((char*)&PacketType, sizeof(WORD));
		pPacket->Enqueue((char*)&AccountNo, sizeof(INT64));		
		pPacket->Enqueue(SessionKey, SESSION_KEY_LEN);		
		pPacket->Enqueue((char*)&Unique, sizeof(INT64));				

		// 패킷 송신	
		do
		{
			AcquireSRWLockShared(&m_lock);

			// LanClient와의 연결이 끊겼으면 송신하지 않고 그냥 나간다.
			if (m_ChatID == INIT_SESSION_ID)
				break;

			SendPacket(m_ChatID, pPacket);	

			/*if (m_GameID == INIT_SESSION_ID)
				break;

			SendPacket(m_GameID, pPacket);

			if (m_MonitorID == INIT_SESSION_ID)
				break;

			SendPacket(m_MonitorID, pPacket);*/
		
		} while (0);

		ReleaseSRWLockShared(&m_lock);

		// 패킷 Alloc에 대한 Free
		CSerialBuffer::Free(pPacket);
	}

	void GGM::CLanServerLogin::OnClientJoin(const SOCKADDR_IN & ClientAddr, ULONGLONG SessionID)
	{
		// 채팅서버나 게임서버가 로그인서버로 접속했음
		if (m_LanClientArr[(WORD)SessionID] != 0xff)
		{		
			OnError(GGM_ERROR::SESSION_SYNC_FAILED, _T("[CLanServerLogin] OnClientJoin Error %d"));
		}					
	}

	void GGM::CLanServerLogin::OnClientLeave(ULONGLONG SessionID)
	{		
		if (m_LanClientArr[(WORD)SessionID] == 0xff)
		{		
			OnError(GGM_ERROR::SESSION_SYNC_FAILED, _T("[CLanServerLogin] OnClientLeave Error %d"));
		}		

		AcquireSRWLockExclusive(&m_lock);
		switch (m_LanClientArr[(WORD)SessionID])
		{
		case dfSERVER_TYPE_CHAT:
			m_ChatID = INIT_SESSION_ID;
			break;
		case dfSERVER_TYPE_GAME:
			m_GameID = INIT_SESSION_ID;
			break;		
		case dfSERVER_TYPE_MONITOR:
			m_MonitorID = INIT_SESSION_ID;
			break;
		}
		ReleaseSRWLockExclusive(&m_lock);

		// 채팅서버나 게임서버가 로그인서버와의 접속을 끊음	
		m_LanClientArr[(WORD)SessionID] = 0xff;	
	}

	bool GGM::CLanServerLogin::OnConnectionRequest(const SOCKADDR_IN & ClientAddr)
	{
		return true;
	}

	void GGM::CLanServerLogin::OnRecv(ULONGLONG SessionID, CSerialBuffer * pPacket)
	{
		WORD PacketType;
		pPacket->Dequeue((char*)&PacketType, sizeof(WORD));

		if (PacketType == en_PACKET_SS_RES_NEW_CLIENT_LOGIN)
		{
			// 게임서버, 채팅서버로부터의 인증토큰 수신 응답
			SS_Res_New_Client_Proc(pPacket);
		}
		else
		{
			// 게임서버 채팅서버가 로그인 서버에 LanClient로서 접속
			SS_LoginServer_Login_Proc(SessionID, pPacket);
		}
	}

	void GGM::CLanServerLogin::OnSend(ULONGLONG SessionID, int SendSize)
	{
	}

	void GGM::CLanServerLogin::OnWorkerThreadBegin()
	{
	}

	void GGM::CLanServerLogin::OnWorkerThreadEnd()
	{
	}

	void GGM::CLanServerLogin::OnError(int ErrorNo, const TCHAR * ErrorMsg)
	{
		CLogger::GetInstance()->Log(_T("LoginServer"), LEVEL::DBG, OUTMODE::FILE, ErrorMsg);
		CCrashDump::ForceCrash();
	}

	void CLanServerLogin::SS_LoginServer_Login_Proc(ULONGLONG SessionID, CSerialBuffer * pPacket)
	{
		// 세션에 해당 서버타입을 기록
		BYTE ServerType;
		pPacket->Dequeue((char*)&ServerType, sizeof(BYTE));
		m_LanClientArr[(WORD)SessionID] = ServerType;		
		
		AcquireSRWLockExclusive(&m_lock);

		switch (ServerType)
		{
		case dfSERVER_TYPE_CHAT:
			m_ChatID = SessionID;
			break;
		case dfSERVER_TYPE_GAME:
			m_GameID = SessionID;
			break;		
		case dfSERVER_TYPE_MONITOR:
			m_MonitorID = SessionID;
			break;
		}

		ReleaseSRWLockExclusive(&m_lock);		
	}

	void CLanServerLogin::SS_Res_New_Client_Proc(CSerialBuffer * pPacket)
	{
		// 게임서버와 채팅서버로부터 인증토큰에 대한 응답이 왔음
		// CNetServerLogin에게 알려주어 유저를 게임서버와 채팅서버로 보냄 		
		INT64 AccountNo;
		INT64 Param;

		pPacket->Dequeue((char*)&AccountNo, sizeof(AccountNo));
		pPacket->Dequeue((char*)&Param, sizeof(Param));

		// NetServerLogin에게 게임서버 혹은 채팅서버가 인증토큰을 정상적으로 수신했음을 알려줌
		m_pNetLogin->Res_Login_Proc(AccountNo, Param, dfLOGIN_STATUS_OK, nullptr, 0);
	}

	bool GGM::LanServerLoginConfig::LoadConfig(const TCHAR * ConfigFileName)
	{
		CParser parser;

		// ----------------------------------------------------------------------------------------------
		// 서버 설정 파일 로드
		bool Ok = parser.LoadFile(ConfigFileName);
		DWORD err = GetLastError();
		// ----------------------------------------------------------------------------------------------

		// ----------------------------------------------------------------------------------------------
		// 서버 구동시 모든 정보는 로그로 남긴다.
		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("LOGIN SERVER CONFIG FILE OPEN FAILED"));
			return 0;
		}

		CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("========== LAN_SERVER_LOGIN OPEN START ========== "));
		// ----------------------------------------------------------------------------------------------

		// ----------------------------------------------------------------------------------------------
		// 구역 설정
		parser.SetSpace(_T("#LAN_SERVER_LOGIN"));
		// ----------------------------------------------------------------------------------------------

		// ----------------------------------------------------------------------------------------------
		// BIND_IP LOAD
		Ok = parser.GetValue(_T("BIND_IP"), BindIP);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET BIND_IP FAILED : [%s]"), BindIP);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("BIND_IP : [%s]"), BindIP);
		// ----------------------------------------------------------------------------------------------

		// ----------------------------------------------------------------------------------------------
		// PORT LOAD
		Ok = parser.GetValue(_T("SERVER_PORT"), (short*)&Port);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET SERVER_PORT FAILED : [%hd]"), Port);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("SERVER_PORT : [%hd]"), Port);		
		// ----------------------------------------------------------------------------------------------

		// ----------------------------------------------------------------------------------------------
		// ConcurrentThreads LOAD
		Ok = parser.GetValue(_T("CONCURRENT_THREADS"), (int*)&ConcurrentThreads);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET CONCURRENT_THREADS FAILED : [%d]"), ConcurrentThreads);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("CONCURRENT_THREADS : [%d]"), ConcurrentThreads);
		// ----------------------------------------------------------------------------------------------

		// ----------------------------------------------------------------------------------------------
		// MaxThreads LOAD
		Ok = parser.GetValue(_T("MAX_THREADS"), (int*)&MaxThreads);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET MAX_THREADS FAILED : [%d]"), MaxThreads);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("MAX_THREADS : [%d]"), MaxThreads);
		// ----------------------------------------------------------------------------------------------

		// ----------------------------------------------------------------------------------------------
		// IsNoDelay LOAD
		Ok = parser.GetValue(_T("NO_DELAY"), (bool*)&IsNoDelay);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET NO_DELAY FAILED : [%d]"), IsNoDelay);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("NO_DELAY : [%d]"), IsNoDelay);
		// ----------------------------------------------------------------------------------------------

		// ----------------------------------------------------------------------------------------------
		// MaxSessions LOAD
		Ok = parser.GetValue(_T("MAX_SESSION"), (short*)&MaxSessions);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET MAX_SESSION FAILED : [%d]"), MaxSessions);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("MAX_SESSION : [%d]"), MaxSessions);		
		// ----------------------------------------------------------------------------------------------

		// ----------------------------------------------------------------------------------------------
		// LogLevel LOAD
		Ok = parser.GetValue(_T("LOG_LEVEL"), (int*)&LogLevel);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET LOG_LEVEL FAILED : [%d]"), LogLevel);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("LOG_LEVEL : [%d]"), LogLevel);
		CLogger::GetInstance()->SetDefaultLogLevel((LEVEL)LogLevel);
		// ----------------------------------------------------------------------------------------------


		CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("========== SERVER OPEN SUCCESSFUL ========== "));

		return true;
	}

}
