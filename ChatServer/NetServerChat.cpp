#include "NetServerChat.h"
#include "LanClientChat.h"
#include "LanClientMonitor.h"
#include "CommonProtocol\CommonProtocol.h"
#include "Define\GGM_ERROR.h"
#include "Logger\Logger.h"
#include <tchar.h>

using namespace std;

namespace GGM
{
	GGM::CNetServerChat::CNetServerChat(
		NetServerChatConfig *pNetConfig, LanClientChatConfig *pLanChatConfig, LanClientMonitorConfig *pLanMonitorConfig
	)
	{				
		// 플레이어 배열 할당
		m_PlayerArr = new Player[pNetConfig->MaxSessions];
		
		if (m_PlayerArr == nullptr)
		{
			OnError(GetLastError(), _T("m_PlayerArr MemAlloc failed %d"));			
		}

		// 섹터내부 자료구조 할당
		for (int i = 0; i < SECTOR_HEIGHT; i++)
		{
			for (int j = 0; j < SECTOR_WIDTH; j++)
			{
				m_GameMap[i][j].SectorArr = new Player*[pNetConfig->MaxSessions];

				if (m_PlayerArr == nullptr)
				{
					OnError(GetLastError(), _T("SectorArr MemAlloc failed %d"));					
				}
			}
		}

		// 일감 메모리 풀 할당
		m_pWorkPool = new CTlsMemoryPool<CHAT_WORK>(0);

		if (m_pWorkPool == nullptr)
		{
			OnError(GetLastError(), _T("m_pWorkPool MemAlloc failed %d"));			
		}

		// 토큰 메모리 풀 할당		
		m_pTokenPool = new CTlsMemoryPool<Token>(0);

		if (m_pTokenPool == nullptr)
		{
			OnError(GetLastError(), _T("m_pTokenPool MemAlloc failed %d"));			
		}

		// 토큰 락 초기화
		InitializeSRWLock(&m_lock);

		// 일감 락프리큐 초기화
		m_WorkQ.InitLockFreeQueue(0, MAX_WORKLOAD);

		// NetServer와 통신할 이벤트 객체 생성	
		// 자동 리셋 모드 이벤트 생성
		m_WorkEvent = CreateEvent(nullptr, false, false, nullptr);

		if (m_WorkEvent == NULL)
			OnError(GetLastError(), _T("CreateEvent %d"));

		// 일감 프로시저 포인터 배열 초기화
		m_ChatWorkProc[CHAT_WORK_TYPE::PACKET] = &CNetServerChat::OnRecvProc;
		m_ChatWorkProc[CHAT_WORK_TYPE::JOIN] = &CNetServerChat::OnJoinProc;
		m_ChatWorkProc[CHAT_WORK_TYPE::LEAVE] = &CNetServerChat::OnLeaveProc;

		// 패킷 프로시저 포인터 배열 초기화
		m_PacketProc[en_PACKET_CS_CHAT_REQ_LOGIN] = &CNetServerChat::ReqLoginProc;
		m_PacketProc[en_PACKET_CS_CHAT_REQ_SECTOR_MOVE] = &CNetServerChat::ReqSectorMoveProc;
		m_PacketProc[en_PACKET_CS_CHAT_REQ_MESSAGE] = &CNetServerChat::ReqMsgProc;
		m_PacketProc[en_PACKET_CS_CHAT_REQ_HEARTBEAT] = &CNetServerChat::ReqHeartbeatProc;

		// 섹터 초기화
		InitSector();

		// 업데이트 스레드 생성
		m_hUpdateThread = (HANDLE)_beginthreadex(nullptr, 0, UpdateThread, (LPVOID)this, 0, nullptr);

		if (m_hUpdateThread == NULL)
		{
			OnError(GetLastError(), _T("_beginthreadex failed [GetLastError : %d]"));		
		}	

		// 토큰 정리 스레드 생성
		m_hTokenCollector = (HANDLE)_beginthreadex(nullptr, 0, TokenThread, (LPVOID)this, 0, nullptr);

		if (m_hTokenCollector == NULL)
		{
			OnError(GetLastError(), _T("_beginthreadex failed [GetLastError : %d]"));
		}

		bool Ok = Start(pNetConfig->BindIP,
			pNetConfig->Port,
			pNetConfig->ConcurrentThreads,
			pNetConfig->MaxThreads,
			pNetConfig->IsNoDelay,
			pNetConfig->MaxSessions,
			pNetConfig->PacketCode,
			pNetConfig->PacketKey1,
			pNetConfig->PacketKey2
		);

		if (Ok == false)
			OnError(GGM_ERROR::STARTUP_FAILED, _T("Server Start Failed [ErrorCode : %d]"));

		// CLanClientChat 생성
		m_pLanChat = new CLanClientChat(pLanChatConfig, this, &m_TokenTable, &m_lock, m_pTokenPool);

		if (m_pLanChat == nullptr)
		{
			OnError(GetLastError(), _T("Mem Alloc Failed [GetLastError : %d]"));
		}

		// CLanClientMonitor 생성
		m_pLanMonitor = new CLanClientMonitor(pLanMonitorConfig, this);

		if (m_pLanMonitor == nullptr)
		{
			OnError(GetLastError(), _T("Mem Alloc Failed [GetLastError : %d]"));
		}
	}

	GGM::CNetServerChat::~CNetServerChat()
	{
		// 채팅 서버 종료 절차
		StopChatServer();

		// 리소스 정리
		delete m_PlayerArr;

		for (int i = 0; i < SECTOR_HEIGHT; i++)
		{
			for (int j = 0; j < SECTOR_WIDTH; j++)
			{
				delete m_GameMap[i][j].SectorArr;
			}
		}

		// 일감 메모리 풀 해제
		delete m_pWorkPool;

		// 토큰 메모리 풀 해제
		delete m_pTokenPool;

		// 이벤트 객체 정리	
		CloseHandle(m_WorkEvent);

		// 스레드 핸들 닫기
		CloseHandle(m_hUpdateThread);
		CloseHandle(m_hTokenCollector);

		// 랜 클라이언트 정리
		delete m_pLanChat;
		delete m_pLanMonitor;	
	}

