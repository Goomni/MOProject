#pragma once
#include "CNetServer\CNetServer.h"
#include "ConfigLoader\ConfigLoader.h"

namespace GGM
{
	constexpr auto ID_LEN = 20;
	constexpr auto NICK_LEN = 20;
	constexpr auto IP_LEN = 16;

	// ���� �������� 
	// �ܺ� CONFIG ���Ϸκ��� �ļ��� �̿��ؼ� ������ �´�.	
	class NetServerLoginConfig : public CConfigLoader
	{
	public:

		TCHAR  BindIP[16];
		USHORT Port;
		TCHAR  ChatServerIP[16];		
		USHORT ChatServerPort;		
		DWORD  ConcurrentThreads;
		DWORD  MaxThreads;
		bool   IsNoDelay;
		WORD   MaxSessions;
		BYTE   PacketCode;
		BYTE   PacketKey1;
		BYTE   PacketKey2;
		int    LogLevel;
		TCHAR  DB_ip[16];
		TCHAR  DB_username[64];
		TCHAR  DB_password[64];
		TCHAR  DB_dbname[64];
		USHORT DB_port;

	public:

		virtual bool LoadConfig(const TCHAR* ConfigFileName) override;

	};	

	// �α��� ���� ���� ��� ���� ��ü	
	struct User
	{
		ULONGLONG SessionID = INIT_SESSION_ID; // �÷��̾� �ĺ��� 
		INT64     AccountNo; // ������ȣ		
		TCHAR     ID[ID_LEN];
		TCHAR     Nick[NICK_LEN];
	};	

	/////////////////////////////////////////////////////////////////////
	// ���漱�� 
	/////////////////////////////////////////////////////////////////////

	// ���Ӽ���, ä�ü������� LanClient�� ����� LanServer
	class CLanServerLogin; 	

	// TLS�� Ȱ���Ͽ� �����庰 ��� ������ �����ϱ� ���� ��� Ŀ���� 
	class CDBConnectorTLS;

	// CLanServerLogin�� �ʱ�ȭ�� ���� ���� ������ ��� Ŭ����
	class LanServerLoginConfig;

	///////////////////////////////////////////////////////////////////
	// Net������ ��ӹ��� �α��� ���� Ŭ����
	// 1. Ŭ���̾�Ʈ�� �α��� ��û�� ����
	// 2. LanServer�μ� LanClient�� ���Ӽ���, ä�ü����� ���
	// 3. Ŭ���̾�Ʈ�� ��û�� �ٸ� �������� �߰��ϰ� Ŭ���̾�Ʈ���� �̰�
	///////////////////////////////////////////////////////////////////
	class CNetServerLogin : public CNetServer
	{		
	public:		
		CNetServerLogin() = delete;

		CNetServerLogin(
			NetServerLoginConfig *pNetLoginConfig,
			LanServerLoginConfig *pLanLoginConfig		
		);

		virtual ~CNetServerLogin();	

		//////////////////////////////////////////////////////////////////////////////////////////////////////////
		// �α��� ó���� �帧 
		// 1. ������ �α��� ������ ���� �� �α��� ��Ŷ �۽�
		// 2. �α��� ������ DB���� ���������� �о�� 
		// 3. ������ ���� ��Ŷ�� DB ���������� ��ġ�Ѵٸ� ä�ü���, ���Ӽ����� �� Ŭ���̾�Ʈ���� ������ū ����
		// 4. ������ū�� ���Ź��� ���� �ش� ������ �޸𸮿� �����ϰ� ���� ��Ŷ �۽�
		// 5. ���� ��Ŷ�� ������ �α��� ������ �������� ���������� �α��� ���� ��Ŷ�� ���� 
		// 6. Send �Ϸ������� ���� �α��� ������ �������� ������ ���� ������ ���Ӽ���, ä�ü����� ������		
		//////////////////////////////////////////////////////////////////////////////////////////////////////////

		// 5���� �ش��ϴ� ������ ó���ϱ� ���� �Լ� 
		// CLanServerLogin���� ȣ���ؾ� �ϱ� ������ public���� �д�.
		void Res_Login_Proc(INT64 AccountNo, INT64 SessionID, BYTE Status, TCHAR *IP, USHORT Port);

		// ����͸� ������ ���
		void PrintInfo();

	protected:
	
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

		// ��Ŷ ���� �Լ�
		void CreatePacket_CS_LOGIN_RES(
			CNetPacket *pPacket, 
			INT64 AccountNo, 
			BYTE Status, 
			TCHAR *ID, 
			TCHAR *Nick, 
			TCHAR *GameServerIP,
			USHORT GameServerPort,
			TCHAR *ChatServerIP,
			USHORT ChatServerPort
		);

	protected:

		// �α��� ���� ���� �迭 
		User            *m_UserArray = nullptr;		

		// �α��� ������ ������ ������ �� 
		size_t           m_UserCount = 0;
		
		// CLanServerLogin�� ������, �α��� ó�� �����߿� LanClient�� ����ϱ� ���� ���
	    CLanServerLogin *m_pLanLogin = nullptr;

		// DB �۾��� Ŀ����
		CDBConnectorTLS *m_pDBConnectorTLS = nullptr;

		// �ϴ� ������ �α��� ������ ä�ü��� IP�� �α��� ������ ������ ����
		TCHAR            m_ChatServerIP[16];
		USHORT           m_ChatServerPort;
	};
}