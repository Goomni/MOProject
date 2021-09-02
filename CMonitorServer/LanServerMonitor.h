#pragma once
#include "CLanServer\CLanServer.h"
#include "ConfigLoader\ConfigLoader.h"
#include "Define\GGM_CONSTANTS.h"

namespace GGM
{
	// ����͸� ���� DB ���� �ֱ� 
	constexpr auto DB_WRITE_PERIOD = 60000;

	///////////////////////////////////////////////////////////////////////////////////////////
	// �� �����κ��� ����͸� ������ ���޹޾Ƽ� ����� ����͸� Ŭ���̾�Ʈ�� ���ش�.
	// ���� �۵� �帧
	// 1. ä��, ��Ʋ, ��Ī ���� �ٸ� �������� LanClient�μ� LanServer�� ����͸� ������ �����Ѵ�.
	// 2. ����͸� ���� ����� ����͸� Ŭ���̾�Ʈ�� NetClient�μ� NetServer�� ����͸� ������ �����Ѵ�.
	// 3. LanClient�� LanServer���� 1�ʿ� �ѹ��� ����͸� ������ �۽��Ѵ�.
	//  3.1. ����͸� ������ ũ�� �ý��� ��ü ������ ������ ������ ������.
	//  3.2. �ý��� ��ü ������ �ϳ��� ������ ����ϰ�, �������� �������� ���� ����Ѵ�.
	// 4. LanServer�� LanClient���� ���޹��� ������ NetClient���� �۽��Ѵ�. �ܼ� �߰� ����
	// 5. 4������ ���޹��� ������ �޸𸮿� �����صξ��ٰ� 1�п� �ѹ��� �α� ��� �����Ѵ�.
	//  5.1. �α� ��񿡴� �ش� ���� ����͸� �������� ���� �ֽ�����, ���, �ִ�, �ּ� ���� ����.	
	///////////////////////////////////////////////////////////////////////////////////////////	

	// ���� �������� 
	// �ܺ� CONFIG ���Ϸκ��� �ļ��� �̿��ؼ� ������ �´�.
	class LanServerMonitorConfig : public CConfigLoader
	{
	public:

		TCHAR  BindIP[16];
		USHORT Port;
		DWORD  ConcurrentThreads;
		DWORD  MaxThreads;
		bool   IsNoDelay;
		WORD   MaxSessions;
		int    LogLevel;
		int    NumOfDataType;

		// ������ȣ
		int    SysCollectorNo;
		int    MatchServerNo;
		int    MasterServerNo;
		int    BattleServerNo;
		int    ChatServerNo;

		// ����͸� ������ 1���ֱ�� DB�� �����ؾ� �ϹǷ� DB ���������� �������Ϸκ��� �޴´�.
		TCHAR  DB_ip[17];
		TCHAR  DB_username[65];
		TCHAR  DB_password[65];
		TCHAR  DB_dbname[65];
		USHORT DB_port;	
	public:

		virtual bool LoadConfig(const TCHAR* ConfigFileName) override;

	};	
	
	// LanClient�κ��� ���޹��� ����͸� �����͸� ����͸� ���� �������� �� �ʿ���
	// LanClient�� NetServer�� �����ϴ� �Լ��� ���� �����͸� �۽��Ѵ�.
	class CNetServerMonitor;

	// DB ������ ����� Ŀ����		
	class CDBConnector;

	// 1�и��� �ѹ��� DB�� ������ �� ������ ����ü 
	struct MonitoringData
	{
		// ���� �ֱٿ� �� �����͸� ����, �ֱ� 1�е��� �ƹ��� �����͵� ���� �ʾҴٸ� DB�� �������� ����
		int DataValue;

		// 1�е��� �� ������ ���� ��, 0 �̸� DB�� ������ �������� ����
		size_t DataCount = 0;

		// 1�е��� �� �������� �Ѱ� DB ����� ��� ���� �� ���
		ULONGLONG DataSum = 0;

		// 1�е��� �� ������ �� �ִ�
		int MaxValue = 0;

		// 1�е��� �� ������ �� �ּڰ�
		int MinValue = 0x7fffffff;

		// ����ȭ�� ���� ��ü
		// DB ���� ������� LanClient�� ��Ŀ �����尡 �� ����ü�� ���������ϹǷ� ����ȭ �ʿ��ϴ�.
		SRWLOCK DataLock;

		// �ش� �����͸� �۽��� ������ ����
		char    ServerName[256];
		int     ServerNo;
	};

