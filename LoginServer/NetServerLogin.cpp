#include "LoginServer\NetServerLogin.h"
#include "LoginServer\LanServerLogin.h"
#include "DBConnector\CDBConnectorTLS.h"
#include "CrashDump\CrashDump.h"
#include "Logger\Logger.h"
#include "CommonProtocol\CommonProtocol.h"
#include "Define\GGM_ERROR.h"
#include <strsafe.h>

namespace GGM
{
	CNetServerLogin::CNetServerLogin(
		NetServerLoginConfig *pNetLoginConfig,
		LanServerLoginConfig *pLanLoginConfig			
	)
	{		
		// ���� �迭 �����Ҵ�
		m_UserArray = new User[pNetLoginConfig->MaxSessions];

		if (m_UserArray == nullptr)
			OnError(GetLastError(), _T("[CNetServerLogin] Mem Alloc Failed [GetLastError : %d]"));

		// TLS DBConnector �����Ҵ�
		char  UTF_8_DB_ip[16];
		char  UTF_8_DB_username[64];
		char  UTF_8_DB_password[64];
		char  UTF_8_DB_dbname[64];

		// �������Ͽ��� �о�� ��� ���������� UTF-16 �����̹Ƿ� UTF-8�� ��ȯ�Ѵ�.
		WideCharToMultiByte(0, 0, pNetLoginConfig->DB_ip, 16, UTF_8_DB_ip, 16, nullptr, nullptr);
		WideCharToMultiByte(0, 0, pNetLoginConfig->DB_username, 64, UTF_8_DB_username, 64, nullptr, nullptr);
		WideCharToMultiByte(0, 0, pNetLoginConfig->DB_password, 64, UTF_8_DB_password, 64, nullptr, nullptr);
		WideCharToMultiByte(0, 0, pNetLoginConfig->DB_dbname, 64, UTF_8_DB_dbname, 64, nullptr, nullptr);

		m_pDBConnectorTLS = new CDBConnectorTLS(UTF_8_DB_ip, UTF_8_DB_username, UTF_8_DB_password, UTF_8_DB_dbname, pNetLoginConfig->DB_port);

		if (m_pDBConnectorTLS == nullptr)
			OnError(GetLastError(), _T("[CNetServerLogin] Mem Alloc Failed [GetLastError : %d]"));

		// ä�ü��� �������� ���� 
		HRESULT result;

		result = StringCchCopyW(m_ChatServerIP, 16, pNetLoginConfig->ChatServerIP);

		if (FAILED(result))
		{
			OnError(GetLastError(), _T("[CNetServerLogin] StringCchCopyA failed Failed [GetLastError : %d]"));			
		}

		m_ChatServerPort = pNetLoginConfig->ChatServerPort;
			   		 	  
		// CNetServerLogin ���� ����
		bool bOk = Start(
			pNetLoginConfig->BindIP,
			pNetLoginConfig->Port,
			pNetLoginConfig->ConcurrentThreads,
			pNetLoginConfig->MaxThreads,
			pNetLoginConfig->IsNoDelay,
			pNetLoginConfig->MaxSessions,
			pNetLoginConfig->PacketCode,
			pNetLoginConfig->PacketKey1,
			pNetLoginConfig->PacketKey2
		);

		// ���� �������н� ũ����
		if (bOk == false)
			OnError(GGM_ERROR::STARTUP_FAILED, _T("[CNetServerLogin] Start Failed %d"));

		// CLanServerLogin �����Ҵ�
		m_pLanLogin = new CLanServerLogin(pLanLoginConfig, this, m_UserArray);

		if (m_pLanLogin == nullptr)
			OnError(GetLastError(), _T("[CNetServerLogin] Mem Alloc Failed [GetLastError : %d]"));
	}

	CNetServerLogin::~CNetServerLogin()
	{
		// ���� �迭 �Ҵ�����
		delete m_UserArray;		

		// CLanServerLogin �Ҵ�����		
		delete m_pLanLogin;

		// ���� ����
		Stop();

		// TLS DBConnector �Ҵ�����
		delete m_pDBConnectorTLS;
	}

