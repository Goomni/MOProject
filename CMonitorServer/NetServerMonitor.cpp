#include "NetServerMonitor.h"
#include "LanServerMonitor.h"
#include "CrashDump\CrashDump.h"
#include "Define\GGM_CONSTANTS.h"
#include "Define\GGM_ERROR.h"
#include "Logger\Logger.h"
#include "Protocol\CommonProtocol.h"
#include "Parser\Parser.h"

namespace GGM
{
	GGM::CNetServerMonitor::CNetServerMonitor(NetServerMonitorConfig * pNetConfig, LanServerMonitorConfig * pLanConfig)
	{
		// 배열 동적할당
		m_ViewerArr = new Viewer[pNetConfig->MaxSessions];
		m_MaxViewer = pNetConfig->MaxSessions;

		// 배열 락 초기화
		InitializeSRWLock(&m_Lock);

		// 고정 세션키 복사
		// 고정 세션키는 설정파일에서 가져오기 때문에 UTF-16이다.
		// UTF-8로 변환한다.		
		int ret = WideCharToMultiByte(0, 0, pNetConfig->LoginSessionkey, 32, m_SessionKey, 33, NULL, NULL);

		if (ret == 0)
			OnError(GetLastError(), _T("[CNetServerMonitor] Ctor, WideCharToMultiByte Failed %d"));

		// 서버 구동 
		bool bOk = Start(
			pNetConfig->BindIP,
			pNetConfig->Port,
			pNetConfig->ConcurrentThreads,
			pNetConfig->MaxThreads,
			pNetConfig->IsNoDelay,
			pNetConfig->MaxSessions,
			pNetConfig->PacketCode,
			pNetConfig->PacketKey			
		);

		if (bOk == false)
			OnError(GGM_ERROR::STARTUP_FAILED, _T("[CNetServerMonitor] StartUp Failed %d"));

		// LanClientMonitor 생성
		m_pLanMonitor = new CLanServerMonitor(pLanConfig, this);

		if (m_pLanMonitor == nullptr)
			OnError(GetLastError(), _T("[CNetServerMonitor] m_pLanMonitor = new CLanServerMonitor(pLanConfig, this) fail %d"));		
	}

	CNetServerMonitor::~CNetServerMonitor()
	{
		// LanClientMonitor Delete
		delete m_pLanMonitor;

		// 서버종료
		Stop();

		// 뷰어 관리배열 할당해제
		delete m_ViewerArr;
	}	

	void GGM::CNetServerMonitor::OnClientJoin(const SOCKADDR_IN & ClientAddr, ULONGLONG SessionID)
	{
		AcquireSRWLockExclusive(&m_Lock);

		// 세션 동기화에 문제가 없는지 검사
		if (m_ViewerArr[(WORD)SessionID].SessionID != INIT_SESSION_ID)
		{
			OnError(GGM_ERROR::SESSION_SYNC_FAILED, _T("[CNetServerMonitor] OnClientJoin Failed %d"));
			ReleaseSRWLockExclusive(&m_Lock);
			return;
		}

		// 접속한 모니터링 뷰어를 배열에 추가
		m_ViewerArr[(WORD)SessionID].SessionID = SessionID;	

		// 카운터 증가
		++m_ViewerCount;

		ReleaseSRWLockExclusive(&m_Lock);	
	}

	void GGM::CNetServerMonitor::OnClientLeave(ULONGLONG SessionID)
	{
		AcquireSRWLockExclusive(&m_Lock);

		// 세션 동기화에 문제가 없는지 검사
		if (m_ViewerArr[(WORD)SessionID].SessionID != SessionID)
		{
			OnError(GGM_ERROR::SESSION_SYNC_FAILED, _T("[CNetServerMonitor] OnClientLeave Failed %d"));
			ReleaseSRWLockExclusive(&m_Lock);
			return;
		}

		// 접속끊은 모니터링 뷰어를 배열에서 삭제
		m_ViewerArr[(WORD)SessionID].SessionID = INIT_SESSION_ID;
		m_ViewerArr[(WORD)SessionID].IsLogin = false;

		// 카운터 감소
		--m_ViewerCount;

		ReleaseSRWLockExclusive(&m_Lock);	
	}

	bool GGM::CNetServerMonitor::OnConnectionRequest(const SOCKADDR_IN & ClientAddr)
	{
		return true;
	}

