#pragma once
#include "CLanServer\CLanServer.h"
#include "ConfigLoader\ConfigLoader.h"
#include "Define\GGM_CONSTANTS.h"

namespace GGM
{
	constexpr auto SESSION_KEY_LEN = 64;

	// ���� �������� 
	// �ܺ� CONFIG ���Ϸκ��� �ļ��� �̿��ؼ� ������ �´�.
	class LanServerLoginConfig : public CConfigLoader
	{
	public:

		TCHAR  BindIP[16];
		USHORT Port;		
		DWORD  ConcurrentThreads;
		DWORD  MaxThreads;
		bool   IsNoDelay;
		WORD   MaxSessions;
		int    LogLevel;

	public:

		virtual bool LoadConfig(const TCHAR* ConfigFileName) override;

	};

	////////////////////////////////////////////////////////////////////
	// ���Ӽ���, ä�ü����� �α��� ������ �ְ�޴� �α��μ����� Lan��Ʈ
	// �α��μ��� (CLanServer) <----> ���Ӽ���, ä�ü��� (CLanClient)
	// Ŭ���̾�Ʈ�� ���ʷ� �α��μ����� ���� ���Ӽ���, ä�ü����� �̰���
	////////////////////////////////////////////////////////////////////

	// NetServerLogin���� �����ϴ� User ����ü ���漱��
	struct User;

	// CLanServerLogin�� �����ϴ� Ŭ���̾�Ʈ 
	// ���Ӽ����� ä�ü����� �ȴ�.	

	class CNetServerLogin;

	class CLanServerLogin : public CLanServer
	{

	public:

		CLanServerLogin() = delete;
		CLanServerLogin(LanServerLoginConfig *pLanLoginConfig, CNetServerLogin *pNetLogin, User *UserArr);
		virtual ~CLanServerLogin();

		// ������ �α��� ��û�� �ϸ� NetServer�� DB���� ������ �ܾ�� ���������� Ȯ���ϰ� �� �Լ��� ȣ���Ѵ�.
		// �� �Լ� ���ο����� �ش� �α��� ��û ������ LanClient���� �����Ѵ�.
		void SendToken(INT64 AccountNo, char* SessionKey, ULONGLONG Unique);

	protected:

		// CLanServer�� �̺�Ʈ �ڵ鸵�� ���������Լ��� �̰����� ����

		// Accept ���� �� ������ Ȯ���Ǹ� ȣ�� 
		virtual void OnClientJoin(const SOCKADDR_IN& ClientAddr, ULONGLONG SessionID) override;

		// �ش� ���ǿ� ���� ��� ���ҽ��� �����Ǹ� ȣ��  
		virtual void OnClientLeave(ULONGLONG SessionID) override;

		// Accept ���� ȣ�� 
		virtual bool OnConnectionRequest(const SOCKADDR_IN& ClientAddr) override;

		// ��Ŷ ���� �Ϸ� �� ȣ��
		virtual void OnRecv(ULONGLONG SessionID, CSerialBuffer *pPacket) override;

		// ��Ŷ �۽� �Ϸ� �� ȣ��
		virtual void OnSend(ULONGLONG SessionID, int SendSize) override;

		// ��Ŀ������ GQCS �ٷ� �ϴܿ��� ȣ�� 
		virtual void OnWorkerThreadBegin() override;

		// ��Ŀ������ 1���� ���� �� ȣ��
		virtual void OnWorkerThreadEnd() override;

		// ���������� ����Ǵ� �Լ����� ������ �߻��� �� �� �Լ��� ȣ���ؼ� �˷��� 
		virtual void OnError(int ErrorNo, const TCHAR* ErrorMsg) override;

	protected:

		/////////////////////////////////////////////////
		// CLanServerLogin ��Ŷ ���ν���
		/////////////////////////////////////////////////

		// LanClient ���� ��û ��Ŷ �۽� >> SS_LoginServer_Login_Proc
		void SS_LoginServer_Login_Proc(ULONGLONG SessionID, CSerialBuffer *pPacket);

		// LanServer�� ������ū LanClient�� �۽� >> LanClient�� �޸𸮿� ���� >> LanClient�� LanServer�� ���� ��Ŷ �۽� >> SS_Res_New_Client_Proc
		void SS_Res_New_Client_Proc(CSerialBuffer *pPacket);

	protected:

		// NetServerLogin�� ������, LanClient�κ��� ������ �޾��� �� NetServerLogin���� �˷��ֱ� ���� �ʿ���.
		CNetServerLogin  *m_pNetLogin = nullptr;

		// LanServer�� ����� LanClient�� �����ϱ� ���� �迭
		BYTE              *m_LanClientArr = nullptr;
		
		SRWLOCK           m_lock;
		
		ULONGLONG         m_ChatID = INIT_SESSION_ID;
		ULONGLONG         m_GameID = INIT_SESSION_ID;
		ULONGLONG         m_MonitorID = INIT_SESSION_ID;

		// ���Ӽ���, ä�ü����� �ְ�޴� ���������� ����� �迭
		User             *m_UserArray = nullptr;	

	};
}