	void GGM::CNetServerChat::StopChatServer()
	{
		// 업데이트 스레드와 토큰 정리 스레드 정지
		QueueUserAPC(CNetServerChat::NetChatExitFunc, m_hUpdateThread, 0);
		QueueUserAPC(CNetServerChat::NetChatExitFunc, m_hTokenCollector, 0);

		// 스레드가 모두 정지할때까지 기다림
		HANDLE hThread[2] = { m_hUpdateThread, m_hTokenCollector };
		WaitForMultipleObjects(2, hThread, TRUE, INFINITE);		

		// 채팅 서버 구동 정지
		Stop();
	}

	void GGM::CNetServerChat::PrintInfo()
	{
		ULONGLONG AcceptTPS = InterlockedAnd(&m_AcceptTPS, 0);
		ULONGLONG UpdateTPS = InterlockedAnd(&m_UpdateTPS, 0);
		LONG64    SendTPS = InterlockedAnd64(&m_SendTPS, 0);
		ULONGLONG RecvTPS = InterlockedAnd(&m_RecvTPS, 0);

		_tprintf_s(_T("================================= CHAT SERVER =================================\n"));
		_tprintf_s(_T("[Session Count]        : %lld\n"), GetSessionCount());
		_tprintf_s(_T("[Player Count]         : %lld\n"), m_PlayerCount);		

		_tprintf_s(_T("[Packet Chunk]         : [ %lld / %lld ]\n"), CNetPacket::PacketPool->GetNumOfChunkInUse(), CNetPacket::PacketPool->GetTotalNumOfChunk());
		_tprintf_s(_T("[Packet Node Usage]    : %lld\n\n"), CNetPacket::PacketPool->GetNumOfChunkNodeInUse());

		_tprintf_s(_T("[ChatWork Chunk]       : [ %lld / %lld ]\n"), m_pWorkPool->GetNumOfChunkInUse(), m_pWorkPool->GetTotalNumOfChunk());
		_tprintf_s(_T("[ChatWork Node Usage]  : %lld\n"), m_pWorkPool->GetNumOfChunkNodeInUse());
		_tprintf_s(_T("[ChatWork Queue]       : [ %ld / %d ] \n\n"), m_WorkQ.size(), MAX_WORKLOAD);

		_tprintf_s(_T("[Token Chunk]          : [ %lld / %lld ]\n"), m_pTokenPool->GetNumOfChunkInUse(), m_pTokenPool->GetTotalNumOfChunk());
		_tprintf_s(_T("[Token Node Usage]     : %lld\n"), m_pTokenPool->GetNumOfChunkNodeInUse());

		_tprintf_s(_T("[Total Accept]         : %lld\n"), m_AcceptTotal);
		_tprintf_s(_T("[Accept TPS]           : %lld\n"), AcceptTPS);
		_tprintf_s(_T("[Update TPS]           : %lld\n\n"), UpdateTPS);

		_tprintf_s(_T("[Packet Recv TPS]      : %lld\n"), RecvTPS);
		_tprintf_s(_T("[Packet Send TPS]      : %lld\n"), SendTPS);

		_tprintf_s(_T("[SessionKey Not Found] : %lld\n"), m_SessionKeyNotFound);
		_tprintf_s(_T("[Invalid SessionKey]   : %lld\n\n"), m_InvalidSessionKey);

		_tprintf_s(_T("[LanClient Reconnect]  : %lld\n"), ReconnectCount);
		_tprintf_s(_T("================================= CHAT SERVER =================================\n"));

	}

	void GGM::CNetServerChat::OnClientJoin(const SOCKADDR_IN & ClientAddr, ULONGLONG SessionID)
	{
		// 일감 구조체 할당	
		CHAT_WORK *pWork = m_pWorkPool->Alloc();

		pWork->type = CHAT_WORK_TYPE::JOIN;
		pWork->SessionID = SessionID;

		// 일감 인큐
		m_WorkQ.Enqueue(pWork);

		// 새로운 세션이 연결되었음을 채팅서버에 알려준다.
		// 업데이트 스레드가 일할 수 있도록 이벤트 신호 준다.
		SetEvent(m_WorkEvent);
	}

	void GGM::CNetServerChat::OnClientLeave(ULONGLONG SessionID)
	{
		// 일감 구조체 할당
		CHAT_WORK *pWork = m_pWorkPool->Alloc();

		pWork->type = CHAT_WORK_TYPE::LEAVE;
		pWork->SessionID = SessionID;

		// 일감 인큐
		m_WorkQ.Enqueue(pWork);

		// 세션이 릴리즈 되었음을 채팅서버에 알려준다.
		// 업데이트 스레드가 일할 수 있도록 이벤트 신호 준다.
		SetEvent(m_WorkEvent);
	}

	bool GGM::CNetServerChat::OnConnectionRequest(const SOCKADDR_IN & ClientAddr)
	{
		// 지금은 할일이 없음
		return true;
	}

	void GGM::CNetServerChat::OnRecv(ULONGLONG SessionID, CNetPacket * pPacket)
	{
		// 일감 구조체 할당
		CHAT_WORK *pWork = m_pWorkPool->Alloc();

		pWork->type = CHAT_WORK_TYPE::PACKET;
		pWork->SessionID = SessionID;
		pWork->pPacket = pPacket;

		// 컨텐츠 계층의 업데이트 스레드가 패킷 가져다가 쓸 것이므로 참조 카운트 증가시켜준다.
		pPacket->AddRefCnt();

		// 일감 인큐
		m_WorkQ.Enqueue(pWork);

		// 처리할 패킷이 도착했음을 채팅서버에 알려준다.
		// 업데이트 스레드가 일할 수 있도록 이벤트 신호 준다.	
		SetEvent(m_WorkEvent);
	}

