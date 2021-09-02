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
		// CPU ������ ���ϱ� ���� ��ü �����Ҵ�
		m_pCpuUsage = new CCpuUsage();

		if (m_pCpuUsage == nullptr)
		{
			OnError(GGM_ERROR::STARTUP_FAILED, _T("[CLanClientMonitor] (new CCpuUsage) MemAlloc Failed %d"));			
		}

		// �����ս� ������ ������� ��ü �����Ҵ�
		m_pPerfCollector = new CPerfCollector;

		if (m_pCpuUsage == nullptr)
		{
			OnError(GGM_ERROR::STARTUP_FAILED, _T("[CLanClientMonitor] (new CCpuUsage) MemAlloc Failed %d"));			
		}		

		// LanClient ����
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

		// ����͸� �����尡 �����ؾ��� ī���͸� ����Ѵ�.		
		if (m_IsSysCollector == true)
		{
			// �� ���μ����� �ý��� ��ü �ϵ���� ������ �����ؾ��Ѵٸ� �ش� ī���͸� ����Ѵ�.		

			/////////////////////////////////////////////////////////////////////////
			//  �����ϴ� �׸���� ����͸� �������ݿ� �ǰ� ������ ����.			
			//  dfMONITOR_DATA_TYPE_SERVER_CPU_TOTAL = 1,  �ϵ���� CPU ���� ��ü
			//	dfMONITOR_DATA_TYPE_SERVER_AVAILABLE_MEMORY, �ϵ���� ��밡�� �޸�
			//	dfMONITOR_DATA_TYPE_SERVER_NETWORK_RECV, �ϵ���� �̴��� ���� ����Ʈ
			//	dfMONITOR_DATA_TYPE_SERVER_NETWORK_SEND, �ϵ���� �̴��� �۽� ����Ʈ
			//	dfMONITOR_DATA_TYPE_SERVER_NONPAGED_MEMORY, �ϵ���� �������� �޸� ��뷮
			//  �� CPU ������ PDH�� ������� �ʰ� ���� ����Ͽ� ���Ѵ�.
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

		// ���μ��� Private Bytes ī���� �߰�
		// �� ������ Ÿ���� ���� ���� �ٸ���.
		bOk = m_pPerfCollector->AddCounter(PROCESS_PRIVATE_BYTES, dfMONITOR_DATA_TYPE_CHAT_MEMORY_COMMIT, PDH_FMT_LONG);

		if (bOk == false)
			OnError(GGM_ERROR::ADD_COUNTER_FAILED, _T("[CLanClientMonitor] GGM_ERROR_ADD_COUNTER_FAILED PROCESS_PRIVATE_BYTES ErrorCode[%d]"));
		
		// MSDN �ǰ���� : ������ ī���� �����͸� �����ϱ����� ���ʷ� 1ȸ ������ ����
		m_pPerfCollector->CollectQueryData();

		// ����͸� ������ ����
		m_hMonitorThread = (HANDLE)_beginthreadex(nullptr, 0, CLanClientMonitor::MonitorThread, this, 0, nullptr);

		if (m_hMonitorThread == NULL)
		{
			OnError(GetLastError(), _T("[CLanClientMonitor] MonitorThread _beginthreadex Failed ErrorCode[%d]"));			
		}
	}

	CLanClientMonitor::~CLanClientMonitor()
	{		
		// ����� �����忡�� ���Ḧ �˷���
		QueueUserAPC(CLanClientMonitor::LanMonitorExitFunc, m_hMonitorThread, 0);
		
		// ������ ���Ḧ ���
		WaitForSingleObject(m_hMonitorThread, INFINITE);	
		
		// ������ �ڵ� ����
		CloseHandle(m_hMonitorThread);	

		// ����͸� ��ü �Ҵ�����
		delete m_pCpuUsage;
		delete m_pPerfCollector;

		// Ŭ���̾�Ʈ ���� ����
		Stop();
	}

	void CLanClientMonitor::OnConnect()
	{
		// ����Ǹ� ����͸� �������� �α��� ��Ŷ ����
		Monitor_Login_Proc(m_ServerNo);		

		// ���� �÷��� ON
		m_IsConnected = true;
	}

	void CLanClientMonitor::OnDisconnect()
	{
		// ���� �÷��� Off
		m_IsConnected = false;
	}

	void CLanClientMonitor::OnRecv(CSerialBuffer * Packet)
	{
		// ����͸� �����κ��� ������Ŷ ���� ����
	}

	void CLanClientMonitor::OnSend(int SendSize)
	{
		// ���� ����
	}

	void CLanClientMonitor::OnWorkerThreadBegin()
	{
		// ���� ����
	}

	void CLanClientMonitor::OnWorkerThreadEnd()
	{
		// ���� ����
	}

	void CLanClientMonitor::OnError(int ErrorNo, const TCHAR * ErrorMsg)
	{
		CLogger::GetInstance()->Log(_T("ChatServer"), LEVEL::DBG, OUTMODE::FILE, ErrorMsg, ErrorNo);
		CCrashDump::ForceCrash();
	}
	
	void CLanClientMonitor::Monitor_Login_Proc(int ServerNo)
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

		// ����ȭ ���� Alloc
		CSerialBuffer *pPacket = CSerialBuffer::Alloc();

		// ��Ŷ ���� 
		CreatePacket_Monitor_Login(pPacket, ServerNo);

		// SendPacket
		SendPacket(pPacket);

		// ����ȭ���� Alloc�� ���� Free
		CSerialBuffer::Free(pPacket);
	}

	void CLanClientMonitor::Monitor_Data_Update_Proc(BYTE DataType, int DataValue, int TimeStamp)
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

		// ����ȭ ���� Alloc
		CSerialBuffer *pPacket = CSerialBuffer::Alloc();

		// ��Ŷ ���� 
		CreatePacket_Monitor_Data_Update(pPacket, DataType, DataValue, TimeStamp);

		// SendPacket
		SendPacket(pPacket);

		// ����ȭ���� Alloc�� ���� Free
		CSerialBuffer::Free(pPacket);

	}

	void CLanClientMonitor::CreatePacket_Monitor_Login(CSerialBuffer * pPacket, int ServerNo)
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

		// ��Ŷ Ÿ��
		WORD PacketType = en_PACKET_SS_MONITOR_LOGIN;
		pPacket->Enqueue((char*)&PacketType, sizeof(WORD));

		// ���� ��ȣ
		pPacket->Enqueue((char*)&ServerNo, sizeof(int));

	}

	void CLanClientMonitor::CreatePacket_Monitor_Data_Update(CSerialBuffer * pPacket, BYTE DataType, int DataValue, int TimeStamp)
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

		// ��Ŷ Ÿ��
		WORD PacketType = en_PACKET_SS_MONITOR_DATA_UPDATE;
		pPacket->Enqueue((char*)&PacketType, sizeof(WORD));

		// ������ Ÿ��
		pPacket->Enqueue((char*)&DataType, sizeof(BYTE));

		// ���� ������ 
		pPacket->Enqueue((char*)&DataValue, sizeof(int));

		// Ÿ�� ������		
		pPacket->Enqueue((char*)&TimeStamp, sizeof(int));		
	}

	unsigned int __stdcall CLanClientMonitor::MonitorThread(LPVOID Param)
	{
		// this ������ ����
		CLanClientMonitor *pThis = (CLanClientMonitor*)Param;

		// ������ ��ü���� ������ ����
		CCpuUsage		*pCpuUsage = pThis->m_pCpuUsage;
		CPerfCollector  *pPerfCollector = pThis->m_pPerfCollector;
		CNetServerChat  *pNetChat = pThis->m_pNetChat;		
		
		// �� ������ �ý��� ���������� �����־�� �ϴ°�?
		bool             IsSysCollector = pThis->m_IsSysCollector;

		// �������� ���� �� �Ǿ� �ִ°�?
		bool             *pIsConnected = &(pThis->m_IsConnected);	

		// Ÿ�� ������ ���Ѵ�.
		__time32_t TimeStamp = _time32(nullptr);

		while (true)
		{
			// �����ֱ�� ����͸� ���� �����ؼ� ����͸� ������ �۽�
			DWORD ret = SleepEx(GGM::MONITOR_PERIOD, true);

			TimeStamp++;

			// ���� ����
			if (ret == WAIT_IO_COMPLETION)
				break;

			// ����͸� ������ ����������� ������ �۽����� �ʴ´�.
			if (*pIsConnected == false)
				continue;					

			// dfMONITOR_DATA_TYPE_CHAT_SERVER_ON, ä�ü��� ON
			pThis->Monitor_Data_Update_Proc(dfMONITOR_DATA_TYPE_CHAT_SERVER_ON, TRUE, TimeStamp);

			// dfMONITOR_DATA_TYPE_CHAT_CPU, ä�ü��� CPU ���� (Ŀ�� + ����)
			pCpuUsage->UpdateProcessUsage();
			int ProcessCpuUsage = (int)pCpuUsage->GetProcessTotalUsage();
			pThis->Monitor_Data_Update_Proc(dfMONITOR_DATA_TYPE_CHAT_CPU, ProcessCpuUsage, TimeStamp);

			// dfMONITOR_DATA_TYPE_CHAT_MEMORY_COMMIT, ä�ü��� �޸� ���� Ŀ�� ��뷮 (Private) Bytes
			pPerfCollector->CollectQueryData();
			PDH_FMT_COUNTERVALUE PrivateMem;
			pPerfCollector->GetFormattedData(dfMONITOR_DATA_TYPE_CHAT_MEMORY_COMMIT, &PrivateMem, PDH_FMT_LONG);
			pThis->Monitor_Data_Update_Proc(dfMONITOR_DATA_TYPE_CHAT_MEMORY_COMMIT, PrivateMem.longValue/1048576, TimeStamp);

			// dfMONITOR_DATA_TYPE_CHAT_PACKET_POOL, ä�ü��� ��ŶǮ ��뷮
			pThis->Monitor_Data_Update_Proc(dfMONITOR_DATA_TYPE_CHAT_PACKET_POOL, (int)CNetPacket::PacketPool->GetNumOfChunkNodeInUse(), TimeStamp);

			// dfMONITOR_DATA_TYPE_CHAT_SESSION, ä�ü��� ���� ������ü
			pThis->Monitor_Data_Update_Proc(dfMONITOR_DATA_TYPE_CHAT_SESSION, (int)pNetChat->GetSessionCount(), TimeStamp);

			// dfMONITOR_DATA_TYPE_CHAT_PLAYER, ä�ü��� �α����� ������ ��ü �ο�
			pThis->Monitor_Data_Update_Proc(dfMONITOR_DATA_TYPE_CHAT_PLAYER, (int)pNetChat->GetLoginPlayer(), TimeStamp);

			// dfMONITOR_DATA_TYPE_CHAT_ROOM, ��Ʋ���� �� ��
			pThis->Monitor_Data_Update_Proc(dfMONITOR_DATA_TYPE_CHAT_ROOM, 0, TimeStamp);

			// �ý��� ����
			if (IsSysCollector == true)
			{
				// dfMONITOR_DATA_TYPE_SERVER_CPU_TOTAL, �ϵ���� CPU ���� ��ü
				pCpuUsage->UpdateTotalUsage();
				int SysCpuUsage = (int)pCpuUsage->GetSysTotalUsage();
				pThis->Monitor_Data_Update_Proc(dfMONITOR_DATA_TYPE_SERVER_CPU_TOTAL, SysCpuUsage, TimeStamp);

				// dfMONITOR_DATA_TYPE_SERVER_AVAILABLE_MEMORY, �ϵ���� ��밡�� �޸�
				PDH_FMT_COUNTERVALUE AvailMem;
				pPerfCollector->GetFormattedData(dfMONITOR_DATA_TYPE_SERVER_AVAILABLE_MEMORY, &AvailMem, PDH_FMT_LONG);
				pThis->Monitor_Data_Update_Proc(dfMONITOR_DATA_TYPE_SERVER_AVAILABLE_MEMORY, AvailMem.longValue, TimeStamp);

				// dfMONITOR_DATA_TYPE_SERVER_NETWORK_RECV, �ϵ���� �̴��� ���� ����Ʈ
				PDH_FMT_COUNTERVALUE_ITEM *pRecvItems;
				DWORD                      RecvItemCount;
				pPerfCollector->GetFormattedDataArray(dfMONITOR_DATA_TYPE_SERVER_NETWORK_RECV, &pRecvItems, &RecvItemCount, PDH_FMT_LONG);

				LONG RecvBytesTotal = 0;
				for (DWORD i = 0; i < RecvItemCount; i++)
					RecvBytesTotal += pRecvItems[i].FmtValue.longValue;

				pThis->Monitor_Data_Update_Proc(dfMONITOR_DATA_TYPE_SERVER_NETWORK_RECV, RecvBytesTotal/1024, TimeStamp);

				// dfMONITOR_DATA_TYPE_SERVER_NETWORK_RECV, �ϵ���� �̴��� �۽� ����Ʈ
				PDH_FMT_COUNTERVALUE_ITEM *pSendItems;
				DWORD                      SendItemCount;
				pPerfCollector->GetFormattedDataArray(dfMONITOR_DATA_TYPE_SERVER_NETWORK_SEND, &pSendItems, &SendItemCount, PDH_FMT_LONG);

				LONG SendBytesTotal = 0;
				for (DWORD i = 0; i < SendItemCount; i++)
					SendBytesTotal += pSendItems[i].FmtValue.longValue;

				pThis->Monitor_Data_Update_Proc(dfMONITOR_DATA_TYPE_SERVER_NETWORK_SEND, SendBytesTotal/1024, TimeStamp);

				//dfMONITOR_DATA_TYPE_SERVER_NONPAGED_MEMORY, �ϵ���� �������� �޸� ��뷮
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
		// ���� ���� ���� �ε�
		bool Ok = parser.LoadFile(ConfigFileName);
		DWORD err = GetLastError();
		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// ���� ������ ��� ������ �α׷� �����.
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