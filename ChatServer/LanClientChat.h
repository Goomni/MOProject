#pragma once
#include "CLanClient\CLanClient.h"
#include "ConfigLoader\ConfigLoader.h"
#include <unordered_map>

namespace GGM
{	
	// ���� �������� 
	// �ܺ� CONFIG ���Ϸκ��� �ļ��� �̿��ؼ� ������ �´�.
	class LanClientChatConfig : public CConfigLoader
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

	public:

		virtual bool LoadConfig(const TCHAR* ConfigFileName) override;

	};

	// ���� ��ū
	struct Token
	{
		char       SessionKey[64];
		ULONGLONG  InsertTime;
	};

	// ���漱��
	class CNetServerChat;	

	// �� Ŭ���̾�Ʈ ä��
	// �α��� ������ ����ϸ鼭 Ŭ���̾�Ʈ �α��� ��û�� ���� ������ū�� �޾ƿ´�.
	class CLanClientChat : public CLanClient
	{
	public:

		CLanClientChat() = delete;

		CLanClientChat(
			LanClientChatConfig               *pLanConfig,  // ���� ������ ���� ����
			CNetServerChat				      *pNetChat,    // CNetServerChat ������ (HAS-A)
			std::unordered_map<INT64, Token*> *pTokenTable, // ��ū ���� ���̺� 
			PSRWLOCK                           pLock,       // ��ū �� ��ü
			CTlsMemoryPool<Token>             *pTokenPool   // ��ū �޸� Ǯ
		);

		virtual ~CLanClientChat();

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

		void Res_Client_Login_Proc(INT64 AccountNo, INT64 Param);
	protected:

		// NetServerChat�� ������
		CNetServerChat                    *m_pNetChat;

		// ��ū ���̺�
		std::unordered_map<INT64, Token*> *m_pTokenTable;
		
		// ��ū ���̺� ��
		PSRWLOCK                           m_pLock;

		// ��ū �Ҵ� Ǯ
		CTlsMemoryPool<Token>             *m_pTokenPool;
	};
}