	void GGM::CNetServerChat::OnSend(ULONGLONG SessionID, int SendSize)
	{		
	}

	void GGM::CNetServerChat::OnWorkerThreadBegin()
	{
		// 딱히 할일이 없음
	}

	void GGM::CNetServerChat::OnWorkerThreadEnd()
	{
		// 딱히 할일이 없음
	}

	void GGM::CNetServerChat::OnError(int ErrorNo, const TCHAR * ErrorMsg)
	{
		// 디버깅용으로 에러나면 무조건 서버 크래쉬
		CLogger::GetInstance()->Log(_T("ChatServer"), LEVEL::DBG, OUTMODE::FILE, ErrorMsg, ErrorNo);
		CCrashDump::ForceCrash();		
	}

	unsigned int __stdcall GGM::CNetServerChat::UpdateThread(LPVOID Param)
	{
		// static 함수이기 때문에 멤버에 접근하기 위해 this 포인터 얻어온다.
		CNetServerChat *pThis = (CNetServerChat*)Param;

		// 일감을 얻어올 큐의 주소를 얻어온다.
		CLockFreeQueue<CHAT_WORK*> *pWorkQueue = &(pThis->m_WorkQ);

		// 일감 풀
		CTlsMemoryPool<CHAT_WORK> *pWorkPool = pThis->m_pWorkPool;

		// NetServer의 일감 신호를 기다릴 이벤트객체를 얻어온다.
		HANDLE WorkEvent = pThis->m_WorkEvent;

		// 업데이트 스레드의 로직을 결정짓는 함수포인터 배열의 주소를 얻어온다.
		CHAT_WORK_PROC *pChatWorkProc = pThis->m_ChatWorkProc;

		ULONGLONG *pUpdateTPS = &(pThis->m_UpdateTPS);

		// 큐에 일감이 들어오면 깨어나서 일을한다.
		while (true)
		{
			DWORD WaitResult = WaitForSingleObjectEx(WorkEvent, INFINITE, true);

			// 종료로직 
			if (WaitResult == WAIT_IO_COMPLETION)
				break;

			// 한번 깨어났으면 뽑을 수 있는만큼 쭉쭉 뽑아서 일처리 한다.
			while (pWorkQueue->size() > 0)
			{
				// 큐에서 일감 뽑아내기
				CHAT_WORK *pWork;

				pWorkQueue->Dequeue(&pWork);

				///////////////////////////////////////////////////////////////////////////////////////	
				// Switch~ case 문을 사용하지 않고 pWork->type을 인덱스로 사용하여 해당 프로시저 호출.		
				// PACKET == 0 -> 일반적인 컨텐츠 패킷 처리 (OnRecvProc)
				// JOIN   == 1 -> 세션 접속 처리 (OnJoinProc 호출)
				// LEAVE  == 2 -> 세션 접속 종료 처리 (OnLeaveProc)
				///////////////////////////////////////////////////////////////////////////////////////
				(pThis->*pChatWorkProc[pWork->type])(pWork);

				// 일감 FREE			
				pWorkPool->Free(pWork);

				InterlockedIncrement(pUpdateTPS);
			}
		}

		// 채팅 서버를 끄기 전에 아직 큐에 남아있는 리소스들을 Free 해준다.
		while (pWorkQueue->size() > 0)
		{
			// 큐에서 일감 뽑아내기
			CHAT_WORK *pWork;

			if (pWorkQueue->Dequeue(&pWork) == false)
				pThis->OnError(GGM_ERROR::LOCK_FREE_Q_DEQ_FAILED, _T("WorkQueue Deq Failed %d"));

			// 패킷 프리
			if (pWork->type == CHAT_WORK_TYPE::PACKET)
				CNetPacket::Free(pWork->pPacket);

			// 일감 FREE			
			pWorkPool->Free(pWork);
		}

		_tprintf_s(_T("[CHAT SERVER] Update Thread Exit [ REMAINING QUEUE SIZE ] : %ld\n"), pWorkQueue->size());

		return 0;
	}

	unsigned int __stdcall CNetServerChat::TokenThread(LPVOID Param)
	{
		// static 함수이기 때문에 멤버에 접근하기 위해 this 포인터 얻어온다.
		CNetServerChat *pThis = (CNetServerChat*)Param;

		// 토큰 테이블의 락 
		PSRWLOCK pLock = &(pThis->m_lock);

		// 토큰 테이블
		unordered_map<INT64, Token*> *pTokenTable = &(pThis->m_TokenTable);

		// 토큰 풀
		CTlsMemoryPool<Token> *pTokenPool = pThis->m_pTokenPool;			

		while (true)
		{
			// 일정시간마다 깨어나서 토큰 테이블을 순회하며 정리한다.
			DWORD ret = SleepEx(TOKEN_ERASE_PERIOD, true);	

			if (ret == WAIT_IO_COMPLETION)
				break;					

			// 토큰 테이블 순회하며 일정시간 동안 응답없는 토큰 삭제
			AcquireSRWLockExclusive(pLock);

			auto iter_cur = pTokenTable->begin();
			auto iter_end = pTokenTable->end();

			ULONGLONG CurrentTime = GetTickCount64();
			
			while (iter_cur != iter_end)
			{
				Token *pToken = iter_cur->second;
				if (CurrentTime - (pToken->InsertTime) > TOKEN_ERASE_PERIOD)
				{
					// 토큰 테이블에서 토큰 삭제
					iter_cur = pTokenTable->erase(iter_cur);

					// 토큰 풀에 토큰 반납
					pTokenPool->Free(pToken);
				}
				else
				{
					++iter_cur;
				}
			}

			ReleaseSRWLockExclusive(pLock);		
		}

		////////////////////////////////////////////////////
		// 종료 
		////////////////////////////////////////////////////

		// 루프 빠져나왔으면 스레드 종료하기전에 토큰 모두 반환 
		AcquireSRWLockExclusive(pLock);

		if (pTokenTable->size() > 0)
		{
			auto iter_cur = pTokenTable->begin();
			auto iter_end = pTokenTable->end();			

			while (iter_cur != iter_end)
			{
				// 토큰 풀에 토큰 반납
				pTokenPool->Free(iter_cur->second);

				++iter_cur;			
			}
		}

		ReleaseSRWLockExclusive(pLock);

		return 0;
	}

