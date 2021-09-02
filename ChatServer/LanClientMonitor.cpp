#include "ChatServer\LanClientMonitor.h"
#include "ChatServer\NetServerChat.h"
#include "CommonProtocol\MonitorProtocol.h"
#include "CpuUsage\CpuUsage.h"
#include "PerfCollector\PerfCollector.h"
#include "Define\GGM_CONSTANTS.h"
#include "Define\GGM_ERROR.h"
#include "Logger\Logger.h"
#include <time.h>

namespace GGM
{
	CLanClientMonitor::CLanClientMonitor(LanClientMonitorConfig * pLanConfig, CNetServerChat *pNetChat)
		: m_ServerNo(pLanConfig->ServerNo), m_IsSysCollector(pLanConfig->IsSysCollector), m_pNetChat(pNetChat)
	{
		// CPU 사용률을 구하기 위해 객체 동적할당
		m_pCpuUsage = new CCpuUsage();

		if (m_pCpuUsage == nullptr)
		{
			OnError(GGM_ERROR::STARTUP_FAILED, _T("[CLanClientMonitor] (new CCpuUsage) MemAlloc Failed %d"));			
		}

		// 퍼포먼스 데이터 얻기위한 객체 동적할당
		m_pPerfCollector = new CPerfCollector;

		if (m_pCpuUsage == nullptr)
		{
			OnError(GGM_ERROR::STARTUP_FAILED, _T("[CLanClientMonitor] (new CCpuUsage) MemAlloc Failed %d"));			
		}		

		// LanClient 구동
		bool bOk = Start(
			pLanConfig->ServerIP,
			pLanConfig->Port,
			pLanConfig->ConcurrentThreads,
			pLanConfig->MaxThreads,
			pLanConfig->IsNoDelay,
			pLanConfig->IsReconnect,
			pLanConfig->ReconnectDelay
		);

		if (bOk == false)
		{
			OnError(GGM_ERROR::STARTUP_FAILED, _T("[CLanClientMonitor] Start Failed %d"));			
		}		

		// 모니터링 스레드가 수집해야할 카운터를 등록한다.		
		if (m_IsSysCollector == true)
		{
			// 이 프로세스가 시스템 전체 하드웨어 사용률을 수집해야한다면 해당 카운터를 등록한다.		

			/////////////////////////////////////////////////////////////////////////
			//  수집하는 항목들은 모니터링 프로토콜에 의거 다음과 같다.			
			//  dfMONITOR_DATA_TYPE_SERVER_CPU_TOTAL = 1,  하드웨어 CPU 사용률 전체
			//	dfMONITOR_DATA_TYPE_SERVER_AVAILABLE_MEMORY, 하드웨어 사용가능 메모리
			//	dfMONITOR_DATA_TYPE_SERVER_NETWORK_RECV, 하드웨어 이더넷 수신 바이트
			//	dfMONITOR_DATA_TYPE_SERVER_NETWORK_SEND, 하드웨어 이더넷 송신 바이트
			//	dfMONITOR_DATA_TYPE_SERVER_NONPAGED_MEMORY, 하드웨어 논페이지 메모리 사용량
			//  단 CPU 사용률은 PDH를 사용하지 않고 직접 계산하여 구한다.
			/////////////////////////////////////////////////////////////////////////

			bool bOk;

			bOk = m_pPerfCollector->AddCounter(AVAILABLE_MBYTES, dfMONITOR_DATA_TYPE_SERVER_AVAILABLE_MEMORY, PDH_FMT_LONG);

			if (bOk == false)
				OnError(GGM_ERROR::ADD_COUNTER_FAILED, _T("[CLanClientMonitor] GGM_ERROR_ADD_COUNTER_FAILED AVAILABLE_MBYTES ErrorCode[%d]"));

			bOk = m_pPerfCollector->AddCounter(NETWORK_BYTES_RECEIVED, dfMONITOR_DATA_TYPE_SERVER_NETWORK_RECV, PDH_FMT_LONG, true);

			if (bOk == false)
				OnError(GGM_ERROR::ADD_COUNTER_FAILED, _T("[CLanClientMonitor] GGM_ERROR_ADD_COUNTER_FAILED NETWORK_BYTES_RECEIVED ErrorCode[%d]"));

			bOk = m_pPerfCollector->AddCounter(NETWORK_BYTES_SENT, dfMONITOR_DATA_TYPE_SERVER_NETWORK_SEND, PDH_FMT_LONG, true);

			if (bOk == false)
				OnError(GGM_ERROR::ADD_COUNTER_FAILED, _T("[CLanClientMonitor] GGM_ERROR_ADD_COUNTER_FAILED NETWORK_BYTES_SENT ErrorCode[%d]"));

			bOk = m_pPerfCollector->AddCounter(POOL_NON_PAGED_BYTES, dfMONITOR_DATA_TYPE_SERVER_NONPAGED_MEMORY, PDH_FMT_LONG);

			if (bOk == false)
				OnError(GGM_ERROR::ADD_COUNTER_FAILED, _T("[CLanClientMonitor] GGM_ERROR_ADD_COUNTER_FAILED POOL_NON_PAGED_BYTES ErrorCode[%d]"));
		}

		// 프로세스 Private Bytes 카운터 추가
		// 이 데이터 타입은 서버 마다 다르다.
		bOk = m_pPerfCollector->AddCounter(PROCESS_PRIVATE_BYTES, dfMONITOR_DATA_TYPE_CHAT_MEMORY_COMMIT, PDH_FMT_LONG);

		if (bOk == false)
			OnError(GGM_ERROR::ADD_COUNTER_FAILED, _T("[CLanClientMonitor] GGM_ERROR_ADD_COUNTER_FAILED PROCESS_PRIVATE_BYTES ErrorCode[%d]"));
		
		// MSDN 권고사항 : 실제로 카운터 데이터를 수집하기전에 최초로 1회 쿼리를 날림
		m_pPerfCollector->CollectQueryData();

		// 모니터링 스레드 생성
		m_hMonitorThread = (HANDLE)_beginthreadex(nullptr, 0, CLanClientMonitor::MonitorThread, this, 0, nullptr);

		if (m_hMonitorThread == NULL)
		{
			OnError(GetLastError(), _T("[CLanClientMonitor] MonitorThread _beginthreadex Failed ErrorCode[%d]"));			
		}
	}

