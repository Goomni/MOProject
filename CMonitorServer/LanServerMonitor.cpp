#include "LanServerMonitor.h"
#include "NetServerMonitor.h"
#include "Logger\Logger.h"
#include "CrashDump\CrashDump.h"
#include "DBConnector\CDBConnectorTLS.h"
#include "Define\GGM_CONSTANTS.h"
#include "Define\GGM_ERROR.h"
#include "Protocol\CommonProtocol.h"
#include <strsafe.h>

namespace GGM
{
	GGM::CLanServerMonitor::CLanServerMonitor(LanServerMonitorConfig * pLanMonitorConfig, CNetServerMonitor * pNetMonitor)
		: m_pNetMonitor(pNetMonitor)
	{	
		// LanClient �迭 �����Ҵ�
		m_LanClientArr = new MonitoringWorker[pLanMonitorConfig->MaxSessions];

		if (m_LanClientArr == nullptr)
			OnError(GetLastError(), _T("[CLanServerMonitor] new BYTE[pLanMonitorConfig->MaxSessions] Failed %d"));		

		// DB ����� �����͵��� ���Ե� ����ü �迭 �����Ҵ� 
		m_DataArray = new MonitoringData[pLanMonitorConfig->NumOfDataType];		

		if (m_LanClientArr == nullptr)
			OnError(GetLastError(), _T("[CLanServerMonitor] new MonitoringData[pLanMonitorConfig->NumOfDataType] Failed %d"));

		for (int i = 1; i < pLanMonitorConfig->NumOfDataType; i++)
		{
			InitializeSRWLock(&m_DataArray[i].DataLock);

			// DB�� �����ϱ� ���� ���� �̸��� �����Ѵ�.
			switch (i)
			{
			case dfMONITOR_DATA_TYPE_SERVER_CPU_TOTAL:
			case dfMONITOR_DATA_TYPE_SERVER_AVAILABLE_MEMORY:
			case dfMONITOR_DATA_TYPE_SERVER_NETWORK_RECV:
			case dfMONITOR_DATA_TYPE_SERVER_NETWORK_SEND:
			case dfMONITOR_DATA_TYPE_SERVER_NONPAGED_MEMORY:
				m_DataArray[i].ServerNo = pLanMonitorConfig->SysCollectorNo;
				StringCchCopyA(m_DataArray[i].ServerName, 256, "SYSTEM");				
				break;

			case dfMONITOR_DATA_TYPE_MATCH_SERVER_ON:
			case dfMONITOR_DATA_TYPE_MATCH_CPU:
			case dfMONITOR_DATA_TYPE_MATCH_MEMORY_COMMIT:
			case dfMONITOR_DATA_TYPE_MATCH_PACKET_POOL:
			case dfMONITOR_DATA_TYPE_MATCH_SESSION:
			case dfMONITOR_DATA_TYPE_MATCH_PLAYER:
			case dfMONITOR_DATA_TYPE_MATCH_MATCHSUCCESS:			
				m_DataArray[i].ServerNo = pLanMonitorConfig->MatchServerNo;
				StringCchCopyA(m_DataArray[i].ServerName, 256, "MATCH");
				break;

			case dfMONITOR_DATA_TYPE_MASTER_SERVER_ON:
			case dfMONITOR_DATA_TYPE_MASTER_CPU:
			case dfMONITOR_DATA_TYPE_MASTER_CPU_SERVER:
			case dfMONITOR_DATA_TYPE_MASTER_MEMORY_COMMIT:
			case dfMONITOR_DATA_TYPE_MASTER_PACKET_POOL:
			case dfMONITOR_DATA_TYPE_MASTER_MATCH_CONNECT:
			case dfMONITOR_DATA_TYPE_MASTER_MATCH_LOGIN:
			case dfMONITOR_DATA_TYPE_MASTER_STAY_CLIENT:
			case dfMONITOR_DATA_TYPE_MASTER_BATTLE_CONNECT:
			case dfMONITOR_DATA_TYPE_MASTER_BATTLE_LOGIN:
			case dfMONITOR_DATA_TYPE_MASTER_BATTLE_STANDBY_ROOM:
				m_DataArray[i].ServerNo = pLanMonitorConfig->MasterServerNo;
				StringCchCopyA(m_DataArray[i].ServerName, 256, "MASTER");
				break;

			case dfMONITOR_DATA_TYPE_BATTLE_SERVER_ON:
			case dfMONITOR_DATA_TYPE_BATTLE_CPU:
			case dfMONITOR_DATA_TYPE_BATTLE_MEMORY_COMMIT:
			case dfMONITOR_DATA_TYPE_BATTLE_PACKET_POOL:
			case dfMONITOR_DATA_TYPE_BATTLE_AUTH_FPS:
			case dfMONITOR_DATA_TYPE_BATTLE_GAME_FPS:
			case dfMONITOR_DATA_TYPE_BATTLE_SESSION_ALL:
			case dfMONITOR_DATA_TYPE_BATTLE_SESSION_AUTH:
			case dfMONITOR_DATA_TYPE_BATTLE_SESSION_GAME:
			case dfMONITOR_DATA_TYPE_BATTLE_:
			case dfMONITOR_DATA_TYPE_BATTLE_ROOM_WAIT:
			case dfMONITOR_DATA_TYPE_BATTLE_ROOM_PLAY:			
				m_DataArray[i].ServerNo = pLanMonitorConfig->BattleServerNo;
				StringCchCopyA(m_DataArray[i].ServerName, 256, "BATTLE");
				break;

			case dfMONITOR_DATA_TYPE_CHAT_SERVER_ON:
			case dfMONITOR_DATA_TYPE_CHAT_CPU:
			case dfMONITOR_DATA_TYPE_CHAT_MEMORY_COMMIT:
			case dfMONITOR_DATA_TYPE_CHAT_PACKET_POOL:
			case dfMONITOR_DATA_TYPE_CHAT_SESSION:
			case dfMONITOR_DATA_TYPE_CHAT_PLAYER:
			case dfMONITOR_DATA_TYPE_CHAT_ROOM:			
				m_DataArray[i].ServerNo = pLanMonitorConfig->ChatServerNo;
				StringCchCopyA(m_DataArray[i].ServerName, 256, "CHAT");
				break;
			}
		}

		m_DataArrSize = pLanMonitorConfig->NumOfDataType;

		// TLS DBConnector ����
		char  UTF_8_DB_ip[17];
		char  UTF_8_DB_username[65];
		char  UTF_8_DB_password[65];
		char  UTF_8_DB_dbname[65];

		// �������Ͽ��� �о�� ��� ���������� UTF-16 �����̹Ƿ� UTF-8�� ��ȯ�Ѵ�.
		WideCharToMultiByte(0, 0, pLanMonitorConfig->DB_ip, 16, UTF_8_DB_ip, 17, nullptr, nullptr);
		WideCharToMultiByte(0, 0, pLanMonitorConfig->DB_username, 64, UTF_8_DB_username, 65, nullptr, nullptr);
		WideCharToMultiByte(0, 0, pLanMonitorConfig->DB_password, 64, UTF_8_DB_password, 65, nullptr, nullptr);
		WideCharToMultiByte(0, 0, pLanMonitorConfig->DB_dbname, 64, UTF_8_DB_dbname, 65, nullptr, nullptr);

		m_pDBConnector = new CDBConnector(UTF_8_DB_ip, UTF_8_DB_username, UTF_8_DB_password, UTF_8_DB_dbname, pLanMonitorConfig->DB_port);

		if (m_pDBConnector == nullptr)
		{
			OnError(GetLastError(), _T("[CLanServerMonitor] new CDBConnectorTLS Failed %d"));		
			throw 0;
		}

		// LanServer ����
		bool bOk = Start(
			pLanMonitorConfig->BindIP,
			pLanMonitorConfig->Port,
			pLanMonitorConfig->ConcurrentThreads,
			pLanMonitorConfig->MaxThreads,
			pLanMonitorConfig->IsNoDelay,
			pLanMonitorConfig->MaxSessions
		);

		if (bOk == false)
		{
			OnError(GGM_ERROR::STARTUP_FAILED, _T("[CLanServerMonitor] Start() Failed %d"));
			throw 0;
		}

		// DB Writer ������ ����
		//m_hDBWriter = (HANDLE)_beginthreadex(nullptr, 0, WriteDB, this, 0, nullptr);

		//if (m_hDBWriter == NULL)
		//{
		//	OnError(GetLastError(), _T("[CLanServerMonitor] DBWriter Thread Creation Failed"));
		//	throw 0;
		//}
	}