	bool GGM::CNetServerChat::OnJoinProc(CHAT_WORK* pWork)
	{
		// 접속 처리 로직 
		// 주목적 : 플레이어 관리 테이블에 플레이어를 추가한다.			
		ULONGLONG SessionID = pWork->SessionID;
		Player *pPlayer = &m_PlayerArr[(WORD)SessionID];

		if (pPlayer->SessionID != INIT_SESSION_ID)
		{
			OnError(GGM_ERROR::SESSION_SYNC_FAILED, _T("OnJoinProc Session Sync Failed %d"));
		}

		// 해당 세션 아이디 정보를 등록
		pPlayer->SessionID = SessionID;

		// 최초 로그인 시에는 섹터정보가 없으므로 일단 nullptr
		// 나중에 섹터 이동 요청 패킷을 받으면 섹터 정보가 갱신된다.
		// 그 이전에는 어떤 로직처리도 되어서는 안됨
		pPlayer->pSector = nullptr;

		// 로그인 처리 안된 상태로 바꿈
		pPlayer->IsLogIn = false;

		++m_PlayerCount;	

		return true;
	}

	bool GGM::CNetServerChat::OnRecvProc(CHAT_WORK* pWork)
	{
		// 패킷 타입별 프로시저가 나열된 멤버 함수 포인터 배열
		CHAT_PACKET_PROC *pPacketProc = m_PacketProc;

		// 일감에서 패킷 가져오기
		CNetPacket *pPacket = pWork->pPacket;

		// 패킷 타입 확인		
		WORD PacketType;
		pPacket->Dequeue((char*)&PacketType, sizeof(PacketType));		

		// 이상한 패킷 타입 보낸 클라이언트라면 연결 끊는다.
		if ((PacketType & 1) == 0 || PacketType > NUM_CHAT_PACKET_TYPE || PacketType < 0)
		{
			Disconnect(pWork->SessionID);

			// 해당 패킷은 NetServer에서 건네준 것을 참조한 것이므로 Free 해 주어야 한다.
			CNetPacket::Free(pPacket);
			return false;
		}

		/////////////////////////////////////////////////////////////////////
		// PacketType 변수의 내용에 따라 패킷 처리
		// PacketType == 패킷 프로시저 멤버 함수 포인터 배열의 인덱스
		// 1 ==  채팅서버 로그인 요청 (en_PACKET_CS_CHAT_REQ_LOGIN)
		// 3 ==  채팅서버 섹터 이동 요청 (en_PACKET_CS_CHAT_REQ_SECTOR_MOVE)
		// 5 ==  채팅서버 채팅 보내기 요청 (en_PACKET_CS_CHAT_REQ_MESSAGE)
		// 7 ==  하트비트 (en_PACKET_CS_CHAT_REQ_HEARTBEAT)
		/////////////////////////////////////////////////////////////////////		
		Player *pPlayer = &m_PlayerArr[(WORD)(pWork->SessionID)];

		if (pPlayer->SessionID != pWork->SessionID)
		{
			OnError(GGM_ERROR::SESSION_SYNC_FAILED, _T("OnRecvProc Session Sync Failed %d"));
			return false;
		}

		(this->*pPacketProc[PacketType])(pPacket, pPlayer);

		// 해당 패킷은 NetServer에서 건네준 것을 참조한 것이므로 Free 해 주어야 한다.
		CNetPacket::Free(pPacket);

		return true;
	}

	bool GGM::CNetServerChat::OnLeaveProc(CHAT_WORK* pWork)
	{
		// 접속 종료 처리 로직 
		// 주목적 : 플레이어 객체 프리 -> 플레이어 정보 삭제 				
		Player *pPlayerArr = m_PlayerArr;
		ULONGLONG SessionID = pWork->SessionID;

		// 플레이어 검색

		Player *pPlayer = &pPlayerArr[(WORD)SessionID];

		if (pPlayer->SessionID != SessionID)
		{
			OnError(GGM_ERROR::SESSION_SYNC_FAILED, _T("OnLeaveProc Session Sync Failed %d"));
			return false;
		}		

		if (pPlayer->pSector != nullptr)
		{
			// 플레이어가 섹터에 속해있다면 삭제해준다.
			WORD SectorIdx = pPlayer->SectorIdx;
			size_t SectorArrSize = pPlayer->pSector->SectorSize;
			Player **SectorArr = pPlayer->pSector->SectorArr;

			// 플레이어의 인덱스가 섹터배열의 마지막이 아니라면
			if (SectorIdx != (SectorArrSize - 1))
			{
				// 마지막 인덱스에 해당하는 플레이어를 삭제될 플레이어의 자리에 꼽아준다.
				SectorArr[SectorIdx] = SectorArr[SectorArrSize - 1];
				SectorArr[SectorIdx]->SectorIdx = SectorIdx;
			}

			// 배열의 Size 줄여준다.
			pPlayer->pSector->SectorSize--;
		}

		pPlayer->SessionID = INIT_SESSION_ID;

		--m_PlayerCount;	

		if (pPlayer->IsLogIn == true)
			--m_LoginPlayer;

		return true;
	}