	CLanClientMonitor::~CLanClientMonitor()
	{		
		// 모니터 스레드에게 종료를 알려줌
		QueueUserAPC(CLanClientMonitor::LanMonitorExitFunc, m_hMonitorThread, 0);
		
		// 스레드 종료를 대기
		WaitForSingleObject(m_hMonitorThread, INFINITE);	
		
		// 스레드 핸들 정리
		CloseHandle(m_hMonitorThread);	

		// 모니터링 객체 할당해제
		delete m_pCpuUsage;
		delete m_pPerfCollector;

		// 클라이언트 구동 정지
		Stop();
	}

	void CLanClientMonitor::OnConnect()
	{
		// 연결되면 모니터링 서버에게 로그인 패킷 보냄
		Monitor_Login_Proc(m_ServerNo);		

		// 연결 플래그 ON
		m_IsConnected = true;
	}

	void CLanClientMonitor::OnDisconnect()
	{
		// 연결 플래그 Off
		m_IsConnected = false;
	}

	void CLanClientMonitor::OnRecv(CSerialBuffer * Packet)
	{
		// 모니터링 서버로부터 응답패킷 받지 않음
	}

	void CLanClientMonitor::OnSend(int SendSize)
	{
		// 할일 없음
	}

	void CLanClientMonitor::OnWorkerThreadBegin()
	{
		// 할일 없음
	}

	void CLanClientMonitor::OnWorkerThreadEnd()
	{
		// 할일 없음
	}

	void CLanClientMonitor::OnError(int ErrorNo, const TCHAR * ErrorMsg)
	{
		CLogger::GetInstance()->Log(_T("ChatServer"), LEVEL::DBG, OUTMODE::FILE, ErrorMsg, ErrorNo);
		CCrashDump::ForceCrash();
	}
	
	void CLanClientMonitor::Monitor_Login_Proc(int ServerNo)
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

		// 직렬화 버퍼 Alloc
		CSerialBuffer *pPacket = CSerialBuffer::Alloc();

		// 패킷 생성 
		CreatePacket_Monitor_Login(pPacket, ServerNo);

		// SendPacket
		SendPacket(pPacket);

