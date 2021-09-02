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
		// �迭 �����Ҵ�
		m_ViewerArr = new Viewer[pNetConfig->MaxSessions];
		m_MaxViewer = pNetConfig->MaxSessions;

		// �迭 �� �ʱ�ȭ
		InitializeSRWLock(&m_Lock);

		// ���� ����Ű ����
		// ���� ����Ű�� �������Ͽ��� �������� ������ UTF-16�̴�.
		// UTF-8�� ��ȯ�Ѵ�.		
		int ret = WideCharToMultiByte(0, 0, pNetConfig->LoginSessionkey, 32, m_SessionKey, 33, NULL, NULL);

		if (ret == 0)
			OnError(GetLastError(), _T("[CNetServerMonitor] Ctor, WideCharToMultiByte Failed %d"));

		// ���� ���� 
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

		// LanClientMonitor ����
		m_pLanMonitor = new CLanServerMonitor(pLanConfig, this);

		if (m_pLanMonitor == nullptr)
			OnError(GetLastError(), _T("[CNetServerMonitor] m_pLanMonitor = new CLanServerMonitor(pLanConfig, this) fail %d"));		
	}

	CNetServerMonitor::~CNetServerMonitor()
	{
		// LanClientMonitor Delete
		delete m_pLanMonitor;

		// ��������
		Stop();

		// ��� �����迭 �Ҵ�����
		delete m_ViewerArr;
	}	

	void GGM::CNetServerMonitor::OnClientJoin(const SOCKADDR_IN & ClientAddr, ULONGLONG SessionID)
	{
		AcquireSRWLockExclusive(&m_Lock);

		// ���� ����ȭ�� ������ ������ �˻�
		if (m_ViewerArr[(WORD)SessionID].SessionID != INIT_SESSION_ID)
		{
			OnError(GGM_ERROR::SESSION_SYNC_FAILED, _T("[CNetServerMonitor] OnClientJoin Failed %d"));
			ReleaseSRWLockExclusive(&m_Lock);
			return;
		}

		// ������ ����͸� �� �迭�� �߰�
		m_ViewerArr[(WORD)SessionID].SessionID = SessionID;	

		// ī���� ����
		++m_ViewerCount;

		ReleaseSRWLockExclusive(&m_Lock);	
	}

	void GGM::CNetServerMonitor::OnClientLeave(ULONGLONG SessionID)
	{
		AcquireSRWLockExclusive(&m_Lock);

		// ���� ����ȭ�� ������ ������ �˻�
		if (m_ViewerArr[(WORD)SessionID].SessionID != SessionID)
		{
			OnError(GGM_ERROR::SESSION_SYNC_FAILED, _T("[CNetServerMonitor] OnClientLeave Failed %d"));
			ReleaseSRWLockExclusive(&m_Lock);
			return;
		}

		// ���Ӳ��� ����͸� �� �迭���� ����
		m_ViewerArr[(WORD)SessionID].SessionID = INIT_SESSION_ID;
		m_ViewerArr[(WORD)SessionID].IsLogin = false;

		// ī���� ����
		--m_ViewerCount;

		ReleaseSRWLockExclusive(&m_Lock);	
	}

	bool GGM::CNetServerMonitor::OnConnectionRequest(const SOCKADDR_IN & ClientAddr)
	{
		return true;
	}

	void GGM::CNetServerMonitor::OnRecv(ULONGLONG SessionID, CNetPacket * pPacket)
	{
		// CNetServerMonitor�� �����ϴ� ��Ŷ�� ����͸� ����� �α��� ��û ��Ŷ�� �����ϴ�.
		Monitor_Tool_Req_Login(SessionID, pPacket);
	}

	void GGM::CNetServerMonitor::OnSend(ULONGLONG SessionID, int SendSize)
	{
		// ���� ����
	}

	void GGM::CNetServerMonitor::OnWorkerThreadBegin()
	{
		// ���� ����
	}

	void GGM::CNetServerMonitor::OnWorkerThreadEnd()
	{
		// ���� ����
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
		// ����͸� Ŭ���̾�Ʈ(��) �� ����͸� ������ �α��� ��û
		//
		//	{
		//		WORD	Type
		//
		//		char	LoginSessionKey[32]		// �α��� ���� Ű. (�̴� ����͸� ������ ���������� ����)
		//										// �� ����͸� ���� ���� Ű�� ������ ���;� ��
		//	}
		//
		//------------------------------------------------------------
		//	en_PACKET_CS_MONITOR_TOOL_REQ_LOGIN,		

		// �� ��Ȳ�̶�� ����ȭ ����
		if (SessionID != m_ViewerArr[(WORD)SessionID].SessionID)
		{
			OnError(GGM_ERROR::SESSION_SYNC_FAILED, _T("[CNetServerMonitor] Monitor_Tool_Req_Login Invalid SessionKey %d"));
			Disconnect(SessionID);
			return;
		}

		// ��Ŷ �����Ͱ� �����ϸ� ���� ����
		if (pPacket->GetCurrentUsage() < 34)
		{
			Disconnect(SessionID);
			return;
		}
		
		// ��Ŷ Ÿ�� ������
		WORD PacketType;
		pPacket->Dequeue((char*)&PacketType, sizeof(WORD));

		// ��Ŷ Ÿ�� �̻��ϸ� ���� ����
		if (PacketType != en_PACKET_CS_MONITOR_TOOL_REQ_LOGIN)
		{
			Disconnect(SessionID);
			return;
		}

		// �α��� ����Ű �� 
		char *pLoginSessionKey = (char*)pPacket->GetReadPtr();
		char *pServerSessionKey = m_SessionKey;		

		if (   *((INT64*)(pServerSessionKey)) != *((INT64*)(pLoginSessionKey))
			|| *((INT64*)(pServerSessionKey + 8)) != *((INT64*)(pLoginSessionKey + 8))
			|| *((INT64*)(pServerSessionKey + 16)) != *((INT64*)(pLoginSessionKey + 16))
			|| *((INT64*)(pServerSessionKey + 24)) != *((INT64*)(pLoginSessionKey + 24))
			)
		{
			// ����Ű �ٸ��� �α��� ����
			// ��Ŷ ������ ���� (SendAndDisconnect ���)
			Monitor_Tool_Res_Login(SessionID, FALSE);
			return;
		}

		// �׳� SendPacket()
		Monitor_Tool_Res_Login(SessionID, TRUE);
		
		// �α��� ���� ����	
		// ���⼭�� ���� ���� �ʾƵ� �ٸ� ��Ŀ�����尡 �ǵ帱 ���� ���� ������ ���� 
		m_ViewerArr[(WORD)SessionID].IsLogin = true;		
	}

	void GGM::CNetServerMonitor::Monitor_Tool_Res_Login(ULONGLONG SessionID, BYTE status)
	{
		//------------------------------------------------------------
		// ����͸� Ŭ���̾�Ʈ(��) ����͸� ������ �α��� ����
		// �α��ο� �����ϸ� 0 ������ �������
		//
		//	{
		//		WORD	Type
		//
		//		BYTE	Status					// �α��� ��� 0 / 1 
		//	}
		//
		//------------------------------------------------------------
		//en_PACKET_CS_MONITOR_TOOL_RES_LOGIN,

		CNetPacket *pPacket = CNetPacket::Alloc();

		CreatePacket_Monitor_Tool_Res_Login(status, pPacket);

		// �α��� �����̸� ������Ŷ ����۽�, ���ж�� ���� �۽��� ���� ����
		if (status == TRUE)
			SendPacket(SessionID, pPacket);
		else
			SendPacketAndDisconnect(SessionID, pPacket);

		CNetPacket::Free(pPacket);
	}

	void GGM::CNetServerMonitor::CreatePacket_Monitor_Tool_Res_Login(BYTE status, CNetPacket * pPacket)
	{
		//------------------------------------------------------------
		// ����͸� Ŭ���̾�Ʈ(��) ����͸� ������ �α��� ����
		// �α��ο� �����ϸ� 0 ������ �������
		//
		//	{
		//		WORD	Type
		//
		//		BYTE	Status					// �α��� ��� 0 / 1 
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
		// ����͸� ������ ����͸� Ŭ���̾�Ʈ(��) ���� ����͸� ������ ����
		//
		// ����͸� ������ ��� ����͸� Ŭ���̾�Ʈ���� ��� �����͸� �ѷ��ش�.
		//
		// �����͸� �����ϱ� ���ؼ��� �ʴ����� ��� �����͸� ��� 30~40���� ����͸� �����͸� �ϳ��� ��Ŷ���� ����°�
		// ������  �������� ������ ������ �����Ƿ� �׳� ������ ����͸� �����͸� ���������� ����ó�� �Ѵ�.
		//
		//	{
		//		WORD	Type
		//		
		//		BYTE	ServerNo				// ���� No
		//		BYTE	DataType				// ����͸� ������ Type �ϴ� Define ��.
		//		int		DataValue				// �ش� ������ ��ġ.
		//		int		TimeStamp				// �ش� �����͸� ���� �ð� TIMESTAMP  (time() �Լ�)
		//										// ���� time �Լ��� time_t Ÿ�Ժ����̳� 64bit �� ���񽺷����
		//										// int �� ĳ�����Ͽ� ����. �׷��� 2038�� ������ ��밡��
		//	}
		//
		//------------------------------------------------------------
		//en_PACKET_CS_MONITOR_TOOL_DATA_UPDATE,

		CNetPacket *pPacket = CNetPacket::Alloc();

		CreatePacket_Monitor_Tool_Data_Update(ServerNo, pData, pPacket);

		// ���� ������� ��� ������ �ϹǷ� ���� �ʿ��ϴ�. 
		AcquireSRWLockExclusive(&m_Lock);
		size_t MaxViewerCount = m_MaxViewer;
		Viewer *pViewerArr = m_ViewerArr;
		for (size_t i = 0; i < MaxViewerCount; i++)
		{
			// ���� �� �α��� ������ ���Ը� ��Ŷ ������.
			if(pViewerArr[i].SessionID != INIT_SESSION_ID && pViewerArr[i].IsLogin == true)
				SendPacket(pViewerArr[i].SessionID, pPacket);
		}
		ReleaseSRWLockExclusive(&m_Lock);

		// Alloc �� ���� Free
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
		// ����͸� ������ ����͸� Ŭ���̾�Ʈ(��) ���� ����͸� ������ ����
		//
		// ����͸� ������ ��� ����͸� Ŭ���̾�Ʈ���� ��� �����͸� �ѷ��ش�.
		//
		// �����͸� �����ϱ� ���ؼ��� �ʴ����� ��� �����͸� ��� 30~40���� ����͸� �����͸� �ϳ��� ��Ŷ���� ����°�
		// ������  �������� ������ ������ �����Ƿ� �׳� ������ ����͸� �����͸� ���������� ����ó�� �Ѵ�.
		//
		//	{
		//		WORD	Type
		//		
		//		BYTE	ServerNo				// ���� No
		//		BYTE	DataType				// ����͸� ������ Type �ϴ� Define ��.
		//		int		DataValue				// �ش� ������ ��ġ.
		//		int		TimeStamp				// �ش� �����͸� ���� �ð� TIMESTAMP  (time() �Լ�)
		//										// ���� time �Լ��� time_t Ÿ�Ժ����̳� 64bit �� ���񽺷����
		//										// int �� ĳ�����Ͽ� ����. �׷��� 2038�� ������ ��밡��
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
		// ���� ���� ���� �ε�
		bool Ok = parser.LoadFile(ConfigFileName);
		DWORD err = GetLastError();

		// ���� ������ ��� ������ �α׷� �����.
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