	void GGM::CNetServerChat::ReqLoginProc(CNetPacket* pPacket, Player* pPlayer)
	{
		//------------------------------------------------------------
		// 채팅서버 로그인 요청
		//
		//	{
		//		WORD	Type
		//
		//		INT64	AccountNo
		//		WCHAR	ID[20]				// null 포함
		//		WCHAR	Nickname[20]		// null 포함
		//		char	SessionKey[64];
		//	}
		//
		//------------------------------------------------------------

		/////////////////////////////////////////////////////////////////////
		// 플레이어 정보를 갱신한다.	
		// AccountNo 마샬링		
		INT64 AccountNo;
		int Result = pPacket->Dequeue((char*)&AccountNo, sizeof(INT64));
		if (Result == GGM_PACKET_ERROR)
		{
			Disconnect(pPlayer->SessionID);
			return;
		}	

		pPlayer->AccountNo = AccountNo;		

		// ID, 닉네임 먀살링
		Result = pPacket->Dequeue((char*)pPlayer->ID, (ID_LEN + NICK_LEN) * sizeof(TCHAR));
		if (Result == GGM_PACKET_ERROR)
		{
			Disconnect(pPlayer->SessionID);
			return;
		}			

		/////////////////////////////////////////////////////////////////////////////////////////
		// 세션키 검증 

		// 세션키 마샬링
		char PayloadSessionKey[SESSIONKEY_LEN];
		Result = pPacket->Dequeue((char*)PayloadSessionKey, SESSIONKEY_LEN);
		if (Result == GGM_PACKET_ERROR)
		{
			Disconnect(pPlayer->SessionID);
			return;
		}

		bool IsLogin = true;
		BYTE LoginStatus = TRUE;
		do
		{
			AcquireSRWLockExclusive(&m_lock);

			// 토큰 테이블에 들어있는 세션키와 유저가 패킷에 실어보낸 세션키를 비교하여 유효성 검사한다.
			auto token_iter = m_TokenTable.find(AccountNo);

			// 세션키 검증하러 들어왔는데 해당 AccountNo에 해당하는 정보가 없으면 안됨
			if (token_iter == m_TokenTable.end())
			{
				m_SessionKeyNotFound++; // 모니터링용 카운터				
				IsLogin = false;
				LoginStatus = FALSE;
				break;
			}

			// 토큰 테이블에 있는 세션키 얻어와서 클라가 들고온 세션키와 비교
			Token *pToken = token_iter->second;
			char* TableSessionKey = pToken->SessionKey;			

			////////////////////////////////////////////////////////////////////////////////
			// 세션키 비교 
			// memcmp의 오버헤드를 피하기 위해 8바이트씩 메모리 직접비교
			if (
				   *((INT64*)(PayloadSessionKey))      != *((INT64*)(TableSessionKey))
				|| *((INT64*)(PayloadSessionKey + 8))  != *((INT64*)(TableSessionKey + 8))
				|| *((INT64*)(PayloadSessionKey + 16)) != *((INT64*)(TableSessionKey + 16))
				|| *((INT64*)(PayloadSessionKey + 24)) != *((INT64*)(TableSessionKey + 24))
				|| *((INT64*)(PayloadSessionKey + 32)) != *((INT64*)(TableSessionKey + 32))
				|| *((INT64*)(PayloadSessionKey + 40)) != *((INT64*)(TableSessionKey + 40))
				|| *((INT64*)(PayloadSessionKey + 48)) != *((INT64*)(TableSessionKey + 48))
				|| *((INT64*)(PayloadSessionKey + 56)) != *((INT64*)(TableSessionKey + 56))
			)
			{
				// 세션 키가 다르다면 로그인 실패
				m_InvalidSessionKey++; // 모니터링용 카운터				
				IsLogin = false;
				LoginStatus = FALSE;
				break;
			}
			////////////////////////////////////////////////////////////////////////////////		

		} while (0);

		ReleaseSRWLockExclusive(&m_lock);
		/////////////////////////////////////////////////////////////////////////////////////////

		// 해당 플레이어의 로그인 상태 업데이트
		pPlayer->IsLogIn = IsLogin;

		// 요청에 대한 응답 패킷 송신	
		ResLoginProc(pPlayer->SessionID, LoginStatus, AccountNo);	

		// 로그인 성공한 유저 수 증가
		if (IsLogin == true)
			m_LoginPlayer++;
	}