	void CNetServerLogin::Res_Login_Proc(INT64 AccountNo, INT64 SessionID, BYTE Status, TCHAR *IP, USHORT Port)
	{
		// CLanServerLogin�� ���Ӽ����� ä�ü����κ��� ������ū ���ſ� ���� ������ �޾Ҵ�.
		// ������ CNetServerLogin�� ������ ���Ӽ����� ä�ü����� �̰��ؾ� �Ѵ�.

		// ���� Param ���� CNetServerLogin�� �����ϴ� �������� ����Ű�� ����.
		User *pUser = &m_UserArray[(WORD)SessionID];

		// �ش� ������ ���Ἲ Ȯ��
		// ������ �α��� ������ �α����� ��û�ϰ��� ���Ӽ����� ä�ü����� ���� ��û�� �� �Ŀ�
		// �ٷ� ������ �����ٸ� ���������� �ùٸ��� ���� ���̴�.
		// ������ �ٸ��ٸ� ���� ó���ؾ� �� ������ ���� �׳� �� �̻� ����ó�� �������� �ʴ´�.
		if (pUser->SessionID != SessionID || pUser->AccountNo != AccountNo)
			return;		

		// ������ �̻���ٸ� ������ ���Ӽ���, ä�ü����� ���� 
		CNetPacket *pPacket = CNetPacket::Alloc();

		// ��Ŷ ����	
		CreatePacket_CS_LOGIN_RES(
			pPacket,
			AccountNo,
			Status,
			pUser->ID,
			pUser->Nick,
			nullptr,
			0,
			m_ChatServerIP,
			m_ChatServerPort
		);

		// Ŭ�󿡰� ��Ŷ �۽�
		// ������ ���� ����� ���� ä�ü����� ���� 
		SendPacketAndDisconnect(SessionID, pPacket);

		// ALLOC�� ���� ����
		CNetPacket::Free(pPacket);
	}

	void CNetServerLogin::PrintInfo()
	{
		ULONGLONG AcceptTPS = InterlockedAnd(&m_AcceptTPS, 0);	
		LONG64    SendTPS = InterlockedAnd64(&m_SendTPS, 0);
		ULONGLONG RecvTPS = InterlockedAnd(&m_RecvTPS, 0);

		_tprintf_s(_T("================================= LOGIN SERVER =================================\n"));
		_tprintf_s(_T("[Session Count]        : %lld\n"), GetSessionCount());
		_tprintf_s(_T("[Login User Count]     : %lld\n\n"), m_UserCount);
	
		_tprintf_s(_T("[NetPacket Chunk]         : [ %lld / %lld ]\n"), CNetPacket::PacketPool->GetNumOfChunkInUse(), CNetPacket::PacketPool->GetTotalNumOfChunk());
		_tprintf_s(_T("[NetPacket Node Usage]    : %lld\n\n"), CNetPacket::PacketPool->GetNumOfChunkNodeInUse());		

		_tprintf_s(_T("[LanPacket Chunk]         : [ %lld / %lld ]\n"), CSerialBuffer::PacketPool->GetNumOfChunkInUse(), CSerialBuffer::PacketPool->GetTotalNumOfChunk());
		_tprintf_s(_T("[LanPacket Node Usage]    : %lld\n\n"), CSerialBuffer::PacketPool->GetNumOfChunkNodeInUse());

		_tprintf_s(_T("[Total Accept]         : %lld\n"), m_AcceptTotal);
		_tprintf_s(_T("[Accept TPS]           : %lld\n"), AcceptTPS);	

		_tprintf_s(_T("[Packet Recv TPS]      : %lld\n"), RecvTPS);
		_tprintf_s(_T("[Packet Send TPS]      : %lld\n"), SendTPS);
		_tprintf_s(_T("================================= LOGIN SERVER =================================\n"));
	}
	
	void GGM::CNetServerLogin::OnClientJoin(const SOCKADDR_IN & ClientAddr, ULONGLONG SessionID)
	{
		// ���� �ش� �迭�� �ε����� ������ ���� �� �ִ��� üũ
		if (m_UserArray[(WORD)SessionID].SessionID != INIT_SESSION_ID)
		{
			// OnClientLeave���� �ش� �迭 �ε����� ���̵� INIT_SESSION_ID���� �ʱ�ȭ�ȴ�.
			// ���� ���Դٸ� ���� ����ȭ ���� �߻��� ���̹Ƿ� ����� �ʿ� 
			OnError(GGM_ERROR::SESSION_SYNC_FAILED, _T("[CNetServerLogin] OnClientJoin Session Sync Failed %d"));
		}

		// ���� ���� ä��
		m_UserArray[(WORD)SessionID].SessionID = SessionID;

		// ���� �� ����
		InterlockedIncrement(&m_UserCount);
	}

	void GGM::CNetServerLogin::OnClientLeave(ULONGLONG SessionID)
	{
		// ������ �α��� ������ ������.
		// SessionID�� ���� �ʱ�ȭ���ش�.
		m_UserArray[(WORD)SessionID].SessionID = INIT_SESSION_ID;

		// ������ ����
		InterlockedDecrement(&m_UserCount);
	}