	void GGM::CNetServerMonitor::OnRecv(ULONGLONG SessionID, CNetPacket * pPacket)
	{
		// CNetServerMonitor가 수신하는 패킷은 모니터링 뷰어의 로그인 요청 패킷이 유일하다.
		Monitor_Tool_Req_Login(SessionID, pPacket);
	}

	void GGM::CNetServerMonitor::OnSend(ULONGLONG SessionID, int SendSize)
	{
		// 할일 없음
	}

	void GGM::CNetServerMonitor::OnWorkerThreadBegin()
	{
		// 할일 없음
	}

	void GGM::CNetServerMonitor::OnWorkerThreadEnd()
	{
		// 할일 없음
	}

	void GGM::CNetServerMonitor::OnError(int ErrorNo, const TCHAR * ErrorMsg)
	{
		CLogger::GetInstance()->Log(_T("Monitor Server"), LEVEL::DBG, OUTMODE::BOTH, ErrorMsg, ErrorNo);
		CCrashDump::ForceCrash();
	}

	void CNetServerMonitor::OnSemError(ULONGLONG SessionID)
	{
		
	}

	void GGM::CNetServerMonitor::Monitor_Tool_Req_Login(ULONGLONG SessionID, CNetPacket * pPacket)
	{
		//------------------------------------------------------------
		// 모니터링 클라이언트(툴) 이 모니터링 서버로 로그인 요청
		//
		//	{
		//		WORD	Type
		//
		//		char	LoginSessionKey[32]		// 로그인 인증 키. (이는 모니터링 서버에 고정값으로 보유)
		//										// 각 모니터링 툴은 같은 키를 가지고 들어와야 함
		//	}
		//
		//------------------------------------------------------------
		//	en_PACKET_CS_MONITOR_TOOL_REQ_LOGIN,		

		// 이 상황이라면 동기화 오류
		if (SessionID != m_ViewerArr[(WORD)SessionID].SessionID)
		{
			OnError(GGM_ERROR::SESSION_SYNC_FAILED, _T("[CNetServerMonitor] Monitor_Tool_Req_Login Invalid SessionKey %d"));
			Disconnect(SessionID);
			return;
		}

		// 패킷 데이터가 부족하면 연결 끊기
		if (pPacket->GetCurrentUsage() < 34)
		{
			Disconnect(SessionID);
			return;
		}
		
		// 패킷 타입 마샬링
		WORD PacketType;
		pPacket->Dequeue((char*)&PacketType, sizeof(WORD));

		// 패킷 타입 이상하면 연결 끊기
		if (PacketType != en_PACKET_CS_MONITOR_TOOL_REQ_LOGIN)
		{
			Disconnect(SessionID);
			return;
		}

		// 로그인 세션키 비교 
		char *pLoginSessionKey = (char*)pPacket->GetReadPtr();
		char *pServerSessionKey = m_SessionKey;		

		if (   *((INT64*)(pServerSessionKey)) != *((INT64*)(pLoginSessionKey))
			|| *((INT64*)(pServerSessionKey + 8)) != *((INT64*)(pLoginSessionKey + 8))
			|| *((INT64*)(pServerSessionKey + 16)) != *((INT64*)(pLoginSessionKey + 16))
			|| *((INT64*)(pServerSessionKey + 24)) != *((INT64*)(pLoginSessionKey + 24))
			)
		{
			// 세션키 다르면 로그인 실패
			// 패킷 보내고 끊음 (SendAndDisconnect 사용)
			Monitor_Tool_Res_Login(SessionID, FALSE);
			return;
		}

		// 그냥 SendPacket()
		Monitor_Tool_Res_Login(SessionID, TRUE);
		
		// 로그인 상태 갱신	
		// 여기서는 락을 걸지 않아도 다른 워커스레드가 건드릴 수가 없기 때문에 안전 
		m_ViewerArr[(WORD)SessionID].IsLogin = true;		
	}