	void GGM::CNetServerChat::ReqSectorMoveProc(CNetPacket* pPacket, Player* pPlayer)
	{
		//------------------------------------------------------------
		// 채팅서버 섹터 이동 요청
		//
		//	{
		//		WORD	Type
		//
		//		INT64	AccountNo
		//		WORD	SectorX
		//		WORD	SectorY
		//	}
		//
		//------------------------------------------------------------			
		
		// 로그인 상태가 아닌데 요청 패킷 보냈으면 연결 끊는다.	
		if (pPlayer->IsLogIn == false)
		{
			Disconnect(pPlayer->SessionID);
			return;
		}

		// AccountNo 마샬링		
		INT64 AccountNo;
		int Result = pPacket->Dequeue((char*)&AccountNo, sizeof(INT64));
		if (Result == GGM_PACKET_ERROR)
		{
			Disconnect(pPlayer->SessionID);
			return;
		}

		// 만약 전달된 AccountNo가 플레이어의 현재 AccountNo와 다르다면 연결 종료
		if (AccountNo != pPlayer->AccountNo)
		{
			Disconnect(pPlayer->SessionID);
			return;
		}

		// 이동한 섹터의 정보를 얻어온다.
		// [0] == X
		// [1] == Y
		WORD SectorCoord[2];
		Result = pPacket->Dequeue((char*)SectorCoord, sizeof(SectorCoord));
		if (Result == GGM_PACKET_ERROR)
		{
			Disconnect(pPlayer->SessionID);
			return;
		}

		// 얻어온 섹터의 좌표가 유효한 수치인지 경계검사 한다.
		if (SectorCoord[0] < 0
			|| SectorCoord[0] > SECTOR_WIDTH - 1
			|| SectorCoord[1] < 0
			|| SectorCoord[1] > SECTOR_HEIGHT - 1)
		{
			// 클라이언트가 이상한 좌표 보냈으면 연결 끊는다.		
			Disconnect(pPlayer->SessionID);
			return;
		}

		// 로그인 한 이후 어느 섹터에도 속해있지 않다면 섹터값은 nullptr이다.
		// 섹터값이 초기값이라면 현재 플레이어가 어느 섹터에도 속해있지 않음을 의미한다.
		// 초기값이 아닌 유효한 섹터에 속해있는 상태라면 이전 섹터에서 해당 플레이어를 제거한다.			
		if (pPlayer->pSector != nullptr)
		{
			// 플레이어가 섹터에 속해있다면 삭제해준다.
			short SectorIdx = pPlayer->SectorIdx;
			size_t SectorArrSize = pPlayer->pSector->SectorSize;
			Player **SectorArr = pPlayer->pSector->SectorArr;

			// 플레이어의 인덱스가 섹터배열의 마지막이 아니라면
			if (SectorIdx != (SectorArrSize - 1))
			{
				// 마지막 인덱스에 해당하는 플레이어를 삭제될 플레이어의 자리에 꼽아준다.
				SectorArr[SectorIdx] = SectorArr[SectorArrSize - 1];
				SectorArr[SectorIdx]->SectorIdx = SectorIdx;
			}

			// 배열의 Size 줄여준다.
			pPlayer->pSector->SectorSize--;
		}

		// 클라가 요청한 섹터 이동 좌표가 유효하다면 이동시켜준다.		
		Sector *pSector = &m_GameMap[SectorCoord[1]][SectorCoord[0]];
		size_t  SectorLastIdx = pSector->SectorSize;
		pSector->SectorArr[SectorLastIdx] = pPlayer;
		
		// 플레이어 섹터 정보 갱신	
		pPlayer->pSector = pSector;
		pPlayer->SectorIdx = (WORD)SectorLastIdx;
		pPlayer->pSector->SectorSize++;

		// 섹터 이동 결과 송신
		ResSectorMoveProc(pPlayer->SessionID, AccountNo, SectorCoord[0], SectorCoord[1]);
	}

	void GGM::CNetServerChat::ReqMsgProc(CNetPacket* pPacket, Player* pPlayer)
	{
		//------------------------------------------------------------
		// 채팅서버 채팅보내기 요청
		//
		//	{
		//		WORD	Type
		//
		//		INT64	AccountNo
		//		WORD	MessageLen
		//		WCHAR	Message[MessageLen / 2]		// null 미포함
		//	}
		//
		//------------------------------------------------------------	

		// 로그인 상태가 아니거나 현재 어느 섹터에도 존재하지 않는 플레이어라면 채팅 패킷처리 불가능
		if (pPlayer->IsLogIn == false || pPlayer->pSector == nullptr)
		{
			Disconnect(pPlayer->SessionID);
			return;
		}

		// AccountNo 마샬링		
		INT64 AccountNo;
		int Result = pPacket->Dequeue((char*)&AccountNo, sizeof(INT64));
		if (Result == GGM_PACKET_ERROR)
		{
			Disconnect(pPlayer->SessionID);
			return;
		}

		// 만약 전달된 AccountNo가 플레이어의 현재 AccountNo와 다르다면 연결 종료
		if (AccountNo != pPlayer->AccountNo)
		{
			Disconnect(pPlayer->SessionID);
			return;
		}

		// MessageLen 마샬링
		WORD MessageLen;
		Result = pPacket->Dequeue((char*)&MessageLen, sizeof(WORD));
		if (Result == GGM_PACKET_ERROR)
		{
			Disconnect(pPlayer->SessionID);
			return;
		}

		// 메시지 마샬링
		char ChatMsg[CHAT_MSG_LEN];
		Result = pPacket->Dequeue(ChatMsg, MessageLen);
		if (Result == GGM_PACKET_ERROR)
		{
			Disconnect(pPlayer->SessionID);
			return;
		}
	
		// 메세지 응답
		ResMsgProc(pPlayer->SessionID, AccountNo, pPlayer, MessageLen, (TCHAR*)ChatMsg);
	}

	void GGM::CNetServerChat::ReqHeartbeatProc(CNetPacket* pPacket, Player* pPlayer)
	{
		// 일단 지금은 하트비트 패킷 처리하지 않음	
	}

	void GGM::CNetServerChat::ResLoginProc(ULONGLONG SessionID, BYTE Result, INT64 AccountNo)
	{
		//------------------------------------------------------------
		// 채팅서버 로그인 응답
		//
		//	{
		//		WORD	Type
		//
		//		BYTE	Status				// 0:실패	1:성공
		//		INT64	AccountNo
		//	}
		//
		//------------------------------------------------------------

		// 응답 보낼 패킷 하나 할당 받는다.
		CNetPacket *pPacket = CNetPacket::Alloc();

		// 패킷 생성
		CreatePacketResLogin(pPacket, Result, AccountNo);

		// 패킷 송신
		// 로그인 결과가 성공이면 패킷 송신만, 실패라면 패킷 송신 후 연결 끊기
		if (Result == TRUE)
			SendPacket(SessionID, pPacket);
		else
			SendPacketAndDisconnect(SessionID, pPacket);

		// Alloc 할때 참조카운트 늘리므로 그에 대한 Free이다.
		CNetPacket::Free(pPacket);
	}

	void GGM::CNetServerChat::ResSectorMoveProc(ULONGLONG SessionID, INT64 AccountNo, WORD SectorX, WORD SectorY)
	{
		// 응답 보낼 패킷 하나 할당 받는다.
		CNetPacket *pPacket = CNetPacket::Alloc();

		// 패킷 생성
		CreatePacketResSectorMove(pPacket, AccountNo, SectorX, SectorY);

		// 패킷 송신
		SendPacket(SessionID, pPacket);

		// Alloc 할때 참조카운트 늘리므로 그에 대한 Free이다.
		CNetPacket::Free(pPacket);
	}