	// ����͸� �����͸� �����ϴ� LanClient���� ����ü 
	struct MonitoringWorker
	{
		int        ServerNo;
		ULONGLONG  SessionID = INIT_SESSION_ID;
	};

	class CLanServerMonitor : public CLanServer
	{
	public:

		// �⺻�����ڴ� ������� �ʴ´�. �ݵ�� ���������� �޾ƿ;� ��
		CLanServerMonitor() = delete;

		/////////////////////////////////////////////////////////
		// ������
		// ���� 1 : LanServerMonitor�� ���� ��������
		// ���� 2 : CNetServerMonitor�� ������
		// ��ȯ   : ����
		/////////////////////////////////////////////////////////
		CLanServerMonitor(LanServerMonitorConfig *pLanMonitorConfig, CNetServerMonitor *pNetMonitor);

		/////////////////////////////////////////////////////////
		// �Ҹ���
		// ���� : ����
		// ��ȯ : ����
		/////////////////////////////////////////////////////////
		virtual ~CLanServerMonitor();	

		ULONGLONG GetLanClientCount() const;

	protected:

		// CLanServer�� �̺�Ʈ �ڵ鸵�� ���������Լ��� �̰����� ����

		// Accept ���� �� ������ Ȯ���Ǹ� ȣ�� 
		virtual void OnClientJoin(const SOCKADDR_IN& ClientAddr, ULONGLONG SessionID) override;

		// �ش� ���ǿ� ���� ��� ���ҽ��� �����Ǹ� ȣ��  
		virtual void OnClientLeave(ULONGLONG SessionID) override;

		// Accept ���� ȣ�� 
		virtual bool OnConnectionRequest(const SOCKADDR_IN& ClientAddr) override;

		// ��Ŷ ���� �Ϸ� �� ȣ��
		virtual void OnRecv(ULONGLONG SessionID, CPacket *pPacket) override;

		// ��Ŷ �۽� �Ϸ� �� ȣ��
		virtual void OnSend(ULONGLONG SessionID, int SendSize) override;

		// ��Ŀ������ GQCS �ٷ� �ϴܿ��� ȣ�� 
		virtual void OnWorkerThreadBegin() override;

		// ��Ŀ������ 1���� ���� �� ȣ��
		virtual void OnWorkerThreadEnd() override;

		// ���������� ����Ǵ� �Լ����� ������ �߻��� �� �� �Լ��� ȣ���ؼ� �˷��� 
		virtual void OnError(int ErrorNo, const TCHAR* ErrorMsg) override;
	
		/////////////////////////////////////////////////
		// CLanServerMonitor ��Ŷ ���ν���
		/////////////////////////////////////////////////

		// �Ϲݼ����� LanClient�� ����͸� ������ LanServer�� �α��� ��û 
		void Monitor_Login(ULONGLONG SessionID, CPacket *pPacket);	

		// �Ϲݼ����� LanClient�� ����͸� ������ ����͸� ������ ����
		void Monitor_Data_Update(ULONGLONG SessionID, CPacket *pPacket);

		///////////////////////////////////////////////////////////////
		// WriteDB
		// ���� : LPVOID pThis [ CLanServerMonitor ��ü�� This ������ ]
		// ��ȯ : unsigned int [ ������ ���� �ڵ� ]
		// ��� : 1�и��� �ֱ������� ����͸� ������ DB�� �����Ѵ�.
		///////////////////////////////////////////////////////////////

		static unsigned int __stdcall WriteDB(LPVOID pThis);

		///////////////////////////////////////////////////////////////
		// DBWriterExitFunc : DB Writer �����带 ������ ���� �ݹ� �Լ�	
		///////////////////////////////////////////////////////////////
		static void __stdcall DBWriterExitFunc(ULONG_PTR Param);

	protected:

		// CNetServerMonitor�� ������, LanClient�κ��� ������ �޾��� �� CNetServerMonitor���� �˷��ֱ� ���� �ʿ���.
		CNetServerMonitor  *m_pNetMonitor = nullptr;

		// LanServer�� ����� LanClient�� �����ϱ� ���� �迭
		MonitoringWorker  *m_LanClientArr = nullptr;
		ULONGLONG          m_LanClientCount = 0;					

		// 1�и��� �����͸� �ֱ������� ������ ������ ���
		MonitoringData    *m_DataArray = nullptr;
		ULONGLONG          m_DataArrSize = 0;

		// TLS DB
		CDBConnector      *m_pDBConnector = nullptr;

		// DB ���� �������� �ڵ�
		HANDLE             m_hDBWriter;	
	};
}