	GGM::CLanServerMonitor::~CLanServerMonitor()
	{
		// DB ������ ���� 
		QueueUserAPC(CLanServerMonitor::DBWriterExitFunc, m_hDBWriter, 0);

		// DB �����尡 ������ �� ���� ���
		WaitForSingleObject(m_hDBWriter, INFINITE);		

		// ���� ���� ���� 
		Stop();

		// TLS DBConnector �Ҵ� ����
		delete m_pDBConnector;

		// DB ����� �����͵��� ���Ե� ����ü �迭 �����Ҵ� ����
		delete m_DataArray;

		// LanClient �迭 �����Ҵ� ���� 
		delete m_LanClientArr;	
	}

	ULONGLONG CLanServerMonitor::GetLanClientCount() const
	{
		return m_LanClientCount;
	}

	void GGM::CLanServerMonitor::OnClientJoin(const SOCKADDR_IN & ClientAddr, ULONGLONG SessionID)
	{
		// �Ϲ� �������� ����͸� ������ ����͸� ������ �۽��� LanClient�� �����Ͽ���
		if (m_LanClientArr[(WORD)SessionID].SessionID != INIT_SESSION_ID)
		{
			OnError(GGM_ERROR::SESSION_SYNC_FAILED, _T("[CLanServerMonitor] OnClientJoin Session Sync Error %d"));
			return;
		}	

		// �迭�� ������ ���� ���� 
		m_LanClientArr[(WORD)SessionID].SessionID = SessionID;

		// �� Ŭ���̾�Ʈ ���Ӽ� ����
		InterlockedIncrement(&m_LanClientCount);
	}