	void GGM::CNetServerChat::ResMsgProc(ULONGLONG SessionID, INT64 AccountNo, Player * pPlayer, WORD MsgLen, TCHAR * Msg)
	{
		// 응답 보낼 패킷 하나 할당 받는다.
		CNetPacket *pPacket = CNetPacket::Alloc();

		// 패킷 생성
		CreatePacketResMsg(pPacket, AccountNo, pPlayer->ID, pPlayer->Nickname, MsgLen, Msg);

		// 해당 채팅 메시지를 보낸 플레이어의 섹터를 포함한 주변 9개 섹터에 위치한 플레이어들에게 채팅 메시지를 모두 보내준다.
		// 에코메시지의 개념으로 채팅 메시지를 서버에게 보낸 세션에게도 다시 보내 준다.	

		// 기준이 되는 플레이어의 주변 9개 섹터 중 유효한 섹터의 수 
		int ValidSectorCount = pPlayer->pSector->ValidCount;

		// 기준이 되는 플레이어의 주변 9개 섹터 중 유효한 섹터의 포인터 배열
		Sector **pVaildSector = pPlayer->pSector->ValidSector;

		for (int i = 0; i < ValidSectorCount; i++)
		{
			Player  **pSectorArr = pVaildSector[i]->SectorArr;
			size_t  SectorSize = pVaildSector[i]->SectorSize;						

			for (size_t i = 0; i < SectorSize; i++)
			{
				SendPacket(pSectorArr[i]->SessionID, pPacket);				
			}
		}

		// Alloc에 대한 Free
		CNetPacket::Free(pPacket);
	}

	void GGM::CNetServerChat::CreatePacketResLogin(CNetPacket * pPacket, BYTE Result, INT64 AccountNo)
	{
		//------------------------------------------------------------
		// 채팅서버 로그인 응답
		//
		//	{
		//		WORD	Type
		//
		//		BYTE	Status				// 0:실패	1:성공
		//		INT64	AccountNo
		//	}
		//
		//------------------------------------------------------------

		// 송신할 패킷을 생성한다.
		WORD PacketType = en_PACKET_CS_CHAT_RES_LOGIN;
		pPacket->Enqueue((char*)&PacketType, sizeof(WORD));
		pPacket->Enqueue((char*)&Result, sizeof(BYTE));
		pPacket->Enqueue((char*)&AccountNo, sizeof(INT64));
	}

	void GGM::CNetServerChat::CreatePacketResSectorMove(CNetPacket * pPacket, INT64 AccountNo, WORD SectorX, WORD SectorY)
	{
		//------------------------------------------------------------
		// 채팅서버 섹터 이동 결과
		//
		//	{
		//		WORD	Type
		//
		//		INT64	AccountNo
		//		WORD	SectorX
		//		WORD	SectorY
		//	}
		//
		//------------------------------------------------------------
		
		// 송신할 패킷을 생성한다.
		WORD PacketType = en_PACKET_CS_CHAT_RES_SECTOR_MOVE;
		pPacket->Enqueue((char*)&PacketType, sizeof(WORD));
		pPacket->Enqueue((char*)&AccountNo, sizeof(INT64));
		pPacket->Enqueue((char*)&SectorX, sizeof(WORD));
		pPacket->Enqueue((char*)&SectorY, sizeof(WORD));
	}

	void GGM::CNetServerChat::CreatePacketResMsg(CNetPacket *pPacket, INT64 AccountNo, TCHAR * ID, TCHAR * NickName, WORD MsgLen, TCHAR * Msg)
	{
		//------------------------------------------------------------
		// 채팅서버 채팅보내기 응답  (다른 클라가 보낸 채팅도 이걸로 받음)
		//
		//	{
		//		WORD	Type
		//
		//		INT64	AccountNo
		//		WCHAR	ID[20]						// null 포함
		//		WCHAR	Nickname[20]				// null 포함
		//		
		//		WORD	MessageLen
		//		WCHAR	Message[MessageLen / 2]		// null 미포함
		//	}
		//
		//------------------------------------------------------------	

		// 송신할 패킷을 생성한다.
		WORD PacketType = en_PACKET_CS_CHAT_RES_MESSAGE;
		pPacket->Enqueue((char*)&PacketType, sizeof(WORD));
		pPacket->Enqueue((char*)&AccountNo, sizeof(INT64));
		pPacket->Enqueue((char*)ID, ID_LEN * sizeof(TCHAR));
		pPacket->Enqueue((char*)NickName, NICK_LEN * sizeof(TCHAR));
		pPacket->Enqueue((char*)&MsgLen, sizeof(WORD));
		pPacket->Enqueue((char*)Msg, MsgLen);
	}

