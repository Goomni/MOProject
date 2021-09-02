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
		// LanClient 배열 동적할당
		m_LanClientArr = new MonitoringWorker[pLanMonitorConfig->MaxSessions];

		if (m_LanClientArr == nullptr)
			OnError(GetLastError(), _T("[CLanServerMonitor] new BYTE[pLanMonitorConfig->MaxSessions] Failed %d"));		

		// DB 저장용 데이터들이 포함된 구조체 배열 동적할당 
		m_DataArray = new MonitoringData[pLanMonitorConfig->NumOfDataType];		

		if (m_LanClientArr == nullptr)
			OnError(GetLastError(), _T("[CLanServerMonitor] new MonitoringData[pLanMonitorConfig->NumOfDataType] Failed %d"));

		for (int i = 1; i < pLanMonitorConfig->NumOfDataType; i++)
		{
			InitializeSRWLock(&m_DataArray[i].DataLock);

			// DB에 저장하기 위해 서버 이름을 설정한다.
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

		// TLS DBConnector 생성
		char  UTF_8_DB_ip[17];
		char  UTF_8_DB_username[65];
		char  UTF_8_DB_password[65];
		char  UTF_8_DB_dbname[65];

		// 설정파일에서 읽어온 디비 연결정보는 UTF-16 정보이므로 UTF-8로 변환한다.
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

		// LanServer 구동
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

		// DB Writer 스레드 생성
		//m_hDBWriter = (HANDLE)_beginthreadex(nullptr, 0, WriteDB, this, 0, nullptr);

		//if (m_hDBWriter == NULL)
		//{
		//	OnError(GetLastError(), _T("[CLanServerMonitor] DBWriter Thread Creation Failed"));
		//	throw 0;
		//}
	}

	GGM::CLanServerMonitor::~CLanServerMonitor()
	{
		// DB 스레드 종료 
		QueueUserAPC(CLanServerMonitor::DBWriterExitFunc, m_hDBWriter, 0);

		// DB 스레드가 종료할 때 까지 대기
		WaitForSingleObject(m_hDBWriter, INFINITE);		

		// 서버 구동 정지 
		Stop();

		// TLS DBConnector 할당 해제
		delete m_pDBConnector;

		// DB 저장용 데이터들이 포함된 구조체 배열 동적할당 해제
		delete m_DataArray;

		// LanClient 배열 동적할당 해제 
		delete m_LanClientArr;	
	}

	ULONGLONG CLanServerMonitor::GetLanClientCount() const
	{
		return m_LanClientCount;
	}

	void GGM::CLanServerMonitor::OnClientJoin(const SOCKADDR_IN & ClientAddr, ULONGLONG SessionID)
	{
		// 일반 서버에서 모니터링 정보를 모니터링 정보로 송신할 LanClient가 접속하였음
		if (m_LanClientArr[(WORD)SessionID].SessionID != INIT_SESSION_ID)
		{
			OnError(GGM_ERROR::SESSION_SYNC_FAILED, _T("[CLanServerMonitor] OnClientJoin Session Sync Error %d"));
			return;
		}	

		// 배열에 접속자 정보 저장 
		m_LanClientArr[(WORD)SessionID].SessionID = SessionID;

		// 랜 클라이언트 접속수 증가
		InterlockedIncrement(&m_LanClientCount);
	}

	void GGM::CLanServerMonitor::OnClientLeave(ULONGLONG SessionID)
	{
		// LanClient가 모니터링 서버와 연결 끊음
		if (m_LanClientArr[(WORD)SessionID].SessionID != SessionID)
		{
			OnError(GGM_ERROR::SESSION_SYNC_FAILED, _T("[CLanServerMonitor] OnClientJoin Session Sync Error %d"));
			return;
		}

		// 배열에서 접속자 정보 삭제
		m_LanClientArr[(WORD)SessionID].SessionID = INIT_SESSION_ID;

		// 랜 클라이언트 접속수 감소
		InterlockedDecrement(&m_LanClientCount);
	}

	bool GGM::CLanServerMonitor::OnConnectionRequest(const SOCKADDR_IN & ClientAddr)
	{
		return true;
	}

	void GGM::CLanServerMonitor::OnRecv(ULONGLONG SessionID, CPacket * pPacket)
	{
		// 패킷 수신 시, 패킷 타입 확인
		WORD PacketType;
		pPacket->Dequeue((char*)&PacketType, sizeof(WORD));

		// 패킷 타입별 프로시저 호출 
		if (PacketType == en_PACKET_SS_MONITOR_DATA_UPDATE)
		{
			// 모니터링 데이터 처리 함수
			Monitor_Data_Update(SessionID, pPacket);
		}
		else
		{
			// 로그인 처리 함수
			Monitor_Login(SessionID, pPacket);			
		}
	}

	void GGM::CLanServerMonitor::OnSend(ULONGLONG SessionID, int SendSize)
	{
		// 할일없음
	}

	void GGM::CLanServerMonitor::OnWorkerThreadBegin()
	{
		// 할일없음
	}

	void GGM::CLanServerMonitor::OnWorkerThreadEnd()
	{
		// 할일없음
	}

	void GGM::CLanServerMonitor::OnError(int ErrorNo, const TCHAR * ErrorMsg)
	{
		CLogger::GetInstance()->Log(_T("MonitorServerLogs"), LEVEL::DBG, OUTMODE::BOTH, ErrorMsg, ErrorNo);		
	}

	void GGM::CLanServerMonitor::Monitor_Login(ULONGLONG SessionID, CPacket * pPacket)
	{
		//------------------------------------------------------------
		// LoginServer, GameServer , ChatServer , Agent 가 모니터링 서버에 로그인 함
		//
		// 
		//	{
		//		WORD	Type
		//
		//		int		ServerNo		// 서버 타입 없이 각 서버마다 고유 번호를 부여하여 사용
		//	}
		//
		//------------------------------------------------------------
		//en_PACKET_SS_MONITOR_LOGIN,

		if(m_LanClientArr[(WORD)SessionID].SessionID != SessionID)
		{
			OnError(GGM_ERROR::SESSION_SYNC_FAILED, _T("[CLanServerMonitor] OnClientJoin Session Sync Error %d"));
			return;
		}

		// 서버번호 마샬링		
		pPacket->Dequeue((char*)&m_LanClientArr[(WORD)SessionID].ServerNo, sizeof(int));

		// 랜 프로토콜이므로 로그인 성공과 실패에 대한 응답패킷 보내지 않음 
	}

	void CLanServerMonitor::Monitor_Data_Update(ULONGLONG SessionID, CPacket * pPacket)
	{
		//------------------------------------------------------------
		// 서버가 모니터링서버로 데이터 전송
		// 각 서버는 자신이 모니터링중인 수치를 1초마다 모니터링 서버로 전송.
		//
		// 서버의 다운 및 기타 이유로 모니터링 데이터가 전달되지 못할떄를 대비하여 TimeStamp 를 전달한다.
		// 이는 모니터링 클라이언트에서 계산,비교 사용한다.
		// 
		//	{
		//		WORD	Type
		//
		//		BYTE	DataType				// 모니터링 데이터 Type 하단 Define 됨.
		//		int		DataValue				// 해당 데이터 수치.
		//		int		TimeStamp				// 해당 데이터를 얻은 시간 TIMESTAMP  (time() 함수)
		//										// 본래 time 함수는 time_t 타입변수이나 64bit 로 낭비스러우니
		//										// int 로 캐스팅하여 전송. 그래서 2038년 까지만 사용가능
		//	}
		//
		//------------------------------------------------------------
		//	en_PACKET_SS_MONITOR_DATA_UPDATE,

		if (m_LanClientArr[(WORD)SessionID].SessionID != SessionID)
		{
			OnError(GGM_ERROR::SESSION_SYNC_FAILED, _T("[CLanServerMonitor] OnClientJoin Session Sync Error %d"));
			return;
		}

		// NetServer에게 데이터가 도착했음을 알려서 모니터링 뷰어들에게 해당 데이터를 송신해줌		
		char * pData = (char*)pPacket->GetReadPtr();

		// 이 함수는 Net 모니터링 서버의 함수로서, 전달된 정보를 모니터링 뷰어들에게 중개해준다.
		m_pNetMonitor->Monitor_Tool_Data_Update(m_LanClientArr[(WORD)SessionID].ServerNo, pData);

		// DB 스레드가 DB에 저장할 수 있도록 1분동안의 데이터를 보관한다.		
		int  DataValue = *((int*)(pData + 1));

		// 데이터가 0이라면 의미없는 데이터로 취급하여 저장하지 않음
		if (DataValue == 0)
			return;

		// DB 스레드와 LanServer의 워커스레드가 동시에 접근하는 데이터이므로 락 건다.
		BYTE DataType = *((BYTE*)pData);
		MonitoringData *pMonitoringData = &m_DataArray[DataType];

		AcquireSRWLockExclusive(&pMonitoringData->DataLock);

		// 가장 최신의 데이터를 저장 
		pMonitoringData->DataValue = DataValue;

		// 유효한 데이터의 개수를 증가, DB 저장 스레드는 이 카운트를 확인해서 DB 저장 여부를 판단
		pMonitoringData->DataCount++;

		// 1분동안의 데이터 평균치를 구하기 위해 합계를 갱신
		pMonitoringData->DataSum += DataValue;

		// 최댓값, 최솟값 갱신
		if (DataValue > pMonitoringData->MaxValue)
			pMonitoringData->MaxValue = DataValue;
		else if (DataValue < pMonitoringData->MinValue)
			pMonitoringData->MinValue = DataValue;

		ReleaseSRWLockExclusive(&pMonitoringData->DataLock);
	}

	unsigned int __stdcall CLanServerMonitor::WriteDB(LPVOID ParmaThis)
	{
		CLanServerMonitor *pThis = (CLanServerMonitor*)ParmaThis;
		
		// DB에 저장될 데이터가 포함된 배열과 그 원소의 개수
		MonitoringData *pDataArr = pThis->m_DataArray;	
		ULONGLONG       DataArrSize = pThis->m_DataArrSize;

		// DB Connector
		CDBConnector   *pDBConnector = pThis->m_pDBConnector;	

		// 1분마다 깨어나서 모니터링 정보를 DB에 저장함 
		while (true)
		{
			DWORD ret = SleepEx(DB_WRITE_PERIOD, true);

			// 종료로직 
			if (ret == WAIT_IO_COMPLETION)
				break;

			// 배열 순회하며 데이터 획득하여 DB에 저장 
			for (ULONGLONG DataType = 1; DataType < DataArrSize; DataType++)
			{
				// 데이터가 중간에 변하면 안되기 때문에 데이터 락 건다.
				AcquireSRWLockExclusive(&pDataArr[DataType].DataLock);

				// 1분동안 온 데이터 집계 수, 0 이면 DB에 데이터 저장하지 않음
				size_t DataCount = pDataArr[DataType].DataCount;

				if (DataCount == 0)
				{
					ReleaseSRWLockExclusive(&pDataArr[DataType].DataLock);
					continue;
				}

				// 여기서 DB에 저장하니 다시 데이터 카운트는 0이다.
				pDataArr[DataType].DataCount = 0;

				// 가장 최근에 온 데이터를 저장
				int DataValue = pDataArr[DataType].DataValue;

				// 1분동안 온 데이터의 총계 DB 저장시 평균 구할 때 사용
				ULONGLONG DataSum = pDataArr[DataType].DataSum;
				pDataArr[DataType].DataSum = 0;

				// 1분동안 온 데이터 중 최댓값
				int MaxValue = pDataArr[DataType].MaxValue;
				pDataArr[DataType].MaxValue = 0;

				// 1분동안 온 데이터 중 최솟값
				int MinValue = pDataArr[DataType].MinValue;
				pDataArr[DataType].MinValue = 0x7fffffff;

				ReleaseSRWLockExclusive(&pDataArr[DataType].DataLock);

				// 데이터 얻을 것은 다 얻었으니 락풀고 DB에 저장하자
				// DB Insert시 이 스레드가 블락상태에 빠지므로 락을 풀고 저장한다.
				
				pDBConnector->SendQuery(false,
					"INSERT INTO `monitor_log`.`monitorlog_201901`"
					"(`logtime`, `serverno`, `servername`, `type`, `value`, `min`, `max`, `avr`)"
					"VALUES (NOW(), '%d', '%s', '%d', '%d', '%d', '%d', '%f')", 
					pDataArr[DataType].ServerNo, // 해당 데이터를 보낸 서버의 번호
					pDataArr[DataType].ServerName, // 해당 데이터를 보낸 서버의 이름 
					DataType, // 해당 데이터의 타입
					DataValue, // 데이터의 값 (0 제외)
					MinValue, // 1분동안 수집된 데이터의 최소값 (0 제외)
					MaxValue, // 1분동안 수집된 데이터의 최대값 (0 제외)
					((float)DataSum / (float) DataCount) // 1분동안 수집된 유의미한 데이터의 평균
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
		// 서버 설정 파일 로드
		bool Ok = parser.LoadFile(ConfigFileName);
		DWORD err = GetLastError();
		// ---------------------------------------------------------------------------------------------------

		// ---------------------------------------------------------------------------------------------------
		// 서버 구동시 모든 정보는 로그로 남긴다.
		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("MONITOR_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("MONITOR_SERVER_INFO CONFIG FILE OPEN FAILED"));
			return 0;
		}

		CLogger::GetInstance()->Log(_T("MONITOR_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("========== LAN_SERVER_MONITOR OPEN START ========== "));
		// ---------------------------------------------------------------------------------------------------

		// ---------------------------------------------------------------------------------------------------
		// 구역 설정
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