	void GGM::CLanServerMonitor::OnClientLeave(ULONGLONG SessionID)
	{
		// LanClient�� ����͸� ������ ���� ����
		if (m_LanClientArr[(WORD)SessionID].SessionID != SessionID)
		{
			OnError(GGM_ERROR::SESSION_SYNC_FAILED, _T("[CLanServerMonitor] OnClientJoin Session Sync Error %d"));
			return;
		}

		// �迭���� ������ ���� ����
		m_LanClientArr[(WORD)SessionID].SessionID = INIT_SESSION_ID;

		// �� Ŭ���̾�Ʈ ���Ӽ� ����
		InterlockedDecrement(&m_LanClientCount);
	}

	bool GGM::CLanServerMonitor::OnConnectionRequest(const SOCKADDR_IN & ClientAddr)
	{
		return true;
	}

	void GGM::CLanServerMonitor::OnRecv(ULONGLONG SessionID, CPacket * pPacket)
	{
		// ��Ŷ ���� ��, ��Ŷ Ÿ�� Ȯ��
		WORD PacketType;
		pPacket->Dequeue((char*)&PacketType, sizeof(WORD));

		// ��Ŷ Ÿ�Ժ� ���ν��� ȣ�� 
		if (PacketType == en_PACKET_SS_MONITOR_DATA_UPDATE)
		{
			// ����͸� ������ ó�� �Լ�
			Monitor_Data_Update(SessionID, pPacket);
		}
		else
		{
			// �α��� ó�� �Լ�
			Monitor_Login(SessionID, pPacket);			
		}
	}

	void GGM::CLanServerMonitor::OnSend(ULONGLONG SessionID, int SendSize)
	{
		// ���Ͼ���
	}

	void GGM::CLanServerMonitor::OnWorkerThreadBegin()
	{
		// ���Ͼ���
	}

	void GGM::CLanServerMonitor::OnWorkerThreadEnd()
	{
		// ���Ͼ���
	}

	void GGM::CLanServerMonitor::OnError(int ErrorNo, const TCHAR * ErrorMsg)
	{
		CLogger::GetInstance()->Log(_T("MonitorServerLogs"), LEVEL::DBG, OUTMODE::BOTH, ErrorMsg, ErrorNo);		
	}