	void GGM::CNetServerChat::InitSector()
	{
		// 섹터 초기화	
		for (int iY = 0; iY < SECTOR_HEIGHT; iY++)
		{
			for (int iX = 0; iX < SECTOR_WIDTH; iX++)
			{
				// 하나의 섹터에 일단 천명분의 메모리 공간 예약해 둠
				//m_GameMap[iY][iX].SectorTable.reserve(100);				

				// 섹터의 멤버로 자기 좌표를 들고 있는다.
				m_GameMap[iY][iX].SectorX = iX;
				m_GameMap[iY][iX].SectorY = iY;

				m_GameMap[iY][iX].ValidSector[m_GameMap[iY][iX].ValidCount] = &m_GameMap[iY][iX];
				m_GameMap[iY][iX].ValidCount++;

				// 주변 섹터가 없으면 nullptr 있으면 주소값 대입		
				if (iY > 0)
				{
					// 유효한 섹터의 포인터를 배열에 따로 보관하고 카운터를 센다.
					m_GameMap[iY][iX].ValidSector[m_GameMap[iY][iX].ValidCount] = &m_GameMap[iY - 1][iX];
					m_GameMap[iY][iX].ValidCount++;
				}

				if (iY < SECTOR_HEIGHT - 1)
				{
					// 유효한 섹터의 포인터를 배열에 따로 보관하고 카운터를 센다.
					m_GameMap[iY][iX].ValidSector[m_GameMap[iY][iX].ValidCount] = &m_GameMap[iY + 1][iX];
					m_GameMap[iY][iX].ValidCount++;
				}

				if (iY > 0 && iX > 0)
				{
					// 유효한 섹터의 포인터를 배열에 따로 보관하고 카운터를 센다.
					m_GameMap[iY][iX].ValidSector[m_GameMap[iY][iX].ValidCount] = &m_GameMap[iY - 1][iX - 1];
					m_GameMap[iY][iX].ValidCount++;
				}

				if (iY > 0 && iX < SECTOR_WIDTH - 1)
				{
					// 유효한 섹터의 포인터를 배열에 따로 보관하고 카운터를 센다.
					m_GameMap[iY][iX].ValidSector[m_GameMap[iY][iX].ValidCount] = &m_GameMap[iY - 1][iX + 1];
					m_GameMap[iY][iX].ValidCount++;
				}

				if (iX > 0)
				{
					// 유효한 섹터의 포인터를 배열에 따로 보관하고 카운터를 센다.
					m_GameMap[iY][iX].ValidSector[m_GameMap[iY][iX].ValidCount] = &m_GameMap[iY][iX - 1];
					m_GameMap[iY][iX].ValidCount++;
				}

				if (iX < SECTOR_WIDTH - 1)
				{
					// 유효한 섹터의 포인터를 배열에 따로 보관하고 카운터를 센다.
					m_GameMap[iY][iX].ValidSector[m_GameMap[iY][iX].ValidCount] = &m_GameMap[iY][iX + 1];
					m_GameMap[iY][iX].ValidCount++;
				}

				if (iX > 0 && iY < SECTOR_HEIGHT - 1)
				{
					// 유효한 섹터의 포인터를 배열에 따로 보관하고 카운터를 센다.
					m_GameMap[iY][iX].ValidSector[m_GameMap[iY][iX].ValidCount] = &m_GameMap[iY + 1][iX - 1];
					m_GameMap[iY][iX].ValidCount++;
				}

				if (iX < SECTOR_WIDTH - 1 && iY < SECTOR_HEIGHT - 1)
				{
					// 유효한 섹터의 포인터를 배열에 따로 보관하고 카운터를 센다.
					m_GameMap[iY][iX].ValidSector[m_GameMap[iY][iX].ValidCount] = &m_GameMap[iY + 1][iX + 1];
					m_GameMap[iY][iX].ValidCount++;
				}

			}
		}
	}

	void __stdcall GGM::CNetServerChat::NetChatExitFunc(ULONG_PTR Param)
	{
		// 아무것도 안함 종료시켜주기 위한 용도
		_tprintf_s(_T("NetChatExitFunc\n"));
	}

	size_t CNetServerChat::GetLoginPlayer() const
	{
		return m_LoginPlayer;
	}

	bool GGM::NetServerChatConfig::LoadConfig(const TCHAR * ConfigFileName)
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

		CLogger::GetInstance()->Log(_T("CHAT_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("========== NET_SERVER_CHAT OPEN START ========== "));

		parser.SetSpace(_T("#NET_SERVER_CHAT"));

		// BIND_IP LOAD
		Ok = parser.GetValue(_T("BIND_IP"), BindIP);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("CHAT_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET BIND_IP FAILED : [%s]"), BindIP);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("CHAT_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("BIND_IP : [%s]"), BindIP);

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

		// MaxSessions LOAD
		Ok = parser.GetValue(_T("MAX_SESSION"), (short*)&MaxSessions);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("CHAT_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET MAX_SESSION FAILED : [%d]"), MaxSessions);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("CHAT_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("MAX_SESSION : [%d]"), MaxSessions);

		// PacketCode LOAD
		Ok = parser.GetValue(_T("PACKET_CODE"), (char*)&PacketCode);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("CHAT_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET PACKET_CODE FAILED : [%d]"), PacketCode);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("CHAT_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("PACKET_CODE : [%d]"), PacketCode);

		// PacketKey1 LOAD
		Ok = parser.GetValue(_T("PACKET_KEY1"), (char*)&PacketKey1);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("CHAT_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET PACKET_KEY1 FAILED : [%d]"), PacketKey1);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("CHAT_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("PACKET_KEY1 : [%d]"), PacketKey1);

		// PacketKey2 LOAD
		Ok = parser.GetValue(_T("PACKET_KEY2"), (char*)&PacketKey2);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("CHAT_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET PACKET_KEY2  FAILED : [%d]"), PacketKey2);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("CHAT_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("PACKET_KEY2 : [%d]"), PacketKey2);

		// LogLevel LOAD
		Ok = parser.GetValue(_T("LOG_LEVEL"), (int*)&LogLevel);

		if (Ok == false)
		{
			CLogger::GetInstance()->Log(_T("CHAT_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("GET LOG_LEVEL FAILED : [%d]"), LogLevel);
			return 0;
		}

		CLogger::GetInstance()->Log(_T("CHAT_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("LOG_LEVEL : [%d]"), LogLevel);
		CLogger::GetInstance()->SetDefaultLogLevel((LEVEL)LogLevel);

		CLogger::GetInstance()->Log(_T("CHAT_SERVER_INFO"), LEVEL::DBG, OUTMODE::BOTH, _T("========== NET_SERVER_CHAT OPEN SUCCESSFUL ========== "));

		return true;
	}
}