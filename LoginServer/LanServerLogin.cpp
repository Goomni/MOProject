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
		// �� Ŭ���̾�Ʈ �����ϱ� ���� �迭
		m_LanClientArr = new BYTE[pLanLoginConfig->MaxSessions];
		memset(m_LanClientArr, 0xff, pLanLoginConfig->MaxSessions);

		if (m_LanClientArr == nullptr)
		{
			OnError(GetLastError(), _T("[CLanServerLogin] Mem Alloc Failed %d"));			
		}	

		// CLanServerLogin ���� ����
		bool bOk = Start(
			pLanLoginConfig->BindIP,
			pLanLoginConfig->Port,
			pLanLoginConfig->ConcurrentThreads,
			pLanLoginConfig->MaxThreads,
			pLanLoginConfig->IsNoDelay,
			pLanLoginConfig->MaxSessions
		);

		// ���� �������н� ũ����
		if (bOk == false)
			OnError(GGM_ERROR::STARTUP_FAILED, _T("[CLanServerLogin] Start Failed %d"));

		// ����ȭ ��ü �ʱ�ȭ
		InitializeSRWLock(&m_lock);
	}

	CLanServerLogin::~CLanServerLogin()
	{
		// ���� ���ҽ� ����
		delete m_LanClientArr;		

		// ���� ���� ����
		Stop();
	}

	void GGM::CLanServerLogin::SendToken(INT64 AccountNo, char* SessionKey, ULONGLONG Unique)
	{
		// CLanServerLogin���� ä�ü����� ���Ӽ����� ������ �α��� ������ ������
		CSerialBuffer *pPacket = CSerialBuffer::Alloc();		

		// ��Ŷ ����
		WORD PacketType = (WORD)en_PACKET_SS_REQ_NEW_CLIENT_LOGIN;
		pPacket->Enqueue((char*)&PacketType, sizeof(WORD));
		pPacket->Enqueue((char*)&AccountNo, sizeof(INT64));		
		pPacket->Enqueue(SessionKey, SESSION_KEY_LEN);		
		pPacket->Enqueue((char*)&Unique, sizeof(INT64));				

		// ��Ŷ �۽�	
		do
		{
			AcquireSRWLockShared(&m_lock);

			// LanClient���� ������ �������� �۽����� �ʰ� �׳� ������.
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

		// ��Ŷ Alloc�� ���� Free
		CSerialBuffer::Free(pPacket);
	}

	void GGM::CLanServerLogin::OnClientJoin(const SOCKADDR_IN & ClientAddr, ULONGLONG SessionID)
	{
		// ä�ü����� ���Ӽ����� �α��μ����� ��������
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

		// ä�ü����� ���Ӽ����� �α��μ������� ������ ����	
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
			// ���Ӽ���, ä�ü����κ����� ������ū ���� ����
			SS_Res_New_Client_Proc(pPacket);
		}
		else
		{
			// ���Ӽ��� ä�ü����� �α��� ������ LanClient�μ� ����
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
		// ���ǿ� �ش� ����Ÿ���� ���
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
		// ���Ӽ����� ä�ü����κ��� ������ū�� ���� ������ ����
		// CNetServerLogin���� �˷��־� ������ ���Ӽ����� ä�ü����� ���� 		
		INT64 AccountNo;
		INT64 Param;

		pPacket->Dequeue((char*)&AccountNo, sizeof(AccountNo));
		pPacket->Dequeue((char*)&Param, sizeof(Param));

		// NetServerLogin���� ���Ӽ��� Ȥ�� ä�ü����� ������ū�� ���������� ���������� �˷���
		m_pNetLogin->Res_Login_Proc(AccountNo, Param, dfLOGIN_STATUS_OK, nullptr, 0);
	}

	bool GGM::LanServerLoginConfig::LoadConfig(const TCHAR * ConfigFileName)
	{
		CParser parser;

		// ----------------------------------------------------------------------------------------------
		// ���� ���� ���� �ε�
		bool Ok = parser.LoadFile(ConfigFileName);
		DWORD err = GetLastError();
		// ----------------------------------------------------------------------------------------------

		// ----------------------------------------------------------------------------------------------
		// ���� ������ ��� ������ �α׷� �����.
		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("LOGIN SERVER CONFIG FILE OPEN FAILED"));
			return 0;
		}

		CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("========== LAN_SERVER_LOGIN OPEN START ========== "));
		// ----------------------------------------------------------------------------------------------

		// ----------------------------------------------------------------------------------------------
		// ���� ����
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