	void GGM::CLanServerMonitor::Monitor_Login(ULONGLONG SessionID, CPacket * pPacket)
	{
		//------------------------------------------------------------
		// LoginServer, GameServer , ChatServer , Agent �� ����͸� ������ �α��� ��
		//
		// 
		//	{
		//		WORD	Type
		//
		//		int		ServerNo		// ���� Ÿ�� ���� �� �������� ���� ��ȣ�� �ο��Ͽ� ���
		//	}
		//
		//------------------------------------------------------------
		//en_PACKET_SS_MONITOR_LOGIN,

		if(m_LanClientArr[(WORD)SessionID].SessionID != SessionID)
		{
			OnError(GGM_ERROR::SESSION_SYNC_FAILED, _T("[CLanServerMonitor] OnClientJoin Session Sync Error %d"));
			return;
		}

		// ������ȣ ������		
		pPacket->Dequeue((char*)&m_LanClientArr[(WORD)SessionID].ServerNo, sizeof(int));

		// �� ���������̹Ƿ� �α��� ������ ���п� ���� ������Ŷ ������ ���� 
	}

	void CLanServerMonitor::Monitor_Data_Update(ULONGLONG SessionID, CPacket * pPacket)
	{
		//------------------------------------------------------------
		// ������ ����͸������� ������ ����
		// �� ������ �ڽ��� ����͸����� ��ġ�� 1�ʸ��� ����͸� ������ ����.
		//
		// ������ �ٿ� �� ��Ÿ ������ ����͸� �����Ͱ� ���޵��� ���ҋ��� ����Ͽ� TimeStamp �� �����Ѵ�.
		// �̴� ����͸� Ŭ���̾�Ʈ���� ���,�� ����Ѵ�.
		// 
		//	{
		//		WORD	Type
		//
		//		BYTE	DataType				// ����͸� ������ Type �ϴ� Define ��.
		//		int		DataValue				// �ش� ������ ��ġ.
		//		int		TimeStamp				// �ش� �����͸� ���� �ð� TIMESTAMP  (time() �Լ�)
		//										// ���� time �Լ��� time_t Ÿ�Ժ����̳� 64bit �� ���񽺷����
		//										// int �� ĳ�����Ͽ� ����. �׷��� 2038�� ������ ��밡��
		//	}
		//
		//------------------------------------------------------------
		//	en_PACKET_SS_MONITOR_DATA_UPDATE,

		if (m_LanClientArr[(WORD)SessionID].SessionID != SessionID)
		{
			OnError(GGM_ERROR::SESSION_SYNC_FAILED, _T("[CLanServerMonitor] OnClientJoin Session Sync Error %d"));
			return;
		}

		// NetServer���� �����Ͱ� ���������� �˷��� ����͸� ���鿡�� �ش� �����͸� �۽�����		
		char * pData = (char*)pPacket->GetReadPtr();

		// �� �Լ��� Net ����͸� ������ �Լ��μ�, ���޵� ������ ����͸� ���鿡�� �߰����ش�.
		m_pNetMonitor->Monitor_Tool_Data_Update(m_LanClientArr[(WORD)SessionID].ServerNo, pData);

		// DB �����尡 DB�� ������ �� �ֵ��� 1�е����� �����͸� �����Ѵ�.		
		int  DataValue = *((int*)(pData + 1));

		// �����Ͱ� 0�̶�� �ǹ̾��� �����ͷ� ����Ͽ� �������� ����
		if (DataValue == 0)
			return;

		// DB ������� LanServer�� ��Ŀ�����尡 ���ÿ� �����ϴ� �������̹Ƿ� �� �Ǵ�.
		BYTE DataType = *((BYTE*)pData);
		MonitoringData *pMonitoringData = &m_DataArray[DataType];

		AcquireSRWLockExclusive(&pMonitoringData->DataLock);

		// ���� �ֽ��� �����͸� ���� 
		pMonitoringData->DataValue = DataValue;

		// ��ȿ�� �������� ������ ����, DB ���� ������� �� ī��Ʈ�� Ȯ���ؼ� DB ���� ���θ� �Ǵ�
		pMonitoringData->DataCount++;

		// 1�е����� ������ ���ġ�� ���ϱ� ���� �հ踦 ����
		pMonitoringData->DataSum += DataValue;

		// �ִ�, �ּڰ� ����
		if (DataValue > pMonitoringData->MaxValue)
			pMonitoringData->MaxValue = DataValue;
		else if (DataValue < pMonitoringData->MinValue)
			pMonitoringData->MinValue = DataValue;

		ReleaseSRWLockExclusive(&pMonitoringData->DataLock);
	}

