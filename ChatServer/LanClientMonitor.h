#pragma once
#include "CLanClient\CLanClient.h"
#include "ConfigLoader\ConfigLoader.h"

namespace GGM
{
	// ����͸� ������ �����͸� �۽��ϴ� �ֱ� 
	constexpr auto MONITOR_PERIOD = 1000;

	// ���� �������� 
	// �ܺ� CONFIG ���Ϸκ��� �ļ��� �̿��ؼ� ������ �´�.
	class LanClientMonitorConfig : public CConfigLoader
	{
	public:

		TCHAR  ServerIP[16];
		USHORT Port;
		DWORD  ConcurrentThreads;
		DWORD  MaxThreads;
		bool   IsNoDelay;
		int    LogLevel;
		bool   IsReconnect;
		int    ReconnectDelay;
		int    ServerNo;
		bool   IsSysCollector;

	public:

		virtual bool LoadConfig(const TCHAR* ConfigFileName) override;
	};	

	// ���漱��

	// ä�ü��� ������ (����͸� ������ ������ ���� �ʿ���)
	class CNetServerChat;
	
	// CPU ���� ���ϴ� Ŭ����
	class CCpuUsage;

	// �����ս� ������ ���� Ŭ���� 
	class CPerfCollector;

	// �� Ŭ���̾�Ʈ �����
	// ����͸� �������� ���� ä�ü����� ����͸� ������ �۽��Ѵ�.
	class CLanClientMonitor : public CLanClient
	{
	public:

		CLanClientMonitor() = delete;

		CLanClientMonitor(LanClientMonitorConfig *pLanConfig, CNetServerChat *pNetChat);

		virtual ~CLanClientMonitor();

	protected:

		/////////////////////////////////////////////////////////////////
		// * ��Ʈ��ũ ���̺귯�� ���� �����Լ�
		// * ��� �̺�Ʈ �ڵ鸵 �Լ��� ���������Լ��μ� ��ӹ޴� �ʿ��� �����Ѵ�.
		/////////////////////////////////////////////////////////////////		

		// LanServer���� ������ ������ ���� ȣ�� 
		virtual void OnConnect() override;

		// �� ���ǿ� ���� I/O Count�� �����ǰ� ���� �� ���ҽ��� �����Ǿ��� �� ȣ��ȴ�. 
		virtual void OnDisconnect() override;

		// ��Ŷ ���� �Ϸ� �� ȣ��
		virtual void OnRecv(CSerialBuffer *Packet) override;

		// ��Ŷ �۽� �Ϸ� �� ȣ��
		virtual void OnSend(int SendSize) override;

		// ��Ŀ������ GQCS �ٷ� �ϴܿ��� ȣ�� 
		virtual void OnWorkerThreadBegin() override;

		// ��Ŀ������ 1���� ���� �� ȣ��
		virtual void OnWorkerThreadEnd() override;

		// ���������� ����Ǵ� �Լ����� ������ �߻��� �� �� �Լ��� ȣ���ؼ� �˷��� 
		virtual void OnError(int ErrorNo, const TCHAR* ErrorMsg) override;		

		// ��Ŷ ���ν���
		void Monitor_Login_Proc(int ServerNo);
		void Monitor_Data_Update_Proc(BYTE DataType, int DataValue, int TimeStamp);

		// ��Ŷ ���� �Լ�
		void CreatePacket_Monitor_Login(CSerialBuffer *pPacket, int ServerNo);
		void CreatePacket_Monitor_Data_Update(CSerialBuffer *pPacket, BYTE DataType, int DataValue, int TimeStamp);

		// ����͸� ������ �Լ�
		static unsigned int __stdcall MonitorThread(LPVOID Param);

		// ����� �����Լ�
		static void __stdcall LanMonitorExitFunc(ULONG_PTR Param);	
	
	protected:

		// ���� ��ȿ��
		bool            m_IsConnected = false;

		// �� ���� ��ȣ
		int             m_ServerNo;

		// �ý��� ��ü �����͸� �����ؾ��ϴ� �����ΰ�
		bool            m_IsSysCollector;

		// ����͸� ������ �ڵ�
		HANDLE          m_hMonitorThread;		

		// ä�ü��� ������
		CNetServerChat  *m_pNetChat;

		// CPU ���� ���
		CCpuUsage	   *m_pCpuUsage;

		// �����ս� ������ ����
		CPerfCollector *m_pPerfCollector;
	};

}