	void GGM::CNetServerMonitor::Monitor_Tool_Res_Login(ULONGLONG SessionID, BYTE status)
	{
		//------------------------------------------------------------
		// 모니터링 클라이언트(툴) 모니터링 서버로 로그인 응답
		// 로그인에 실패하면 0 보내고 끊어버림
		//
		//	{
		//		WORD	Type
		//
		//		BYTE	Status					// 로그인 결과 0 / 1 
		//	}
		//
		//------------------------------------------------------------
		//en_PACKET_CS_MONITOR_TOOL_RES_LOGIN,

		CNetPacket *pPacket = CNetPacket::Alloc();

		CreatePacket_Monitor_Tool_Res_Login(status, pPacket);

		// 로그인 성공이면 성공패킷 정상송신, 실패라면 실패 송신후 연결 끊기
		if (status == TRUE)
			SendPacket(SessionID, pPacket);
		else
			SendPacketAndDisconnect(SessionID, pPacket);

		CNetPacket::Free(pPacket);
	}

	void GGM::CNetServerMonitor::CreatePacket_Monitor_Tool_Res_Login(BYTE status, CNetPacket * pPacket)
	{
		//------------------------------------------------------------
		// 모니터링 클라이언트(툴) 모니터링 서버로 로그인 응답
		// 로그인에 실패하면 0 보내고 끊어버림
		//
		//	{
		//		WORD	Type
		//
		//		BYTE	Status					// 로그인 결과 0 / 1 
		//	}
		//
		//------------------------------------------------------------
		//en_PACKET_CS_MONITOR_TOOL_RES_LOGIN,

		WORD PacketType = en_PACKET_CS_MONITOR_TOOL_RES_LOGIN;

		pPacket->Enqueue((char*)&PacketType, sizeof(WORD));
		pPacket->Enqueue((char*)&status, sizeof(BYTE));
	}

	void CNetServerMonitor::Monitor_Tool_Data_Update(BYTE ServerNo, char * pData)
	{
		//------------------------------------------------------------
		// 모니터링 서버가 모니터링 클라이언트(툴) 에게 모니터링 데이터 전송
		//
		// 모니터링 서버는 모든 모니터링 클라이언트에게 모든 데이터를 뿌려준다.
		//
		// 데이터를 절약하기 위해서는 초단위로 모든 데이터를 묶어서 30~40개의 모니터링 데이터를 하나의 패킷으로 만드는게
		// 좋으나  여러가지 생각할 문제가 많으므로 그냥 각각의 모니터링 데이터를 개별적으로 전송처리 한다.
		//
		//	{
		//		WORD	Type
		//		
		//		BYTE	ServerNo				// 서버 No
		//		BYTE	DataType				// 모니터링 데이터 Type 하단 Define 됨.
		//		int		DataValue				// 해당 데이터 수치.
		//		int		TimeStamp				// 해당 데이터를 얻은 시간 TIMESTAMP  (time() 함수)
		//										// 본래 time 함수는 time_t 타입변수이나 64bit 로 낭비스러우니
		//										// int 로 캐스팅하여 전송. 그래서 2038년 까지만 사용가능
		//	}
		//
		//------------------------------------------------------------
		//en_PACKET_CS_MONITOR_TOOL_DATA_UPDATE,

		CNetPacket *pPacket = CNetPacket::Alloc();

		CreatePacket_Monitor_Tool_Data_Update(ServerNo, pData, pPacket);

		// 여러 뷰어한테 모두 보내야 하므로 락이 필요하다. 
		AcquireSRWLockExclusive(&m_Lock);
		size_t MaxViewerCount = m_MaxViewer;
		Viewer *pViewerArr = m_ViewerArr;
		for (size_t i = 0; i < MaxViewerCount; i++)
		{
			// 접속 후 로그인 성공한 뷰어에게만 패킷 보낸다.
			if(pViewerArr[i].SessionID != INIT_SESSION_ID && pViewerArr[i].IsLogin == true)
				SendPacket(pViewerArr[i].SessionID, pPacket);
		}
		ReleaseSRWLockExclusive(&m_Lock);

		// Alloc 에 대한 Free
		CNetPacket::Free(pPacket);
	}