	unsigned int __stdcall CLanServerMonitor::WriteDB(LPVOID ParmaThis)
	{
		CLanServerMonitor *pThis = (CLanServerMonitor*)ParmaThis;
		
		// DB�� ����� �����Ͱ� ���Ե� �迭�� �� ������ ����
		MonitoringData *pDataArr = pThis->m_DataArray;	
		ULONGLONG       DataArrSize = pThis->m_DataArrSize;

		// DB Connector
		CDBConnector   *pDBConnector = pThis->m_pDBConnector;	

		// 1�и��� ����� ����͸� ������ DB�� ������ 
		while (true)
		{
			DWORD ret = SleepEx(DB_WRITE_PERIOD, true);

			// ������� 
			if (ret == WAIT_IO_COMPLETION)
				break;

			// �迭 ��ȸ�ϸ� ������ ȹ���Ͽ� DB�� ���� 
			for (ULONGLONG DataType = 1; DataType < DataArrSize; DataType++)
			{
				// �����Ͱ� �߰��� ���ϸ� �ȵǱ� ������ ������ �� �Ǵ�.
				AcquireSRWLockExclusive(&pDataArr[DataType].DataLock);

				// 1�е��� �� ������ ���� ��, 0 �̸� DB�� ������ �������� ����
				size_t DataCount = pDataArr[DataType].DataCount;

				if (DataCount == 0)
				{
					ReleaseSRWLockExclusive(&pDataArr[DataType].DataLock);
					continue;
				}

				// ���⼭ DB�� �����ϴ� �ٽ� ������ ī��Ʈ�� 0�̴�.
				pDataArr[DataType].DataCount = 0;

				// ���� �ֱٿ� �� �����͸� ����
				int DataValue = pDataArr[DataType].DataValue;

				// 1�е��� �� �������� �Ѱ� DB ����� ��� ���� �� ���
				ULONGLONG DataSum = pDataArr[DataType].DataSum;
				pDataArr[DataType].DataSum = 0;

				// 1�е��� �� ������ �� �ִ�
				int MaxValue = pDataArr[DataType].MaxValue;
				pDataArr[DataType].MaxValue = 0;

				// 1�е��� �� ������ �� �ּڰ�
				int MinValue = pDataArr[DataType].MinValue;
				pDataArr[DataType].MinValue = 0x7fffffff;

				ReleaseSRWLockExclusive(&pDataArr[DataType].DataLock);

				// ������ ���� ���� �� ������� ��Ǯ�� DB�� ��������
				// DB Insert�� �� �����尡 ������¿� �����Ƿ� ���� Ǯ�� �����Ѵ�.
				
				pDBConnector->SendQuery(false,
					"INSERT INTO `monitor_log`.`monitorlog_201901`"
					"(`logtime`, `serverno`, `servername`, `type`, `value`, `min`, `max`, `avr`)"
					"VALUES (NOW(), '%d', '%s', '%d', '%d', '%d', '%d', '%f')", 
					pDataArr[DataType].ServerNo, // �ش� �����͸� ���� ������ ��ȣ
					pDataArr[DataType].ServerName, // �ش� �����͸� ���� ������ �̸� 
					DataType, // �ش� �������� Ÿ��
					DataValue, // �������� �� (0 ����)
					MinValue, // 1�е��� ������ �������� �ּҰ� (0 ����)
					MaxValue, // 1�е��� ������ �������� �ִ밪 (0 ����)
					((float)DataSum / (float) DataCount) // 1�е��� ������ ���ǹ��� �������� ���
				);
			}
		}

		return 0;
	}