	bool GGM::CNetServerLogin::OnConnectionRequest(const SOCKADDR_IN & ClientAddr)
	{
		// ������ �� ���� ����.
		return true;
	}

	void GGM::CNetServerLogin::OnRecv(ULONGLONG SessionID, CNetPacket * pPacket)
	{
		// NetServerLogin�� �ܺ� ��Ʈ��ũ Ŭ���̾�Ʈ�κ��� �޴� ��Ŷ�� �ϳ����̴�.
		// en_PACKET_CS_LOGIN_REQ_LOGIN
		// �α��� ��û�� ���� ��Ŷ ���� ó���� �̰����� �����Ѵ�.
		WORD PacketType;
		int Result = pPacket->Dequeue((char*)&PacketType, sizeof(WORD));
		if (Result == GGM_PACKET_ERROR)
		{
			Disconnect(SessionID);
			return;
		}

		// Ÿ��üũ
		if (PacketType != (WORD)en_PACKET_CS_LOGIN_REQ_LOGIN)
		{
			// �̻��� Ÿ���� ��Ŷ�� ���� Ŭ���̾�Ʈ�� ���� ����
			OnError(GGM_ERROR::WRONG_PACKET_TYPE, _T("WRONG PACKET TYPE!\n"));
			Disconnect(SessionID);
			return;
		}		
		
		INT64 AccountNo;
		Result = pPacket->Dequeue((char*)&AccountNo, sizeof(INT64));
		if (Result == GGM_PACKET_ERROR)
		{
			Disconnect(SessionID);
			return;
		}
		
		// DB���� �ش� AccountNo�� ��Ī�Ǵ� �������� �ҷ��´�.
		// �� TLS�� ����� DBConnector ���´�.
		CDBConnector *pDBConnector = m_pDBConnectorTLS->GetTlsDBConnector();

		// ���� �۽�		
		pDBConnector->SendQuery(true, "SELECT * FROM accountdb.v_account WHERE accountno = %lld", AccountNo);

		// ����� ���´�.
		MYSQL_ROW result = pDBConnector->FetchRow();	

		if(result == nullptr)
		{
			// SELECT ���� ���� ���� �۽��� �����ߴµ� result�� nullptr�̶�� AccountNo�� �ش��ϴ� ����� ���� ���̴�.
			// dfLOGIN_STATUS_ACCOUNT_MISS ����						
			Res_Login_Proc(AccountNo, SessionID, dfLOGIN_STATUS_ACCOUNT_MISS, nullptr, 0);
			return;
		}
		
		// ���� �׽�Ʈ ������ �ش� ������ ������ �˻��ؾ� �� ����� 
		///////////////////////////////////////////////////////////////////////////////////////////////////////
		//if (result[3] == NULL)
		//{
		//	// SELECT ���� ���� ���� �۽��� �����ߴµ� sessionKey �÷��� NULL
		//	// dfLOGIN_STATUS_SESSION_MISS
		//	Res_Login_Proc(AccountNo, SessionID, dfLOGIN_STATUS_SESSION_MISS, nullptr, 0);
		//	return;
		//}

		//if (result[4] == NULL)
		//{
		//	// SELECT ���� ���� ���� �۽��� �����ߴµ� status �÷��� NULL
		//	// dfLOGIN_STATUS_STATUS_MISS
		//	Res_Login_Proc(AccountNo, SessionID, dfLOGIN_STATUS_STATUS_MISS, nullptr, 0);
		//	return;
		//}	

		// �α��� ���ɻ��������˻�		
		//if (result[4] != _T('0'))
		//{
		//	// ���� �α��� ���� ���°� �ƴ϶�� ����
		//	// dfLOGIN_STATUS_FAIL
		//	Res_Login_Proc(AccountNo, SessionID, dfLOGIN_STATUS_FAIL, nullptr, 0);
		//	return;
		//}			

		// �߰��� ����Ű�� ��ȿ���� �˻��ؾ��Ѵ�.
		//if (����Ű�� ��ȿ���� ������)
		//{
		//	Res_Login_Proc(AccountNo, SessionID, dfLOGIN_STATUS_FAIL, nullptr, 0);
		//	return;
		//}
		///////////////////////////////////////////////////////////////////////////////////////////////////////

		// �̻��� ���ٸ�
		// ���� ������ �α��� ������ ���� �迭�� ����
		User *pUser = &m_UserArray[(WORD)SessionID];

		// ���� ����ȭ�� ������ �ִٸ� ũ����
		if (pUser->SessionID == INIT_SESSION_ID)
			OnError(GGM_ERROR::SESSION_SYNC_FAILED, _T("[CNetServerLogin] OnRecv Session Sync Failed %d"));

		// Account ���� ����
		pUser->AccountNo = AccountNo;

		// ��񿡼� �ܾ�� ���̵�� �г����� UTF-8���ڵ��̴�.
		// �޸𸮿� �����Ҷ� UTF-16���� �ٲپ��ش�.
		char *pResult = result[1];
		char *pID = (char*)pUser->ID;
		char *pNick = (char*)pUser->Nick;
			
		///////////////////////////////////////////////////
		// UTF - 8 ->> UTF - 16 ��ȯ����
		while (*pResult != 0)
		{
			*pID = *pResult;
			*(pID + 1) = 0;
			pID += 2;
			pResult++;
		}

		*(WORD*)pID = (WORD)0;
		pResult++;

		while (*pResult != 0)
		{
			*pNick = *pResult;
			*(pNick + 1) = 0;
			pNick += 2;
			pResult++;
		}

		*(WORD*)pNick = (WORD)0;
		// UTF - 8 ->> UTF - 16 ��ȯ�Ϸ�
		///////////////////////////////////////////////////		
		
		// ����� ����
		if(result != nullptr)
			pDBConnector->FreeResult();	

		// CLanServerLogin���� �α��� �� ���� ������ ä�ü����� ���Ӽ����� ������ ��
		m_pLanLogin->SendToken(AccountNo, (char*)pPacket->GetReadPtr(), SessionID);
	}

