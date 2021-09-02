#include "LanClientChat.h"
#include "Logger\Logger.h"
#include "CommonProtocol\CommonProtocol.h"
#include "Define\GGM_ERROR.h"

namespace GGM
{
	GGM::CLanClientChat::CLanClientChat(
		LanClientChatConfig * pLanConfig, 
		CNetServerChat * pNetChat, 
		std::unordered_map<INT64, Token*>* pTokenTable, 
		PSRWLOCK pLock,
		CTlsMemoryPool<Token>             *pTokenPool
	) :m_pNetChat(pNetChat), m_pTokenTable(pTokenTable), m_pLock(pLock), m_pTokenPool(pTokenPool)
	{
		// Ŭ���̾�Ʈ ����
		bool Ok = Start(
			pLanConfig->ServerIP,
			pLanConfig->Port,
			pLanConfig->ConcurrentThreads,
			pLanConfig->MaxThreads,
			pLanConfig->IsNoDelay,
			pLanConfig->IsReconnect,
			pLanConfig->ReconnectDelay
		);

		if (Ok == false)
			OnError(GGM_ERROR::STARTUP_FAILED, _T("CLanClientChat Start Failed"));
	}

	GGM::CLanClientChat::~CLanClientChat()
	{
		Stop();
	}

	void GGM::CLanClientChat::OnConnect()
	{
		//------------------------------------------------------------
		// �ٸ� ������ �α��� ������ �α���.
		// �̴� ������ ������, �׳� �α��� ��.  
		//
		//	{
		//		WORD	Type
		//
		//		BYTE	ServerType			// dfSERVER_TYPE_GAME / dfSERVER_TYPE_CHAT
		//
		//		WCHAR	ServerName[32]		// �ش� ������ �̸�.  
		//	}
		//
		//------------------------------------------------------------

		// LanServer�� ������ �Ǿ����� ���� ��û ��Ŷ ������.
		CSerialBuffer *pPacket = CSerialBuffer::Alloc();		

		// ������ ���� ��Ŷ Ÿ��
		WORD PacketType = en_PACKET_SS_LOGINSERVER_LOGIN;

		// ������ Ÿ��
		BYTE ServerType = dfSERVER_TYPE_CHAT;

		pPacket->Enqueue((char*)&PacketType, sizeof(WORD));
		pPacket->Enqueue((char*)&ServerType, sizeof(BYTE));

		pPacket->RelocateWrite(64);

		SendPacket(pPacket);

		// Alloc �� ���� Free
		CSerialBuffer::Free(pPacket);
	}

	void GGM::CLanClientChat::OnDisconnect()
	{
		
	}