	void __stdcall CLanServerMonitor::DBWriterExitFunc(ULONG_PTR Param)
	{
		
	}

	bool LanServerMonitorConfig::LoadConfig(const TCHAR * ConfigFileName)
	{
		CParser parser;

		// ---------------------------------------------------------------------------------------------------
		// ���� ���� ���� �ε�
		bool Ok = parser.LoadFile(ConfigFileName);
		DWORD err = GetLastError();
		// ---------------------------------------------------------------------------------------------------

		// ---------------------------------------------------------------------------------------------------
		// ���� ������ ��� ������ �α׷� �����.
		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("MONITOR_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("MONITOR_SERVER_INFO CONFIG FILE OPEN FAILED"));
			return 0;
		}

		CLogger::GetInstance()->Log(_T("MONITOR_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("========== LAN_SERVER_MONITOR OPEN START ========== "));
		// ---------------------------------------------------------------------------------------------------

		// ---------------------------------------------------------------------------------------------------
		// ���� ����
		parser.SetSpace(_T("#LAN_SERVER_MONITOR"));

		// BIND_IP LOAD
		Ok = parser.GetValue(_T("BIND_IP"), BindIP);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("MONITOR_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET BIND_IP FAILED : [%s]"), BindIP);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("MONITOR_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("BIND_IP : [%s]"), BindIP);
		// ---------------------------------------------------------------------------------------------------

		// ---------------------------------------------------------------------------------------------------
		// PORT LOAD
		Ok = parser.GetValue(_T("SERVER_PORT"), (short*)&Port);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("MONITOR_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET SERVER_PORT FAILED : [%hd]"), Port);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("MONITOR_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("SERVER_PORT : [%hd]"), Port);
		// ---------------------------------------------------------------------------------------------------

		// ---------------------------------------------------------------------------------------------------
		// ConcurrentThreads LOAD
		Ok = parser.GetValue(_T("CONCURRENT_THREADS"), (int*)&ConcurrentThreads);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("MONITOR_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET CONCURRENT_THREADS FAILED : [%d]"), ConcurrentThreads);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("MONITOR_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("CONCURRENT_THREADS : [%d]"), ConcurrentThreads);
		// ---------------------------------------------------------------------------------------------------

		// ---------------------------------------------------------------------------------------------------
		// MaxThreads LOAD
		Ok = parser.GetValue(_T("MAX_THREADS"), (int*)&MaxThreads);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("MONITOR_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET MAX_THREADS FAILED : [%d]"), MaxThreads);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("MONITOR_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("MAX_THREADS : [%d]"), MaxThreads);
		// ---------------------------------------------------------------------------------------------------

		// ---------------------------------------------------------------------------------------------------
		// IsNoDelay LOAD
		Ok = parser.GetValue(_T("NO_DELAY"), (bool*)&IsNoDelay);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("MONITOR_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET NO_DELAY FAILED : [%d]"), IsNoDelay);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("MONITOR_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("NO_DELAY : [%d]"), IsNoDelay);
		// ---------------------------------------------------------------------------------------------------

		// ---------------------------------------------------------------------------------------------------
		// MaxSessions LOAD
		Ok = parser.GetValue(_T("MAX_SESSION"), (short*)&MaxSessions);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("MONITOR_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET MAX_SESSION FAILED : [%d]"), MaxSessions);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("MONITOR_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("MAX_SESSION : [%d]"), MaxSessions);
		// ---------------------------------------------------------------------------------------------------

		// ---------------------------------------------------------------------------------------------------
		// LogLevel LOAD
		Ok = parser.GetValue(_T("LOG_LEVEL"), (int*)&LogLevel);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("MONITOR_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET LOG_LEVEL FAILED : [%d]"), LogLevel);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("MONITOR_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("LOG_LEVEL : [%d]"), LogLevel);
		CLogger::GetInstance()->SetDefaultLogLevel((LEVEL)LogLevel);
		// ---------------------------------------------------------------------------------------------------

		// ---------------------------------------------------------------------------------------------------
		// NumOfDataType LOAD
		Ok = parser.GetValue(_T("NUM_OF_DATATYPE"), (int*)&NumOfDataType);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("MONITOR_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET NUM_OF_DATATYPE FAILED : [%d]"), NumOfDataType);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("MONITOR_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("NUM_OF_DATATYPE  : [%d]"), NumOfDataType);
		// ---------------------------------------------------------------------------------------------------

		// ---------------------------------------------------------------------------------------------------
		// SysCollectorNo LOAD
		Ok = parser.GetValue(_T("SYSTEM"), (int*)&SysCollectorNo);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("MONITOR_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET SYSTEM  FAILED : [%d]"), SysCollectorNo);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("MONITOR_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("SYSTEM  : [%d]"), SysCollectorNo);
		// ---------------------------------------------------------------------------------------------------

		// ---------------------------------------------------------------------------------------------------
		// MatchServerNo LOAD
		Ok = parser.GetValue(_T("MATCH"), (int*)&MatchServerNo);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("MONITOR_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET MATCH FAILED : [%d]"), MatchServerNo);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("MONITOR_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("MATCH : [%d]"), MatchServerNo);
		// ---------------------------------------------------------------------------------------------------

		// ---------------------------------------------------------------------------------------------------
		// MasterServerNo LOAD
		Ok = parser.GetValue(_T("MASTER"), (int*)&MasterServerNo);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("MONITOR_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET MASTER FAILED : [%d]"), MasterServerNo);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("MONITOR_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("MASTER : [%d]"), MasterServerNo);
		// ---------------------------------------------------------------------------------------------------

		// ---------------------------------------------------------------------------------------------------
		// BattleServerNo LOAD
		Ok = parser.GetValue(_T("BATTLE"), (int*)&BattleServerNo);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("MONITOR_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET BATTLE FAILED : [%d]"), BattleServerNo);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("MONITOR_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("BATTLE : [%d]"), BattleServerNo);
		// ---------------------------------------------------------------------------------------------------

		// ---------------------------------------------------------------------------------------------------
		// ChatServerNo LOAD
		Ok = parser.GetValue(_T("CHAT"), (int*)&ChatServerNo);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("MONITOR_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET CHAT FAILED : [%d]"), BattleServerNo);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("MONITOR_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("CHAT : [%d]"), ChatServerNo);
		// ---------------------------------------------------------------------------------------------------

		// ---------------------------------------------------------------------------------------------------
		// DB_ip Load
		Ok = parser.GetValue(_T("DB_IP"), DB_ip);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET DB_IP FAILED : [%s]"), DB_ip);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("DB_IP : [%s]"), DB_ip);
		// ---------------------------------------------------------------------------------------------------

		// ---------------------------------------------------------------------------------------------------
		// DB_username Load
		Ok = parser.GetValue(_T("DB_USERNAME"), DB_username);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET DB_USERNAME FAILED : [%s]"), DB_username);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("DB_USERNAME : [%s]"), DB_username);
		// ---------------------------------------------------------------------------------------------------

		// ---------------------------------------------------------------------------------------------------
		// DB_password Load
		Ok = parser.GetValue(_T("DB_PASSWORD"), DB_password);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET DB_PASSWORD FAILED : [%s]"), DB_password);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("DB_PASSWORD : [%s]"), DB_password);
		// ---------------------------------------------------------------------------------------------------

		// ---------------------------------------------------------------------------------------------------
		// DB_dbname Load
		Ok = parser.GetValue(_T("DB_DBNAME"), DB_dbname);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET DB_DBNAME FAILED : [%s]"), DB_dbname);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("DB_DBNAME : [%s]"), DB_dbname);
		// ---------------------------------------------------------------------------------------------------

		// ---------------------------------------------------------------------------------------------------
		// DB_port Load
		Ok = parser.GetValue(_T("DB_PORT"), (short*)&DB_port);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET DB_PORT FAILED : [%hd]"), DB_port);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("LOGIN_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("DB_PORT : [%hd]"), DB_port);
		// ---------------------------------------------------------------------------------------------------

		CLogger::GetInstance()->Log(_T("MONITOR_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("========== LAN_SERVER_MONITOR OPEN SUCCESSFUL ========== "));

		return true;
	}

}