	void CNetServerMonitor::PrintInfo()
	{
		ULONGLONG AcceptTPS = InterlockedAnd(&m_AcceptTPS, 0);
		LONG64    SendTPS = InterlockedAnd64(&m_SendTPS, 0);
		ULONGLONG RecvTPS = InterlockedAnd64(&m_RecvTPS, 0);

		_tprintf_s(_T("================================= MONITORING SERVER =================================\n"));
		_tprintf_s(_T("[NetSession Count]     : %lld\n"), GetSessionCount());
		_tprintf_s(_T("[Viewer Count]         : %lld\n"), m_ViewerCount);
		_tprintf_s(_T("[LanSession Count]     : %lld\n"), m_pLanMonitor->GetSessionCount());
		_tprintf_s(_T("[LanClient Count]      : %lld\n\n"), m_pLanMonitor->GetLanClientCount());

		_tprintf_s(_T("[NetPacket Chunk]         : [ %lld / %lld ]\n"), CNetPacket::PacketPool->GetNumOfChunkInUse(), CNetPacket::PacketPool->GetTotalNumOfChunk());
		_tprintf_s(_T("[NetPacket Node Usage]    : %lld\n\n"), CNetPacket::PacketPool->GetNumOfChunkNodeInUse());

		_tprintf_s(_T("[LanPacket Chunk]         : [ %lld / %lld ]\n"), CPacket::PacketPool->GetNumOfChunkInUse(), CPacket::PacketPool->GetTotalNumOfChunk());
		_tprintf_s(_T("[LanPacket Node Usage]    : %lld\n\n"), CPacket::PacketPool->GetNumOfChunkNodeInUse());

		_tprintf_s(_T("[Total Accept]         : %lld\n"), m_AcceptTotal);
		_tprintf_s(_T("[Accept TPS]           : %lld\n"), AcceptTPS);

		_tprintf_s(_T("[Packet Recv TPS]      : %lld\n"), RecvTPS);
		_tprintf_s(_T("[Packet Send TPS]      : %lld\n"), SendTPS);
		_tprintf_s(_T("================================= MONITORING SERVER =================================\n"));
	}

	void GGM::CNetServerMonitor::CreatePacket_Monitor_Tool_Data_Update(BYTE ServerNo, char * pData, CNetPacket * pPacket)
	{
		//------------------------------------------------------------
		// 모니터링 서버가 모니터링 클라이언트(툴) 에게 모니터링 데이터 전송
		//
		// 모니터링 서버는 모든 모니터링 클라이언트에게 모든 데이터를 뿌려준다.
		//
		// 데이터를 절약하기 위해서는 초단위로 모든 데이터를 묶어서 30~40개의 모니터링 데이터를 하나의 패킷으로 만드는게
		// 좋으나  여러가지 생각할 문제가 많으므로 그냥 각각의 모니터링 데이터를 개별적으로 전송처리 한다.
		//
		//	{
		//		WORD	Type
		//		
		//		BYTE	ServerNo				// 서버 No
		//		BYTE	DataType				// 모니터링 데이터 Type 하단 Define 됨.
		//		int		DataValue				// 해당 데이터 수치.
		//		int		TimeStamp				// 해당 데이터를 얻은 시간 TIMESTAMP  (time() 함수)
		//										// 본래 time 함수는 time_t 타입변수이나 64bit 로 낭비스러우니
		//										// int 로 캐스팅하여 전송. 그래서 2038년 까지만 사용가능
		//	}
		//
		//------------------------------------------------------------
		//en_PACKET_CS_MONITOR_TOOL_DATA_UPDATE,

		WORD PacketType = en_PACKET_CS_MONITOR_TOOL_DATA_UPDATE;

		pPacket->Enqueue((char*)&PacketType, sizeof(WORD));
		pPacket->Enqueue((char*)&ServerNo, sizeof(BYTE));
		pPacket->Enqueue((char*)pData, 9);
	}