		// 직렬화버퍼 Alloc에 대한 Free
		CSerialBuffer::Free(pPacket);
	}

	void CLanClientMonitor::Monitor_Data_Update_Proc(BYTE DataType, int DataValue, int TimeStamp)
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

		// 직렬화 버퍼 Alloc
		CSerialBuffer *pPacket = CSerialBuffer::Alloc();

		// 패킷 생성 
		CreatePacket_Monitor_Data_Update(pPacket, DataType, DataValue, TimeStamp);

		// SendPacket
		SendPacket(pPacket);

		// 직렬화버퍼 Alloc에 대한 Free
		CSerialBuffer::Free(pPacket);

	}

	void CLanClientMonitor::CreatePacket_Monitor_Login(CSerialBuffer * pPacket, int ServerNo)
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

		// 패킷 타입
		WORD PacketType = en_PACKET_SS_MONITOR_LOGIN;
		pPacket->Enqueue((char*)&PacketType, sizeof(WORD));

		// 서버 번호
		pPacket->Enqueue((char*)&ServerNo, sizeof(int));

	}

	void CLanClientMonitor::CreatePacket_Monitor_Data_Update(CSerialBuffer * pPacket, BYTE DataType, int DataValue, int TimeStamp)
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

		// 패킷 타입
		WORD PacketType = en_PACKET_SS_MONITOR_DATA_UPDATE;
		pPacket->Enqueue((char*)&PacketType, sizeof(WORD));

		// 데이터 타입
		pPacket->Enqueue((char*)&DataType, sizeof(BYTE));

		// 측정 데이터 
		pPacket->Enqueue((char*)&DataValue, sizeof(int));

		// 타임 스탬프		
		pPacket->Enqueue((char*)&TimeStamp, sizeof(int));		
	}

	unsigned int __stdcall CLanClientMonitor::MonitorThread(LPVOID Param)
	{
		// this 포인터 얻어옴
		CLanClientMonitor *pThis = (CLanClientMonitor*)Param;

		// 측정용 객체들의 포인터 얻어옴
		CCpuUsage		*pCpuUsage = pThis->m_pCpuUsage;
		CPerfCollector  *pPerfCollector = pThis->m_pPerfCollector;
		CNetServerChat  *pNetChat = pThis->m_pNetChat;		
		
		// 이 서버가 시스템 측정정보도 보내주어야 하는가?
		bool             IsSysCollector = pThis->m_IsSysCollector;

		// 랜서버와 연결 잘 되어 있는가?
		bool             *pIsConnected = &(pThis->m_IsConnected);	

		// 타임 스탬프 구한다.
		__time32_t TimeStamp = _time32(nullptr);

		while (true)
		{
			// 일정주기로 모니터링 정보 수집해서 모니터링 서버로 송신
			DWORD ret = SleepEx(GGM::MONITOR_PERIOD, true);

			TimeStamp++;

			// 종료 절차
			if (ret == WAIT_IO_COMPLETION)
				break;

			// 모니터링 서버와 연결끊겼으면 데이터 송신하지 않는다.
			if (*pIsConnected == false)
				continue;					

			// dfMONITOR_DATA_TYPE_CHAT_SERVER_ON, 채팅서버 ON
			pThis->Monitor_Data_Update_Proc(dfMONITOR_DATA_TYPE_CHAT_SERVER_ON, TRUE, TimeStamp);

			// dfMONITOR_DATA_TYPE_CHAT_CPU, 채팅서버 CPU 사용률 (커널 + 유저)
			pCpuUsage->UpdateProcessUsage();
			int ProcessCpuUsage = (int)pCpuUsage->GetProcessTotalUsage();
			pThis->Monitor_Data_Update_Proc(dfMONITOR_DATA_TYPE_CHAT_CPU, ProcessCpuUsage, TimeStamp);

			// dfMONITOR_DATA_TYPE_CHAT_MEMORY_COMMIT, 채팅서버 메모리 유저 커밋 사용량 (Private) Bytes
			pPerfCollector->CollectQueryData();
			PDH_FMT_COUNTERVALUE PrivateMem;
			pPerfCollector->GetFormattedData(dfMONITOR_DATA_TYPE_CHAT_MEMORY_COMMIT, &PrivateMem, PDH_FMT_LONG);
			pThis->Monitor_Data_Update_Proc(dfMONITOR_DATA_TYPE_CHAT_MEMORY_COMMIT, PrivateMem.longValue/1048576, TimeStamp);

			// dfMONITOR_DATA_TYPE_CHAT_PACKET_POOL, 채팅서버 패킷풀 사용량
			pThis->Monitor_Data_Update_Proc(dfMONITOR_DATA_TYPE_CHAT_PACKET_POOL, (int)CNetPacket::PacketPool->GetNumOfChunkNodeInUse(), TimeStamp);

			// dfMONITOR_DATA_TYPE_CHAT_SESSION, 채팅서버 접속 세션전체
			pThis->Monitor_Data_Update_Proc(dfMONITOR_DATA_TYPE_CHAT_SESSION, (int)pNetChat->GetSessionCount(), TimeStamp);

			// dfMONITOR_DATA_TYPE_CHAT_PLAYER, 채팅서버 로그인을 성공한 전체 인원
			pThis->Monitor_Data_Update_Proc(dfMONITOR_DATA_TYPE_CHAT_PLAYER, (int)pNetChat->GetLoginPlayer(), TimeStamp);

			// dfMONITOR_DATA_TYPE_CHAT_ROOM, 배틀서버 방 수
			pThis->Monitor_Data_Update_Proc(dfMONITOR_DATA_TYPE_CHAT_ROOM, 0, TimeStamp);

			// 시스템 정보
			if (IsSysCollector == true)
			{
				// dfMONITOR_DATA_TYPE_SERVER_CPU_TOTAL, 하드웨어 CPU 사용률 전체
				pCpuUsage->UpdateTotalUsage();
				int SysCpuUsage = (int)pCpuUsage->GetSysTotalUsage();
				pThis->Monitor_Data_Update_Proc(dfMONITOR_DATA_TYPE_SERVER_CPU_TOTAL, SysCpuUsage, TimeStamp);

				// dfMONITOR_DATA_TYPE_SERVER_AVAILABLE_MEMORY, 하드웨어 사용가능 메모리
				PDH_FMT_COUNTERVALUE AvailMem;
				pPerfCollector->GetFormattedData(dfMONITOR_DATA_TYPE_SERVER_AVAILABLE_MEMORY, &AvailMem, PDH_FMT_LONG);
				pThis->Monitor_Data_Update_Proc(dfMONITOR_DATA_TYPE_SERVER_AVAILABLE_MEMORY, AvailMem.longValue, TimeStamp);

				// dfMONITOR_DATA_TYPE_SERVER_NETWORK_RECV, 하드웨어 이더넷 수신 바이트
				PDH_FMT_COUNTERVALUE_ITEM *pRecvItems;
				DWORD                      RecvItemCount;
				pPerfCollector->GetFormattedDataArray(dfMONITOR_DATA_TYPE_SERVER_NETWORK_RECV, &pRecvItems, &RecvItemCount, PDH_FMT_LONG);

				LONG RecvBytesTotal = 0;
				for (DWORD i = 0; i < RecvItemCount; i++)
					RecvBytesTotal += pRecvItems[i].FmtValue.longValue;

				pThis->Monitor_Data_Update_Proc(dfMONITOR_DATA_TYPE_SERVER_NETWORK_RECV, RecvBytesTotal/1024, TimeStamp);

				// dfMONITOR_DATA_TYPE_SERVER_NETWORK_RECV, 하드웨어 이더넷 송신 바이트
				PDH_FMT_COUNTERVALUE_ITEM *pSendItems;
				DWORD                      SendItemCount;
				pPerfCollector->GetFormattedDataArray(dfMONITOR_DATA_TYPE_SERVER_NETWORK_SEND, &pSendItems, &SendItemCount, PDH_FMT_LONG);

				LONG SendBytesTotal = 0;
				for (DWORD i = 0; i < SendItemCount; i++)
					SendBytesTotal += pSendItems[i].FmtValue.longValue;

				pThis->Monitor_Data_Update_Proc(dfMONITOR_DATA_TYPE_SERVER_NETWORK_SEND, SendBytesTotal/1024, TimeStamp);

				//dfMONITOR_DATA_TYPE_SERVER_NONPAGED_MEMORY, 하드웨어 논페이지 메모리 사용량
				PDH_FMT_COUNTERVALUE NonPagedPool;
				pPerfCollector->GetFormattedData(dfMONITOR_DATA_TYPE_SERVER_NONPAGED_MEMORY, &NonPagedPool, PDH_FMT_LONG);
				pThis->Monitor_Data_Update_Proc(dfMONITOR_DATA_TYPE_SERVER_NONPAGED_MEMORY, NonPagedPool.longValue/1048576, TimeStamp);
			}
		}

		return 0;
	}

	void __stdcall CLanClientMonitor::LanMonitorExitFunc(ULONG_PTR Param)
	{
		
	}

	bool LanClientMonitorConfig::LoadConfig(const TCHAR * ConfigFileName)
	{
		CParser parser;

		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// 서버 설정 파일 로드
		bool Ok = parser.LoadFile(ConfigFileName);
		DWORD err = GetLastError();
		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// 서버 구동시 모든 정보는 로그로 남긴다.
		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("CHAT_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("SERVER CONFIG FILE OPEN FAILED"));
			return 0;
		}

		CLogger::GetInstance()->Log(_T("CHAT_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("========== LAN_CLIENT_MONITOR OPEN START ========== "));

		parser.SetSpace(_T("#LAN_CLIENT_MONITOR"));
		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////		
		// ServerIP LOAD
		Ok = parser.GetValue(_T("SERVER_IP"), ServerIP);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("CHAT_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET SERVER_IP FAILED : [%s]"), ServerIP);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("CHAT_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("SERVER_IP : [%s]"), ServerIP);
		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// PORT LOAD
		Ok = parser.GetValue(_T("SERVER_PORT"), (short*)&Port);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("CHAT_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET SERVER_PORT FAILED : [%hd]"), Port);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("CHAT_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("SERVER_PORT : [%hd]"), Port);
		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// ConcurrentThreads LOAD
		Ok = parser.GetValue(_T("CONCURRENT_THREADS"), (int*)&ConcurrentThreads);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("CHAT_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET CONCURRENT_THREADS FAILED : [%d]"), ConcurrentThreads);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("CHAT_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("CONCURRENT_THREADS : [%d]"), ConcurrentThreads);
		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// MaxThreads LOAD
		Ok = parser.GetValue(_T("MAX_THREADS"), (int*)&MaxThreads);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("CHAT_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET MAX_THREADS FAILED : [%d]"), MaxThreads);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("CHAT_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("MAX_THREADS : [%d]"), MaxThreads);
		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// IsNoDelay LOAD
		Ok = parser.GetValue(_T("NO_DELAY"), (bool*)&IsNoDelay);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("CHAT_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET NO_DELAY FAILED : [%d]"), IsNoDelay);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("CHAT_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("NO_DELAY : [%d]"), IsNoDelay);
		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// IsReconnect LOAD
		Ok = parser.GetValue(_T("RECONNECT"), (bool*)&IsReconnect);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("CHAT_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET RECONNECT FAILED : [%d]"), IsReconnect);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("CHAT_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("RECONNECT : [%d]"), IsReconnect);
		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// Reconnect Delay LOAD
		Ok = parser.GetValue(_T("RECONNECT_DELAY"), (int*)&ReconnectDelay);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("CHAT_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET RECONNECT_DELAY FAILED : [%d]"), ReconnectDelay);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("CHAT_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("RECONNECT_DELAY : [%d]"), ReconnectDelay);
		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// ServerNo LOAD
		Ok = parser.GetValue(_T("SERVER_NO"), (int*)&ServerNo);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("CHAT_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET SERVER_NO FAILED : [%d]"), ServerNo);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("CHAT_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("SERVER_NO : [%d]"), ServerNo);
		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// IsSysCollector LOAD
		Ok = parser.GetValue(_T("SYS_COLLECTOR"), (bool*)&IsSysCollector);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("CHAT_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET SYS_COLLECTOR FAILED : [%d]"), IsSysCollector);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("CHAT_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("SYS_COLLECTOR : [%d]"), IsSysCollector);
		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// LogLevel LOAD
		Ok = parser.GetValue(_T("LOG_LEVEL"), (int*)&LogLevel);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("CHAT_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET LOG_LEVEL FAILED : [%d]"), LogLevel);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("CHAT_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("LOG_LEVEL : [%d]"), LogLevel);
		CLogger::GetInstance()->SetDefaultLogLevel((LEVEL)LogLevel);
		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

		CLogger::GetInstance()->Log(_T("CHAT_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("========== LAN_CLIENT_MONITOR OPEN SUCCESSFUL ========== "));

		return true;
	}
}