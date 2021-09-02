#pragma once
#include "CNetServer\CNetServer.h"
#include "ConfigLoader\ConfigLoader.h"

namespace GGM
{
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
	class NetServerMonitorConfig : public CConfigLoader
	{
	public:

		TCHAR  BindIP[16];
		USHORT Port;
		DWORD  ConcurrentThreads;
		DWORD  MaxThreads;
		bool   IsNoDelay;
		WORD   MaxSessions;
		BYTE   PacketCode;
		BYTE   PacketKey1;
		BYTE   PacketKey;
		int    LogLevel;
		TCHAR  LoginSessionkey[40];			

	public:

		virtual bool LoadConfig(const TCHAR* ConfigFileName) override;

	};

	// ����͸� ������ ����� ����͸� ����� ����	
	struct Viewer
	{
		ULONGLONG SessionID = INIT_SESSION_ID;
		bool      IsLogin = false;
	};

	// ���漱�� 

	// ����͸� ������ LanServer�� ��������
	class LanServerMonitorConfig;

	// ����͸� ������ LanServer�� ������
	class CLanServerMonitor;

	// NetClient�� ����͸� Ŭ���̾�Ʈ�� ����ϴ� NetServer
	class CNetServerMonitor : public CNetServer
	{
	public:

		// �⺻�����ڴ� ������� �ʴ´�. �ݵ�� ���������� �޾ƿ;� ��
		CNetServerMonitor() = delete;

		/////////////////////////////////////////////////////////
		// ������
		// ���� 1 : CNetServerMonitor�� ���� ��������
		// ���� 2 : LanServerMonitorConfig �� ���� ��������
		// ��ȯ   : ����
		/////////////////////////////////////////////////////////
		CNetServerMonitor(NetServerMonitorConfig *pNetConfig, LanServerMonitorConfig *pLanConfig);

		/////////////////////////////////////////////////////////
		// �Ҹ���
		// ���� : ����
		// ��ȯ : ����
		/////////////////////////////////////////////////////////
		virtual ~CNetServerMonitor();

		// ����͸� ������ ����͸� ������ ������ ����
		void Monitor_Tool_Data_Update(BYTE ServerNo, char *pData);

		void PrintInfo();

	private:

		//////////////////////////////////////////////
		// ���� �̺�Ʈ �ڵ鸵 �Լ�
		//////////////////////////////////////////////

		// Accept ���� �� ������ Ȯ���Ǹ� ȣ�� 
		virtual void OnClientJoin(const SOCKADDR_IN& ClientAddr, ULONGLONG SessionID) override;

		// �ش� ���ǿ� ���� ��� ���ҽ��� �����Ǹ� ȣ��  
		virtual void OnClientLeave(ULONGLONG SessionID) override;

		// Accept ���� ȣ�� 
		virtual bool OnConnectionRequest(const SOCKADDR_IN& ClientAddr) override;

		// ��Ŷ ���� �Ϸ� �� ȣ��
		virtual void OnRecv(ULONGLONG SessionID, CNetPacket *pPacket) override;

		// ��Ŷ �۽� �Ϸ� �� ȣ��
		virtual void OnSend(ULONGLONG SessionID, int SendSize) override;

		// ��Ŀ������ GQCS �ٷ� �ϴܿ��� ȣ�� 
		virtual void OnWorkerThreadBegin() override;

		// ��Ŀ������ 1���� ���� �� ȣ��
		virtual void OnWorkerThreadEnd() override;

		// ���������� ����Ǵ� �Լ����� ������ �߻��� �� �� �Լ��� ȣ���ؼ� �˷��� 
		virtual void OnError(int ErrorNo, const TCHAR* ErrorMsg) override;

		virtual void OnSemError(ULONGLONG SessionID) override;

		///////////////////////////////////////////////////////////////////////////
		// ��Ŷ ���ν��� : ���γ����� ���ν��� ���� 
		///////////////////////////////////////////////////////////////////////////

		// ����͸� ���� ����͸� �������� �α��� ��û 
		void Monitor_Tool_Req_Login(ULONGLONG SessionID, CNetPacket *pPacket);

		// ����͸� ������ ����͸� ���� �α��� ���� 
		void Monitor_Tool_Res_Login(ULONGLONG SessionID, BYTE status);
		void CreatePacket_Monitor_Tool_Res_Login(BYTE status, CNetPacket *pPacket);

		// ����͸� ������ ����͸� ������ ������ ����	
		void CreatePacket_Monitor_Tool_Data_Update(BYTE ServerNo, char *pData, CNetPacket *pPacket);

	private:

		// ����� ����͸� ��� �����迭, NetPacket�� �����ϱ� ���� ���� ���̵� ������ �ȴ�.
		Viewer		      *m_ViewerArr;
		
		// ����� ����͸� ����� ��
		size_t			   m_ViewerCount = 0;
		size_t             m_MaxViewer = 0;

		// ��� ���� �迭�� ��
		SRWLOCK            m_Lock;

		// CLanServerMonitor�� ������ 
		CLanServerMonitor *m_pLanMonitor;

		// ���� ����Ű
		char               m_SessionKey[33];
	};

}