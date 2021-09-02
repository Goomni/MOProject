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
		// 유저 배열 동적할당
		m_UserArray = new User[pNetLoginConfig->MaxSessions];

		if (m_UserArray == nullptr)
			OnError(GetLastError(), _T("[CNetServerLogin] Mem Alloc Failed [GetLastError : %d]"));

		// TLS DBConnector 동적할당
		char  UTF_8_DB_ip[16];
		char  UTF_8_DB_username[64];
		char  UTF_8_DB_password[64];
		char  UTF_8_DB_dbname[64];

		// 설정파일에서 읽어온 디비 연결정보는 UTF-16 정보이므로 UTF-8로 변환한다.
		WideCharToMultiByte(0, 0, pNetLoginConfig->DB_ip, 16, UTF_8_DB_ip, 16, nullptr, nullptr);
		WideCharToMultiByte(0, 0, pNetLoginConfig->DB_username, 64, UTF_8_DB_username, 64, nullptr, nullptr);
		WideCharToMultiByte(0, 0, pNetLoginConfig->DB_password, 64, UTF_8_DB_password, 64, nullptr, nullptr);
		WideCharToMultiByte(0, 0, pNetLoginConfig->DB_dbname, 64, UTF_8_DB_dbname, 64, nullptr, nullptr);

		m_pDBConnectorTLS = new CDBConnectorTLS(UTF_8_DB_ip, UTF_8_DB_username, UTF_8_DB_password, UTF_8_DB_dbname, pNetLoginConfig->DB_port);

		if (m_pDBConnectorTLS == nullptr)
			OnError(GetLastError(), _T("[CNetServerLogin] Mem Alloc Failed [GetLastError : %d]"));

		// 채팅서버 연결정보 저장 
		HRESULT result;

		result = StringCchCopyW(m_ChatServerIP, 16, pNetLoginConfig->ChatServerIP);

		if (FAILED(result))
		{
			OnError(GetLastError(), _T("[CNetServerLogin] StringCchCopyA failed Failed [GetLastError : %d]"));			
		}

		m_ChatServerPort = pNetLoginConfig->ChatServerPort;
			   		 	  
		// CNetServerLogin 서버 구동
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

		// 서버 구동실패시 크래쉬
		if (bOk == false)
			OnError(GGM_ERROR::STARTUP_FAILED, _T("[CNetServerLogin] Start Failed %d"));

		// CLanServerLogin 동적할당
		m_pLanLogin = new CLanServerLogin(pLanLoginConfig, this, m_UserArray);

		if (m_pLanLogin == nullptr)
			OnError(GetLastError(), _T("[CNetServerLogin] Mem Alloc Failed [GetLastError : %d]"));
	}

	CNetServerLogin::~CNetServerLogin()
	{
		// 유저 배열 할당해제
		delete m_UserArray;		

		// CLanServerLogin 할당해제		
		delete m_pLanLogin;

		// 서버 종료
		Stop();

		// TLS DBConnector 할당해제
		delete m_pDBConnectorTLS;
	}

	void CNetServerLogin::Res_Login_Proc(INT64 AccountNo, INT64 SessionID, BYTE Status, TCHAR *IP, USHORT Port)
	{
		// CLanServerLogin이 게임서버와 채팅서버로부터 인증토큰 수신에 대한 응답을 받았다.
		// 이제는 CNetServerLogin이 유저를 게임서버와 채팅서버로 이관해야 한다.

		// 고유 Param 값은 CNetServerLogin이 관리하는 유저들의 세션키와 같다.
		User *pUser = &m_UserArray[(WORD)SessionID];

		// 해당 유저의 무결성 확인
		// 유저가 로그인 서버로 로그인을 요청하고나서 게임서버와 채팅서버로 인증 요청이 간 후에
		// 바로 접속을 끊었다면 유저정보가 올바르지 않을 것이다.
		// 정보가 다르다면 딱히 처리해야 할 로직은 없고 그냥 더 이상 인증처리 진행하지 않는다.
		if (pUser->SessionID != SessionID || pUser->AccountNo != AccountNo)
			return;		

		// 정보가 이상없다면 유저를 게임서버, 채팅서버로 보냄 
		CNetPacket *pPacket = CNetPacket::Alloc();

		// 패킷 생성	
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

		// 클라에게 패킷 송신
		// 보내고 끊기 기능을 통해 채팅서버로 보냄 
		SendPacketAndDisconnect(SessionID, pPacket);

		// ALLOC에 대한 프리
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
		// 현재 해당 배열의 인덱스에 세션을 받을 수 있는지 체크
		if (m_UserArray[(WORD)SessionID].SessionID != INIT_SESSION_ID)
		{
			// OnClientLeave에서 해당 배열 인덱스의 아이디가 INIT_SESSION_ID으로 초기화된다.
			// 여기 들어왔다면 세션 동기화 문제 발생한 것이므로 디버깅 필요 
			OnError(GGM_ERROR::SESSION_SYNC_FAILED, _T("[CNetServerLogin] OnClientJoin Session Sync Failed %d"));
		}

		// 유저 정보 채움
		m_UserArray[(WORD)SessionID].SessionID = SessionID;

		// 유저 수 증가
		InterlockedIncrement(&m_UserCount);
	}

	void GGM::CNetServerLogin::OnClientLeave(ULONGLONG SessionID)
	{
		// 유저가 로그인 서버를 떠났다.
		// SessionID의 값을 초기화해준다.
		m_UserArray[(WORD)SessionID].SessionID = INIT_SESSION_ID;

		// 유저수 감소
		InterlockedDecrement(&m_UserCount);
	}

	bool GGM::CNetServerLogin::OnConnectionRequest(const SOCKADDR_IN & ClientAddr)
	{
		// 지금은 할 것이 없다.
		return true;
	}

	void GGM::CNetServerLogin::OnRecv(ULONGLONG SessionID, CNetPacket * pPacket)
	{
		// NetServerLogin이 외부 네트워크 클라이언트로부터 받는 패킷은 하나뿐이다.
		// en_PACKET_CS_LOGIN_REQ_LOGIN
		// 로그인 요청에 대한 패킷 수신 처리를 이곳에서 진행한다.
		WORD PacketType;
		int Result = pPacket->Dequeue((char*)&PacketType, sizeof(WORD));
		if (Result == GGM_PACKET_ERROR)
		{
			Disconnect(SessionID);
			return;
		}

		// 타입체크
		if (PacketType != (WORD)en_PACKET_CS_LOGIN_REQ_LOGIN)
		{
			// 이상한 타입의 패킷을 보낸 클라이언트는 연결 종료
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
		
		// DB에서 해당 AccountNo와 매칭되는 계정정보 불러온다.
		// 내 TLS에 저장된 DBConnector 얻어온다.
		CDBConnector *pDBConnector = m_pDBConnectorTLS->GetTlsDBConnector();

		// 쿼리 송신		
		pDBConnector->SendQuery(true, "SELECT * FROM accountdb.v_account WHERE accountno = %lld", AccountNo);

		// 결과셋 얻어온다.
		MYSQL_ROW result = pDBConnector->FetchRow();	

		if(result == nullptr)
		{
			// SELECT 문에 대한 쿼리 송신은 성공했는데 result가 nullptr이라면 AccountNo에 해당하는 결과가 없는 것이다.
			// dfLOGIN_STATUS_ACCOUNT_MISS 오류						
			Res_Login_Proc(AccountNo, SessionID, dfLOGIN_STATUS_ACCOUNT_MISS, nullptr, 0);
			return;
		}
		
		// 더미 테스트 에서는 해당 사항이 없지만 검사해야 할 내용들 
		///////////////////////////////////////////////////////////////////////////////////////////////////////
		//if (result[3] == NULL)
		//{
		//	// SELECT 문에 대한 쿼리 송신은 성공했는데 sessionKey 컬럼이 NULL
		//	// dfLOGIN_STATUS_SESSION_MISS
		//	Res_Login_Proc(AccountNo, SessionID, dfLOGIN_STATUS_SESSION_MISS, nullptr, 0);
		//	return;
		//}

		//if (result[4] == NULL)
		//{
		//	// SELECT 문에 대한 쿼리 송신은 성공했는데 status 컬럼이 NULL
		//	// dfLOGIN_STATUS_STATUS_MISS
		//	Res_Login_Proc(AccountNo, SessionID, dfLOGIN_STATUS_STATUS_MISS, nullptr, 0);
		//	return;
		//}	

		// 로그인 가능상태인지검사		
		//if (result[4] != _T('0'))
		//{
		//	// 현재 로그인 가능 상태가 아니라면 문제
		//	// dfLOGIN_STATUS_FAIL
		//	Res_Login_Proc(AccountNo, SessionID, dfLOGIN_STATUS_FAIL, nullptr, 0);
		//	return;
		//}			

		// 추가로 세션키의 유효성도 검사해야한다.
		//if (세션키가 유효하지 않으면)
		//{
		//	Res_Login_Proc(AccountNo, SessionID, dfLOGIN_STATUS_FAIL, nullptr, 0);
		//	return;
		//}
		///////////////////////////////////////////////////////////////////////////////////////////////////////

		// 이상이 없다면
		// 유저 정보를 로그인 서버의 유저 배열에 저장
		User *pUser = &m_UserArray[(WORD)SessionID];

		// 세션 동기화에 문제가 있다면 크래쉬
		if (pUser->SessionID == INIT_SESSION_ID)
			OnError(GGM_ERROR::SESSION_SYNC_FAILED, _T("[CNetServerLogin] OnRecv Session Sync Failed %d"));

		// Account 정보 저장
		pUser->AccountNo = AccountNo;

		// 디비에서 긁어온 아이디와 닉네임은 UTF-8인코딩이다.
		// 메모리에 저장할때 UTF-16으로 바꾸어준다.
		char *pResult = result[1];
		char *pID = (char*)pUser->ID;
		char *pNick = (char*)pUser->Nick;
			
		///////////////////////////////////////////////////
		// UTF - 8 ->> UTF - 16 변환시작
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
		// UTF - 8 ->> UTF - 16 변환완료
		///////////////////////////////////////////////////		
		
		// 결과셋 프리
		if(result != nullptr)
			pDBConnector->FreeResult();	

		// CLanServerLogin에게 로그인 할 유저 정보를 채팅서버와 게임서버로 보내게 함
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
		// 로그인 서버에서 클라이언트로 로그인 응답
		//
		//	{
		//		WORD	Type
		//
		//		INT64	AccountNo
		//		BYTE	Status				// 0 (세션오류) / 1 (성공) ...  하단 defines 사용
		//
		//		WCHAR	ID[20]				// 사용자 ID		. null 포함
		//		WCHAR	Nickname[20]		// 사용자 닉네임	. null 포함
		//
		//		WCHAR	GameServerIP[16]	// 접속대상 게임,채팅 서버 정보
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

		// 게임서버 주소정보 건너뜀
		pPacket->RelocateWrite((IP_LEN * sizeof(TCHAR)) + sizeof(USHORT));				
		
		// 채팅 서버 주소 복사
		pPacket->Enqueue((char*)ChatServerIP, IP_LEN * sizeof(TCHAR));
		pPacket->Enqueue((char*)&ChatServerPort, sizeof(USHORT));				
	}

	bool NetServerLoginConfig::LoadConfig(const TCHAR * ConfigFileName)
	{
		CParser parser;

		// 서버 설정 파일 로드
		bool Ok = parser.LoadFile(ConfigFileName);		
		DWORD err = GetLastError();

		// 서버 구동시 모든 정보는 로그로 남긴다.
		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("LOGIN_SERVER CONFIG FILE OPEN FAILED"));
			return 0;
		}

		CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("========== NET_SERVER_LOGIN OPEN START ========== "));

		// 구역 설정
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

		// CHAT SERVER 주소 정보
		Ok = parser.GetValue(_T("CHATSERVER_IP"), ChatServerIP);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET CHATSERVER_IP FAILED : [%s]"), ChatServerIP);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("CHATSERVER_IP : [%s]"), ChatServerIP);

		// CHAT SERVER 주소 정보
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

		// 디비 연결 정보
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