	void GGM::CLanClientChat::OnRecv(CSerialBuffer * pPacket)
	{
		//------------------------------------------------------------
		// �α��μ������� ����.ä�� ������ ���ο� Ŭ���̾�Ʈ ������ �˸�.
		//
		// �������� Parameter �� ����Ű ������ ���� ������ Ȯ���� ���� � ��. �̴� ���� ������� �ٽ� �ް� ��.
		// ä�ü����� ���Ӽ����� Parameter �� ���� ó���� �ʿ� ������ �״�� Res �� ������� �մϴ�.
		//
		//	{
		//		WORD	Type
		//
		//		INT64	AccountNo
		//		CHAR	SessionKey[64]
		//		INT64	Parameter
		//	}
		//
		//------------------------------------------------------------	
		
		pPacket->EraseData(sizeof(WORD));

		// ��Ŷ���� �ʿ��� ���� ������		
		INT64 AccountNo;
		pPacket->Dequeue((char*)&AccountNo, sizeof(INT64));

		// ��ū ���̺� ��ū ����
		// CLanClientChat�� ��Ŀ������� CNetServerChat�� ������Ʈ �����尡 ��������
		// �� �ɰ� �ش� ���� ����				
		AcquireSRWLockExclusive(m_pLock);		
		
		// ��ū ����ü ������
		Token *pToken = nullptr;
		
		// �ϴ� �ش� AccountNo�� �ش��ϴ� ��ū�� ���Ե� ���� �ִ��� ã�ƺ���.
		// � ������ ��ū�� �־���� ������ ��� ��ó �α��� ó���� �ȵ� ä�� �ٽ� �������� ���� �ִ�.
		auto iter_find = m_pTokenTable->find(AccountNo);

		// ������ 
		if (iter_find == m_pTokenTable->end())
		{
			// ��ū �ϳ� Alloc()
			// �� ��쿡�� ���ο� ��ū ����ü�� ����Ű ����
			pToken = m_pTokenPool->Alloc();			
			
			// �ϴ� ���̺� ����
			auto pair = m_pTokenTable->insert({ AccountNo, pToken });
			
			// ���⼭�� �����ϸ� ũ����
			if (pair.second == false)
				CCrashDump::ForceCrash();
		}
		else
		{
			// �̹� �ش� AccountNo�� �ش��ϴ� ��ū�� ���ԵǾ� ������ �ִ°� �޾ƿ�
			// �� ��쿡�� �̹� �ִ� ��ū ����ü�� ���ο� ����Ű ���
			pToken = iter_find->second;			
		}		
		
		// ��ū�� ����Ű ����
		pPacket->Dequeue(pToken->SessionKey, 64);		

		// �������� ä�ü����� �α��� ��û���� �ʴ� ������ ���ؼ� �����ϱ� ���� �ð��� ���
		pToken->InsertTime = GetTickCount64();
	
		ReleaseSRWLockExclusive(m_pLock);

		// Login �������� ������ū�� ���������� �޸𸮿� ���������� �۽����ش�.

		// ��ū ��Ŷ�� ����Ű ����
		INT64 Param;
		pPacket->Dequeue((char*)&Param, sizeof(INT64));
		Res_Client_Login_Proc(AccountNo, Param);
	}

	void GGM::CLanClientChat::OnSend(int SendSize)
	{
	}

	void GGM::CLanClientChat::OnWorkerThreadBegin()
	{
	}

	void GGM::CLanClientChat::OnWorkerThreadEnd()
	{
	}

	void GGM::CLanClientChat::OnError(int ErrorNo, const TCHAR * ErrorMsg)
	{
		CLogger::GetInstance()->Log(_T("ChatServer"), LEVEL::DBG, OUTMODE::FILE, ErrorMsg, ErrorNo);
		CCrashDump::ForceCrash();
	}

	void CLanClientChat::Res_Client_Login_Proc(INT64 AccountNo, INT64 Param)
	{
		//------------------------------------------------------------
		// ����.ä�� ������ ���ο� Ŭ���̾�Ʈ ������Ŷ ���Ű���� ������.
		// ���Ӽ�����, ä�ü����� ��Ŷ�� ������ ������, �α��μ����� Ÿ ������ ���� �� CHAT,GAME ������ �����ϹǷ� 
		// �̸� ����ؼ� �˾Ƽ� ���� �ϵ��� ��.
		//
		// �÷��̾��� ���� �α��� �Ϸ�� �� ��Ŷ�� Chat,Game ���ʿ��� �� �޾��� ������.
		//
		// ������ �� Parameter �� �̹� ����Ű ������ ���� ������ �� �ִ� Ư�� ��
		// ClientID �� ����, ���� ī������ ���� ��� ����.
		//
		// �α��μ����� ���Ӱ� �������� �ݺ��ϴ� ��� ������ ���������� ���� ������ ���� ��������
		// �����Ͽ� �ٸ� ����Ű�� ��� ���� ������ ����.
		//
		//	{
		//		WORD	Type
		//
		//		INT64	AccountNo
		//		INT64	Parameter
		//	}
		//
		//------------------------------------------------------------

		// ��Ŷ �ϳ� �Ҵ����
		CSerialBuffer *pPacket = CSerialBuffer::Alloc();

		//////////////////////////////////////////////////////////////////
		// ��Ŷ ����
		WORD PacketType = en_PACKET_SS_RES_NEW_CLIENT_LOGIN;
		pPacket->Enqueue((char*)&PacketType, sizeof(WORD));
		pPacket->Enqueue((char*)&AccountNo, sizeof(INT64));
		pPacket->Enqueue((char*)&Param, sizeof(INT64));	
		//////////////////////////////////////////////////////////////////

		// ��Ŷ �۽�
		SendPacket(pPacket);

		// Alloc �� ���� ����
		CSerialBuffer::Free(pPacket);
	}