	void GGM::CNetServerLogin::OnSend(ULONGLONG SessionID, int SendSize)
	{		
	}

	void GGM::CNetServerLogin::OnWorkerThreadBegin()
	{

	}

	void GGM::CNetServerLogin::OnWorkerThreadEnd()
	{

	}

	void GGM::CNetServerLogin::OnError(int ErrorNo, const TCHAR * ErrorMsg)
	{
		CLogger::GetInstance()->Log(_T("LoginServer"), LEVEL::DBG, OUTMODE::FILE, ErrorMsg, ErrorNo);
		CCrashDump::ForceCrash();
	}

	void CNetServerLogin::CreatePacket_CS_LOGIN_RES(
		CNetPacket * pPacket,
		INT64 AccountNo,
		BYTE Status,
		TCHAR * ID,
		TCHAR * Nick,
		TCHAR * GameServerIP,
		USHORT GameServerPort,
		TCHAR * ChatServerIP, USHORT ChatServerPort
	)
	{
		//------------------------------------------------------------
		// �α��� �������� Ŭ���̾�Ʈ�� �α��� ����
		//
		//	{
		//		WORD	Type
		//
		//		INT64	AccountNo
		//		BYTE	Status				// 0 (���ǿ���) / 1 (����) ...  �ϴ� defines ���
		//
		//		WCHAR	ID[20]				// ����� ID		. null ����
		//		WCHAR	Nickname[20]		// ����� �г���	. null ����
		//
		//		WCHAR	GameServerIP[16]	// ���Ӵ�� ����,ä�� ���� ����
		//		USHORT	GameServerPort
		//		WCHAR	ChatServerIP[16]
		//		USHORT	ChatServerPort
		//	}
		//
		//------------------------------------------------------------

		WORD PacketType = (WORD)en_PACKET_CS_LOGIN_RES_LOGIN;

		pPacket->Enqueue((char*)&PacketType, sizeof(WORD));
		pPacket->Enqueue((char*)&AccountNo, sizeof(INT64));
		pPacket->Enqueue((char*)&Status, sizeof(BYTE));		
		pPacket->Enqueue((char*)ID, ID_LEN * sizeof(TCHAR));
		pPacket->Enqueue((char*)Nick, NICK_LEN * sizeof(TCHAR));		

		// ���Ӽ��� �ּ����� �ǳʶ�
		pPacket->RelocateWrite((IP_LEN * sizeof(TCHAR)) + sizeof(USHORT));				
		
		// ä�� ���� �ּ� ����
		pPacket->Enqueue((char*)ChatServerIP, IP_LEN * sizeof(TCHAR));
		pPacket->Enqueue((char*)&ChatServerPort, sizeof(USHORT));				
	}