	bool GGM::NetServerMonitorConfig::LoadConfig(const TCHAR * ConfigFileName)
	{
		CParser parser;

		//-------------------------------------------------------------------------------------------------------------------------
		// 서버 설정 파일 로드
		bool Ok = parser.LoadFile(ConfigFileName);
		DWORD err = GetLastError();

		// 서버 구동시 모든 정보는 로그로 남긴다.
		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("MONITOR_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("SERVER CONFIG FILE OPEN FAILED"));
			return 0;
		}

		CLogger::GetInstance()->Log(_T("MONITOR_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("========== NET_SERVER_MONITOR OPEN START ========== "));

		parser.SetSpace(_T("#NET_SERVER_MONITOR"));
		//-------------------------------------------------------------------------------------------------------------------------

		//-------------------------------------------------------------------------------------------------------------------------
		// BIND_IP LOAD
		Ok = parser.GetValue(_T("BIND_IP"), BindIP);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("MONITOR_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET BIND_IP FAILED : [%s]"), BindIP);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("MONITOR_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("BIND_IP : [%s]"), BindIP);
		//-------------------------------------------------------------------------------------------------------------------------

		//-------------------------------------------------------------------------------------------------------------------------
		// PORT LOAD
		Ok = parser.GetValue(_T("SERVER_PORT"), (short*)&Port);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("MONITOR_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET SERVER_PORT FAILED : [%hd]"), Port);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("MONITOR_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("SERVER_PORT : [%hd]"), Port);
		//-------------------------------------------------------------------------------------------------------------------------

		//-------------------------------------------------------------------------------------------------------------------------
		// ConcurrentThreads LOAD
		Ok = parser.GetValue(_T("CONCURRENT_THREADS"), (int*)&ConcurrentThreads);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("MONITOR_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET CONCURRENT_THREADS FAILED : [%d]"), ConcurrentThreads);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("MONITOR_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("CONCURRENT_THREADS : [%d]"), ConcurrentThreads);
		//-------------------------------------------------------------------------------------------------------------------------

		//-------------------------------------------------------------------------------------------------------------------------
		// MaxThreads LOAD
		Ok = parser.GetValue(_T("MAX_THREADS"), (int*)&MaxThreads);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("MONITOR_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET MAX_THREADS FAILED : [%d]"), MaxThreads);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("MONITOR_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("MAX_THREADS : [%d]"), MaxThreads);
		//-------------------------------------------------------------------------------------------------------------------------

		//-------------------------------------------------------------------------------------------------------------------------
		// IsNoDelay LOAD
		Ok = parser.GetValue(_T("NO_DELAY"), (bool*)&IsNoDelay);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("MONITOR_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET NO_DELAY FAILED : [%d]"), IsNoDelay);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("MONITOR_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("NO_DELAY : [%d]"), IsNoDelay);
		//-------------------------------------------------------------------------------------------------------------------------

		//-------------------------------------------------------------------------------------------------------------------------
		// MaxSessions LOAD
		Ok = parser.GetValue(_T("MAX_SESSION"), (short*)&MaxSessions);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("MONITOR_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET MAX_SESSION FAILED : [%d]"), MaxSessions);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("MONITOR_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("MAX_SESSION : [%d]"), MaxSessions);
		//-------------------------------------------------------------------------------------------------------------------------

		//-------------------------------------------------------------------------------------------------------------------------
		// PacketCode LOAD
		Ok = parser.GetValue(_T("PACKET_CODE"), (char*)&PacketCode);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("MONITOR_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET PACKET_CODE FAILED : [%d]"), PacketCode);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("MONITOR_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("PACKET_CODE : [%d]"), PacketCode);
		//-------------------------------------------------------------------------------------------------------------------------

		//-------------------------------------------------------------------------------------------------------------------------
		// PacketKey LOAD
		Ok = parser.GetValue(_T("PACKET_KEY"), (char*)&PacketKey);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("MONITOR_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET PACKET_KEY FAILED : [%d]"), PacketKey);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("MONITOR_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("PACKET_KEY : [%d]"), PacketKey);
		//-------------------------------------------------------------------------------------------------------------------------

		//-------------------------------------------------------------------------------------------------------------------------
		// LogLevel LOAD
		Ok = parser.GetValue(_T("LOG_LEVEL"), (int*)&LogLevel);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("MONITOR_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET LOG_LEVEL FAILED : [%d]"), LogLevel);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("MONITOR_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("LOG_LEVEL : [%d]"), LogLevel);
		CLogger::GetInstance()->SetDefaultLogLevel((LEVEL)LogLevel);
		//-------------------------------------------------------------------------------------------------------------------------

		//-------------------------------------------------------------------------------------------------------------------------
		// LoginSessionKey LOAD
		Ok = parser.GetValue(_T("LOGIN_KEY"), LoginSessionkey);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("MONITOR_SERVER_INFO"), LEVEL::DBG, OUTMODE::FILE, _T("GET LOGIN_KEY FAILED : [%s]"), LoginSessionkey);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("MONITOR_SERVER_INFO"), LEVEL::DBG, OUTMODE::FILE, _T("LOGIN_KEY : [%s]"), LoginSessionkey);
		//-------------------------------------------------------------------------------------------------------------------------

		CLogger::GetInstance()->Log(_T("MONITOR_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("========== NET_SERVER_MONITOR OPEN SUCCESSFUL ========== "));

		return true;
	}

}