	bool LanClientChatConfig::LoadConfig(const TCHAR * ConfigFileName)
	{
		CParser parser;

		// ���� ���� ���� �ε�
		bool Ok = parser.LoadFile(ConfigFileName);
		DWORD err = GetLastError();

		// ���� ������ ��� ������ �α׷� �����.
		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("CHAT_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("SERVER CONFIG FILE OPEN FAILED"));
			return 0;
		}

		CLogger::GetInstance()->Log(_T("CHAT_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("========== LAN_CLIENT_CHAT OPEN START ========== "));

		parser.SetSpace(_T("#LAN_CLIENT_CHAT"));

		// BIND_IP LOAD
		Ok = parser.GetValue(_T("SERVER_IP"), ServerIP);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("CHAT_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET SERVER_IP FAILED : [%s]"), ServerIP);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("CHAT_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("SERVER_IP : [%s]"), ServerIP);

		// PORT LOAD
		Ok = parser.GetValue(_T("SERVER_PORT"), (short*)&Port);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("CHAT_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET SERVER_PORT FAILED : [%hd]"), Port);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("CHAT_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("SERVER_PORT : [%hd]"), Port);

		// ConcurrentThreads LOAD
		Ok = parser.GetValue(_T("CONCURRENT_THREADS"), (int*)&ConcurrentThreads);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("CHAT_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET CONCURRENT_THREADS FAILED : [%d]"), ConcurrentThreads);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("CHAT_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("CONCURRENT_THREADS : [%d]"), ConcurrentThreads);

		// MaxThreads LOAD
		Ok = parser.GetValue(_T("MAX_THREADS"), (int*)&MaxThreads);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("CHAT_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET MAX_THREADS FAILED : [%d]"), MaxThreads);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("CHAT_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("MAX_THREADS : [%d]"), MaxThreads);

		// IsNoDelay LOAD
		Ok = parser.GetValue(_T("NO_DELAY"), (bool*)&IsNoDelay);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("CHAT_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET NO_DELAY FAILED : [%d]"), IsNoDelay);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("CHAT_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("NO_DELAY : [%d]"), IsNoDelay);		

		// IsReconnect LOAD
		Ok = parser.GetValue(_T("RECONNECT"), (bool*)&IsReconnect);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("CHAT_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET RECONNECT FAILED : [%d]"), IsReconnect);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("CHAT_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("RECONNECT : [%d]"), IsReconnect);

		// Reconnect Delay LOAD
		Ok = parser.GetValue(_T("RECONNECT_DELAY"), (int*)&ReconnectDelay);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("CHAT_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET RECONNECT_DELAY FAILED : [%d]"), ReconnectDelay);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("CHAT_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("RECONNECT_DELAY : [%d]"), ReconnectDelay);

		// LogLevel LOAD
		Ok = parser.GetValue(_T("LOG_LEVEL"), (int*)&LogLevel);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("CHAT_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET LOG_LEVEL FAILED : [%d]"), LogLevel);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("CHAT_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("LOG_LEVEL : [%d]"), LogLevel);
		CLogger::GetInstance()->SetDefaultLogLevel((LEVEL)LogLevel);

		CLogger::GetInstance()->Log(_T("CHAT_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("========== LAN_CLIENT_CHAT OPEN SUCCESSFUL ========== "));

		return true;
	}

}
