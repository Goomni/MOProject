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
		// 클라이언트 구동
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
		// 다른 서버가 로그인 서버로 로그인.
		// 이는 응답이 없으며, 그냥 로그인 됨.  
		//
		//	{
		//		WORD	Type
		//
		//		BYTE	ServerType			// dfSERVER_TYPE_GAME / dfSERVER_TYPE_CHAT
		//
		//		WCHAR	ServerName[32]		// 해당 서버의 이름.  
		//	}
		//
		//------------------------------------------------------------

		// LanServer와 연결이 되었으니 연결 요청 패킷 보낸다.
		CSerialBuffer *pPacket = CSerialBuffer::Alloc();		

		// 서버간 연결 패킷 타입
		WORD PacketType = en_PACKET_SS_LOGINSERVER_LOGIN;

		// 서버의 타입
		BYTE ServerType = dfSERVER_TYPE_CHAT;

		pPacket->Enqueue((char*)&PacketType, sizeof(WORD));
		pPacket->Enqueue((char*)&ServerType, sizeof(BYTE));

		pPacket->RelocateWrite(64);

		SendPacket(pPacket);

		// Alloc 에 대한 Free
		CSerialBuffer::Free(pPacket);
	}

	void GGM::CLanClientChat::OnDisconnect()
	{
		
	}

	void GGM::CLanClientChat::OnRecv(CSerialBuffer * pPacket)
	{
		//------------------------------------------------------------
		// 로그인서버에서 게임.채팅 서버로 새로운 클라이언트 접속을 알림.
		//
		// 마지막의 Parameter 는 세션키 공유에 대한 고유값 확인을 위한 어떤 값. 이는 응답 결과에서 다시 받게 됨.
		// 채팅서버와 게임서버는 Parameter 에 대한 처리는 필요 없으며 그대로 Res 로 돌려줘야 합니다.
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

		// 패킷에서 필요한 내용 마샬링		
		INT64 AccountNo;
		pPacket->Dequeue((char*)&AccountNo, sizeof(INT64));

		// 토큰 테이블에 토큰 저장
		// CLanClientChat의 워커스레드와 CNetServerChat의 업데이트 스레드가 동시접근
		// 락 걸고 해당 내용 삽입				
		AcquireSRWLockExclusive(m_pLock);		
		
		// 토큰 구조체 포인터
		Token *pToken = nullptr;
		
		// 일단 해당 AccountNo에 해당하는 토큰이 삽입된 적이 있는지 찾아본다.
		// 어떤 유저가 토큰만 넣어놓고 연결을 끊어서 미처 로그인 처리가 안된 채로 다시 접속했을 수도 있다.
		auto iter_find = m_pTokenTable->find(AccountNo);

		// 없으면 
		if (iter_find == m_pTokenTable->end())
		{
			// 토큰 하나 Alloc()
			// 이 경우에는 새로운 토큰 구조체에 세션키 복사
			pToken = m_pTokenPool->Alloc();			
			
			// 일단 테이블에 삽입
			auto pair = m_pTokenTable->insert({ AccountNo, pToken });
			
			// 여기서도 실패하면 크래쉬
			if (pair.second == false)
				CCrashDump::ForceCrash();
		}
		else
		{
			// 이미 해당 AccountNo에 해당하는 토큰이 삽입되어 있으면 있는것 받아옴
			// 이 경우에는 이미 있는 토큰 구조체에 새로운 세션키 덮어씀
			pToken = iter_find->second;			
		}		
		
		// 토큰에 세션키 복사
		pPacket->Dequeue(pToken->SessionKey, 64);		

		// 오랫동안 채팅서버로 로그인 요청하지 않는 유저에 대해서 정리하기 위해 시간을 기록
		pToken->InsertTime = GetTickCount64();
	
		ReleaseSRWLockExclusive(m_pLock);

		// Login 서버에게 인증토큰을 정상적으로 메모리에 저장했음을 송신해준다.

		// 토큰 패킷의 고유키 얻음
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
		// 게임.채팅 서버가 새로운 클라이언트 접속패킷 수신결과를 돌려줌.
		// 게임서버용, 채팅서버용 패킷의 구분은 없으며, 로그인서버에 타 서버가 접속 시 CHAT,GAME 서버를 구분하므로 
		// 이를 사용해서 알아서 구분 하도록 함.
		//
		// 플레이어의 실제 로그인 완료는 이 패킷을 Chat,Game 양쪽에서 다 받았을 시점임.
		//
		// 마지막 값 Parameter 는 이번 세션키 공유에 대해 구분할 수 있는 특정 값
		// ClientID 를 쓰던, 고유 카운팅을 쓰던 상관 없음.
		//
		// 로그인서버에 접속과 재접속을 반복하는 경우 이전에 공유응답이 새로 접속한 뒤의 응답으로
		// 오해하여 다른 세션키를 들고 가는 문제가 생김.
		//
		//	{
		//		WORD	Type
		//
		//		INT64	AccountNo
		//		INT64	Parameter
		//	}
		//
		//------------------------------------------------------------

		// 패킷 하나 할당받음
		CSerialBuffer *pPacket = CSerialBuffer::Alloc();

		//////////////////////////////////////////////////////////////////
		// 패킷 생성
		WORD PacketType = en_PACKET_SS_RES_NEW_CLIENT_LOGIN;
		pPacket->Enqueue((char*)&PacketType, sizeof(WORD));
		pPacket->Enqueue((char*)&AccountNo, sizeof(INT64));
		pPacket->Enqueue((char*)&Param, sizeof(INT64));	
		//////////////////////////////////////////////////////////////////

		// 패킷 송신
		SendPacket(pPacket);

		// Alloc 에 대한 프리
		CSerialBuffer::Free(pPacket);
	}

	bool LanClientChatConfig::LoadConfig(const TCHAR * ConfigFileName)
	{
		CParser parser;

		// 서버 설정 파일 로드
		bool Ok = parser.LoadFile(ConfigFileName);
		DWORD err = GetLastError();

		// 서버 구동시 모든 정보는 로그로 남긴다.
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