	bool NetServerLoginConfig::LoadConfig(const TCHAR * ConfigFileName)
	{
		CParser parser;

		// ���� ���� ���� �ε�
		bool Ok = parser.LoadFile(ConfigFileName);		
		DWORD err = GetLastError();

		// ���� ������ ��� ������ �α׷� �����.
		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("LOGIN_SERVER CONFIG FILE OPEN FAILED"));
			return 0;
		}

		CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("========== NET_SERVER_LOGIN OPEN START ========== "));

		// ���� ����
		parser.SetSpace(_T("#NET_SERVER_LOGIN"));

		// BIND_IP LOAD
		Ok = parser.GetValue(_T("BIND_IP"), BindIP);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET BIND_IP FAILED : [%s]"), BindIP);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("BIND_IP : [%s]"), BindIP);

		// PORT LOAD
		Ok = parser.GetValue(_T("SERVER_PORT"), (short*)&Port);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET SERVER_PORT FAILED : [%hd]"), Port);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("SERVER_PORT : [%hd]"), Port);

		// CHAT SERVER �ּ� ����
		Ok = parser.GetValue(_T("CHATSERVER_IP"), ChatServerIP);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET CHATSERVER_IP FAILED : [%s]"), ChatServerIP);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("CHATSERVER_IP : [%s]"), ChatServerIP);

		// CHAT SERVER �ּ� ����
		Ok = parser.GetValue(_T("CHATSERVER_PORT"), (short*)&ChatServerPort);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET CHATSERVER_PORT FAILED : [%s]"), ChatServerPort);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("CHATSERVER_PORT : [%hd]"), ChatServerPort);

		// ConcurrentThreads LOAD
		Ok = parser.GetValue(_T("CONCURRENT_THREADS"), (int*)&ConcurrentThreads);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET CONCURRENT_THREADS FAILED : [%d]"), ConcurrentThreads);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("CONCURRENT_THREADS : [%d]"), ConcurrentThreads);

		// MaxThreads LOAD
		Ok = parser.GetValue(_T("MAX_THREADS"), (int*)&MaxThreads);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET MAX_THREADS FAILED : [%d]"), MaxThreads);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("MAX_THREADS : [%d]"), MaxThreads);

		// IsNoDelay LOAD
		Ok = parser.GetValue(_T("NO_DELAY"), (bool*)&IsNoDelay);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET NO_DELAY FAILED : [%d]"), IsNoDelay);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("NO_DELAY : [%d]"), IsNoDelay);

		// MaxSessions LOAD
		Ok = parser.GetValue(_T("MAX_SESSION"), (short*)&MaxSessions);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET MAX_SESSION FAILED : [%d]"), MaxSessions);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("MAX_SESSION : [%d]"), MaxSessions);

		// PacketCode LOAD
		Ok = parser.GetValue(_T("PACKET_CODE"), (char*)&PacketCode);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET PACKET_CODE FAILED : [%d]"), PacketCode);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("PACKET_CODE : [%d]"), PacketCode);

		// PacketKey1 LOAD
		Ok = parser.GetValue(_T("PACKET_KEY1"), (char*)&PacketKey1);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET PACKET_KEY1 FAILED : [%d]"), PacketKey1);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("PACKET_KEY1 : [%d]"), PacketKey1);

		// PacketKey2 LOAD
		Ok = parser.GetValue(_T("PACKET_KEY2"), (char*)&PacketKey2);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET PACKET_KEY2  FAILED : [%d]"), PacketKey2);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("PACKET_KEY2 : [%d]"), PacketKey2);

		// LogLevel LOAD
		Ok = parser.GetValue(_T("LOG_LEVEL"), (int*)&LogLevel);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET LOG_LEVEL FAILED : [%d]"), LogLevel);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("LOG_LEVEL : [%d]"), LogLevel);
		CLogger::GetInstance()->SetDefaultLogLevel((LEVEL)LogLevel);		

		// ��� ���� ����
		Ok = parser.GetValue(_T("DB_IP"), DB_ip);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET DB_IP FAILED : [%s]"), DB_ip);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("DB_IP : [%s]"), DB_ip);

		Ok = parser.GetValue(_T("DB_USERNAME"), DB_username);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET DB_USERNAME FAILED : [%s]"), DB_username);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("DB_USERNAME : [%s]"), DB_username);

		Ok = parser.GetValue(_T("DB_PASSWORD"), DB_password);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET DB_PASSWORD FAILED : [%s]"), DB_password);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("DB_PASSWORD : [%s]"), DB_password);

		Ok = parser.GetValue(_T("DB_DBNAME"), DB_dbname);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET DB_DBNAME FAILED : [%s]"), DB_dbname);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("DB_DBNAME : [%s]"), DB_dbname);

		Ok = parser.GetValue(_T("DB_PORT"), (short*)&DB_port);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET DB_PORT FAILED : [%hd]"), DB_port);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("DB_PORT : [%hd]"), DB_port);

		CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("========== NET_SERVER_LOGIN OPEN SUCCESSFUL ========== "));

		return true;
	}
}


