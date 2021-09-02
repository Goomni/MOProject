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
		// �÷��̾� �迭 �Ҵ�
		m_PlayerArr = new Player[pNetConfig->MaxSessions];
		
		if (m_PlayerArr == nullptr)
		{
			OnError(GetLastError(), _T("m_PlayerArr MemAlloc failed %d"));			
		}

		// ���ͳ��� �ڷᱸ�� �Ҵ�
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

		// �ϰ� �޸� Ǯ �Ҵ�
		m_pWorkPool = new CTlsMemoryPool<CHAT_WORK>(0);

		if (m_pWorkPool == nullptr)
		{
			OnError(GetLastError(), _T("m_pWorkPool MemAlloc failed %d"));			
		}

		// ��ū �޸� Ǯ �Ҵ�		
		m_pTokenPool = new CTlsMemoryPool<Token>(0);

		if (m_pTokenPool == nullptr)
		{
			OnError(GetLastError(), _T("m_pTokenPool MemAlloc failed %d"));			
		}

		// ��ū �� �ʱ�ȭ
		InitializeSRWLock(&m_lock);

		// �ϰ� ������ť �ʱ�ȭ
		m_WorkQ.InitLockFreeQueue(0, MAX_WORKLOAD);

		// NetServer�� ����� �̺�Ʈ ��ü ����	
		// �ڵ� ���� ��� �̺�Ʈ ����
		m_WorkEvent = CreateEvent(nullptr, false, false, nullptr);

		if (m_WorkEvent == NULL)
			OnError(GetLastError(), _T("CreateEvent %d"));

		// �ϰ� ���ν��� ������ �迭 �ʱ�ȭ
		m_ChatWorkProc[CHAT_WORK_TYPE::PACKET] = &CNetServerChat::OnRecvProc;
		m_ChatWorkProc[CHAT_WORK_TYPE::JOIN] = &CNetServerChat::OnJoinProc;
		m_ChatWorkProc[CHAT_WORK_TYPE::LEAVE] = &CNetServerChat::OnLeaveProc;

		// ��Ŷ ���ν��� ������ �迭 �ʱ�ȭ
		m_PacketProc[en_PACKET_CS_CHAT_REQ_LOGIN] = &CNetServerChat::ReqLoginProc;
		m_PacketProc[en_PACKET_CS_CHAT_REQ_SECTOR_MOVE] = &CNetServerChat::ReqSectorMoveProc;
		m_PacketProc[en_PACKET_CS_CHAT_REQ_MESSAGE] = &CNetServerChat::ReqMsgProc;
		m_PacketProc[en_PACKET_CS_CHAT_REQ_HEARTBEAT] = &CNetServerChat::ReqHeartbeatProc;

		// ���� �ʱ�ȭ
		InitSector();

		// ������Ʈ ������ ����
		m_hUpdateThread = (HANDLE)_beginthreadex(nullptr, 0, UpdateThread, (LPVOID)this, 0, nullptr);

		if (m_hUpdateThread == NULL)
		{
			OnError(GetLastError(), _T("_beginthreadex failed [GetLastError : %d]"));		
		}	

		// ��ū ���� ������ ����
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

		// CLanClientChat ����
		m_pLanChat = new CLanClientChat(pLanChatConfig, this, &m_TokenTable, &m_lock, m_pTokenPool);

		if (m_pLanChat == nullptr)
		{
			OnError(GetLastError(), _T("Mem Alloc Failed [GetLastError : %d]"));
		}

		// CLanClientMonitor ����
		m_pLanMonitor = new CLanClientMonitor(pLanMonitorConfig, this);

		if (m_pLanMonitor == nullptr)
		{
			OnError(GetLastError(), _T("Mem Alloc Failed [GetLastError : %d]"));
		}
	}

	GGM::CNetServerChat::~CNetServerChat()
	{
		// ä�� ���� ���� ����
		StopChatServer();

		// ���ҽ� ����
		delete m_PlayerArr;

		for (int i = 0; i < SECTOR_HEIGHT; i++)
		{
			for (int j = 0; j < SECTOR_WIDTH; j++)
			{
				delete m_GameMap[i][j].SectorArr;
			}
		}

		// �ϰ� �޸� Ǯ ����
		delete m_pWorkPool;

		// ��ū �޸� Ǯ ����
		delete m_pTokenPool;

		// �̺�Ʈ ��ü ����	
		CloseHandle(m_WorkEvent);

		// ������ �ڵ� �ݱ�
		CloseHandle(m_hUpdateThread);
		CloseHandle(m_hTokenCollector);

		// �� Ŭ���̾�Ʈ ����
		delete m_pLanChat;
		delete m_pLanMonitor;	
	}

	void GGM::CNetServerChat::StopChatServer()
	{
		// ������Ʈ ������� ��ū ���� ������ ����
		QueueUserAPC(CNetServerChat::NetChatExitFunc, m_hUpdateThread, 0);
		QueueUserAPC(CNetServerChat::NetChatExitFunc, m_hTokenCollector, 0);

		// �����尡 ��� �����Ҷ����� ��ٸ�
		HANDLE hThread[2] = { m_hUpdateThread, m_hTokenCollector };
		WaitForMultipleObjects(2, hThread, TRUE, INFINITE);		

		// ä�� ���� ���� ����
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
		// �ϰ� ����ü �Ҵ�	
		CHAT_WORK *pWork = m_pWorkPool->Alloc();

		pWork->type = CHAT_WORK_TYPE::JOIN;
		pWork->SessionID = SessionID;

		// �ϰ� ��ť
		m_WorkQ.Enqueue(pWork);

		// ���ο� ������ ����Ǿ����� ä�ü����� �˷��ش�.
		// ������Ʈ �����尡 ���� �� �ֵ��� �̺�Ʈ ��ȣ �ش�.
		SetEvent(m_WorkEvent);
	}

	void GGM::CNetServerChat::OnClientLeave(ULONGLONG SessionID)
	{
		// �ϰ� ����ü �Ҵ�
		CHAT_WORK *pWork = m_pWorkPool->Alloc();

		pWork->type = CHAT_WORK_TYPE::LEAVE;
		pWork->SessionID = SessionID;

		// �ϰ� ��ť
		m_WorkQ.Enqueue(pWork);

		// ������ ������ �Ǿ����� ä�ü����� �˷��ش�.
		// ������Ʈ �����尡 ���� �� �ֵ��� �̺�Ʈ ��ȣ �ش�.
		SetEvent(m_WorkEvent);
	}

	bool GGM::CNetServerChat::OnConnectionRequest(const SOCKADDR_IN & ClientAddr)
	{
		// ������ ������ ����
		return true;
	}

	void GGM::CNetServerChat::OnRecv(ULONGLONG SessionID, CNetPacket * pPacket)
	{
		// �ϰ� ����ü �Ҵ�
		CHAT_WORK *pWork = m_pWorkPool->Alloc();

		pWork->type = CHAT_WORK_TYPE::PACKET;
		pWork->SessionID = SessionID;
		pWork->pPacket = pPacket;

		// ������ ������ ������Ʈ �����尡 ��Ŷ �����ٰ� �� ���̹Ƿ� ���� ī��Ʈ ���������ش�.
		pPacket->AddRefCnt();

		// �ϰ� ��ť
		m_WorkQ.Enqueue(pWork);

		// ó���� ��Ŷ�� ���������� ä�ü����� �˷��ش�.
		// ������Ʈ �����尡 ���� �� �ֵ��� �̺�Ʈ ��ȣ �ش�.	
		SetEvent(m_WorkEvent);
	}

	void GGM::CNetServerChat::OnSend(ULONGLONG SessionID, int SendSize)
	{		
	}

	void GGM::CNetServerChat::OnWorkerThreadBegin()
	{
		// ���� ������ ����
	}

	void GGM::CNetServerChat::OnWorkerThreadEnd()
	{
		// ���� ������ ����
	}

	void GGM::CNetServerChat::OnError(int ErrorNo, const TCHAR * ErrorMsg)
	{
		// ���������� �������� ������ ���� ũ����
		CLogger::GetInstance()->Log(_T("ChatServer"), LEVEL::DBG, OUTMODE::FILE, ErrorMsg, ErrorNo);
		CCrashDump::ForceCrash();		
	}

	unsigned int __stdcall GGM::CNetServerChat::UpdateThread(LPVOID Param)
	{
		// static �Լ��̱� ������ ����� �����ϱ� ���� this ������ ���´�.
		CNetServerChat *pThis = (CNetServerChat*)Param;

		// �ϰ��� ���� ť�� �ּҸ� ���´�.
		CLockFreeQueue<CHAT_WORK*> *pWorkQueue = &(pThis->m_WorkQ);

		// �ϰ� Ǯ
		CTlsMemoryPool<CHAT_WORK> *pWorkPool = pThis->m_pWorkPool;

		// NetServer�� �ϰ� ��ȣ�� ��ٸ� �̺�Ʈ��ü�� ���´�.
		HANDLE WorkEvent = pThis->m_WorkEvent;

		// ������Ʈ �������� ������ �������� �Լ������� �迭�� �ּҸ� ���´�.
		CHAT_WORK_PROC *pChatWorkProc = pThis->m_ChatWorkProc;

		ULONGLONG *pUpdateTPS = &(pThis->m_UpdateTPS);

		// ť�� �ϰ��� ������ ����� �����Ѵ�.
		while (true)
		{
			DWORD WaitResult = WaitForSingleObjectEx(WorkEvent, INFINITE, true);

			// ������� 
			if (WaitResult == WAIT_IO_COMPLETION)
				break;

			// �ѹ� ������� ���� �� �ִ¸�ŭ ���� �̾Ƽ� ��ó�� �Ѵ�.
			while (pWorkQueue->size() > 0)
			{
				// ť���� �ϰ� �̾Ƴ���
				CHAT_WORK *pWork;

				pWorkQueue->Dequeue(&pWork);

				///////////////////////////////////////////////////////////////////////////////////////	
				// Switch~ case ���� ������� �ʰ� pWork->type�� �ε����� ����Ͽ� �ش� ���ν��� ȣ��.		
				// PACKET == 0 -> �Ϲ����� ������ ��Ŷ ó�� (OnRecvProc)
				// JOIN   == 1 -> ���� ���� ó�� (OnJoinProc ȣ��)
				// LEAVE  == 2 -> ���� ���� ���� ó�� (OnLeaveProc)
				///////////////////////////////////////////////////////////////////////////////////////
				(pThis->*pChatWorkProc[pWork->type])(pWork);

				// �ϰ� FREE			
				pWorkPool->Free(pWork);

				InterlockedIncrement(pUpdateTPS);
			}
		}

		// ä�� ������ ���� ���� ���� ť�� �����ִ� ���ҽ����� Free ���ش�.
		while (pWorkQueue->size() > 0)
		{
			// ť���� �ϰ� �̾Ƴ���
			CHAT_WORK *pWork;

			if (pWorkQueue->Dequeue(&pWork) == false)
				pThis->OnError(GGM_ERROR::LOCK_FREE_Q_DEQ_FAILED, _T("WorkQueue Deq Failed %d"));

			// ��Ŷ ����
			if (pWork->type == CHAT_WORK_TYPE::PACKET)
				CNetPacket::Free(pWork->pPacket);

			// �ϰ� FREE			
			pWorkPool->Free(pWork);
		}

		_tprintf_s(_T("[CHAT SERVER] Update Thread Exit [ REMAINING QUEUE SIZE ] : %ld\n"), pWorkQueue->size());

		return 0;
	}

	unsigned int __stdcall CNetServerChat::TokenThread(LPVOID Param)
	{
		// static �Լ��̱� ������ ����� �����ϱ� ���� this ������ ���´�.
		CNetServerChat *pThis = (CNetServerChat*)Param;

		// ��ū ���̺��� �� 
		PSRWLOCK pLock = &(pThis->m_lock);

		// ��ū ���̺�
		unordered_map<INT64, Token*> *pTokenTable = &(pThis->m_TokenTable);

		// ��ū Ǯ
		CTlsMemoryPool<Token> *pTokenPool = pThis->m_pTokenPool;			

		while (true)
		{
			// �����ð����� ����� ��ū ���̺��� ��ȸ�ϸ� �����Ѵ�.
			DWORD ret = SleepEx(TOKEN_ERASE_PERIOD, true);	

			if (ret == WAIT_IO_COMPLETION)
				break;					

			// ��ū ���̺� ��ȸ�ϸ� �����ð� ���� ������� ��ū ����
			AcquireSRWLockExclusive(pLock);

			auto iter_cur = pTokenTable->begin();
			auto iter_end = pTokenTable->end();

			ULONGLONG CurrentTime = GetTickCount64();
			
			while (iter_cur != iter_end)
			{
				Token *pToken = iter_cur->second;
				if (CurrentTime - (pToken->InsertTime) > TOKEN_ERASE_PERIOD)
				{
					// ��ū ���̺��� ��ū ����
					iter_cur = pTokenTable->erase(iter_cur);

					// ��ū Ǯ�� ��ū �ݳ�
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
		// ���� 
		////////////////////////////////////////////////////

		// ���� ������������ ������ �����ϱ����� ��ū ��� ��ȯ 
		AcquireSRWLockExclusive(pLock);

		if (pTokenTable->size() > 0)
		{
			auto iter_cur = pTokenTable->begin();
			auto iter_end = pTokenTable->end();			

			while (iter_cur != iter_end)
			{
				// ��ū Ǯ�� ��ū �ݳ�
				pTokenPool->Free(iter_cur->second);

				++iter_cur;			
			}
		}

		ReleaseSRWLockExclusive(pLock);

		return 0;
	}

	bool GGM::CNetServerChat::OnJoinProc(CHAT_WORK* pWork)
	{
		// ���� ó�� ���� 
		// �ָ��� : �÷��̾� ���� ���̺� �÷��̾ �߰��Ѵ�.			
		ULONGLONG SessionID = pWork->SessionID;
		Player *pPlayer = &m_PlayerArr[(WORD)SessionID];

		if (pPlayer->SessionID != INIT_SESSION_ID)
		{
			OnError(GGM_ERROR::SESSION_SYNC_FAILED, _T("OnJoinProc Session Sync Failed %d"));
		}

		// �ش� ���� ���̵� ������ ���
		pPlayer->SessionID = SessionID;

		// ���� �α��� �ÿ��� ���������� �����Ƿ� �ϴ� nullptr
		// ���߿� ���� �̵� ��û ��Ŷ�� ������ ���� ������ ���ŵȴ�.
		// �� �������� � ����ó���� �Ǿ�� �ȵ�
		pPlayer->pSector = nullptr;

		// �α��� ó�� �ȵ� ���·� �ٲ�
		pPlayer->IsLogIn = false;

		++m_PlayerCount;	

		return true;
	}

	bool GGM::CNetServerChat::OnRecvProc(CHAT_WORK* pWork)
	{
		// ��Ŷ Ÿ�Ժ� ���ν����� ������ ��� �Լ� ������ �迭
		CHAT_PACKET_PROC *pPacketProc = m_PacketProc;

		// �ϰ����� ��Ŷ ��������
		CNetPacket *pPacket = pWork->pPacket;

		// ��Ŷ Ÿ�� Ȯ��		
		WORD PacketType;
		pPacket->Dequeue((char*)&PacketType, sizeof(PacketType));		

		// �̻��� ��Ŷ Ÿ�� ���� Ŭ���̾�Ʈ��� ���� ���´�.
		if ((PacketType & 1) == 0 || PacketType > NUM_CHAT_PACKET_TYPE || PacketType < 0)
		{
			Disconnect(pWork->SessionID);

			// �ش� ��Ŷ�� NetServer���� �ǳ��� ���� ������ ���̹Ƿ� Free �� �־�� �Ѵ�.
			CNetPacket::Free(pPacket);
			return false;
		}

		/////////////////////////////////////////////////////////////////////
		// PacketType ������ ���뿡 ���� ��Ŷ ó��
		// PacketType == ��Ŷ ���ν��� ��� �Լ� ������ �迭�� �ε���
		// 1 ==  ä�ü��� �α��� ��û (en_PACKET_CS_CHAT_REQ_LOGIN)
		// 3 ==  ä�ü��� ���� �̵� ��û (en_PACKET_CS_CHAT_REQ_SECTOR_MOVE)
		// 5 ==  ä�ü��� ä�� ������ ��û (en_PACKET_CS_CHAT_REQ_MESSAGE)
		// 7 ==  ��Ʈ��Ʈ (en_PACKET_CS_CHAT_REQ_HEARTBEAT)
		/////////////////////////////////////////////////////////////////////		
		Player *pPlayer = &m_PlayerArr[(WORD)(pWork->SessionID)];

		if (pPlayer->SessionID != pWork->SessionID)
		{
			OnError(GGM_ERROR::SESSION_SYNC_FAILED, _T("OnRecvProc Session Sync Failed %d"));
			return false;
		}

		(this->*pPacketProc[PacketType])(pPacket, pPlayer);

		// �ش� ��Ŷ�� NetServer���� �ǳ��� ���� ������ ���̹Ƿ� Free �� �־�� �Ѵ�.
		CNetPacket::Free(pPacket);

		return true;
	}

	bool GGM::CNetServerChat::OnLeaveProc(CHAT_WORK* pWork)
	{
		// ���� ���� ó�� ���� 
		// �ָ��� : �÷��̾� ��ü ���� -> �÷��̾� ���� ���� 				
		Player *pPlayerArr = m_PlayerArr;
		ULONGLONG SessionID = pWork->SessionID;

		// �÷��̾� �˻�

		Player *pPlayer = &pPlayerArr[(WORD)SessionID];

		if (pPlayer->SessionID != SessionID)
		{
			OnError(GGM_ERROR::SESSION_SYNC_FAILED, _T("OnLeaveProc Session Sync Failed %d"));
			return false;
		}		

		if (pPlayer->pSector != nullptr)
		{
			// �÷��̾ ���Ϳ� �����ִٸ� �������ش�.
			WORD SectorIdx = pPlayer->SectorIdx;
			size_t SectorArrSize = pPlayer->pSector->SectorSize;
			Player **SectorArr = pPlayer->pSector->SectorArr;

			// �÷��̾��� �ε����� ���͹迭�� �������� �ƴ϶��
			if (SectorIdx != (SectorArrSize - 1))
			{
				// ������ �ε����� �ش��ϴ� �÷��̾ ������ �÷��̾��� �ڸ��� �ž��ش�.
				SectorArr[SectorIdx] = SectorArr[SectorArrSize - 1];
				SectorArr[SectorIdx]->SectorIdx = SectorIdx;
			}

			// �迭�� Size �ٿ��ش�.
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
		// ä�ü��� �α��� ��û
		//
		//	{
		//		WORD	Type
		//
		//		INT64	AccountNo
		//		WCHAR	ID[20]				// null ����
		//		WCHAR	Nickname[20]		// null ����
		//		char	SessionKey[64];
		//	}
		//
		//------------------------------------------------------------

		/////////////////////////////////////////////////////////////////////
		// �÷��̾� ������ �����Ѵ�.	
		// AccountNo ������		
		INT64 AccountNo;
		int Result = pPacket->Dequeue((char*)&AccountNo, sizeof(INT64));
		if (Result == GGM_PACKET_ERROR)
		{
			Disconnect(pPlayer->SessionID);
			return;
		}	

		pPlayer->AccountNo = AccountNo;		

		// ID, �г��� �ϻ층
		Result = pPacket->Dequeue((char*)pPlayer->ID, (ID_LEN + NICK_LEN) * sizeof(TCHAR));
		if (Result == GGM_PACKET_ERROR)
		{
			Disconnect(pPlayer->SessionID);
			return;
		}			

		/////////////////////////////////////////////////////////////////////////////////////////
		// ����Ű ���� 

		// ����Ű ������
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

			// ��ū ���̺� ����ִ� ����Ű�� ������ ��Ŷ�� �Ǿ�� ����Ű�� ���Ͽ� ��ȿ�� �˻��Ѵ�.
			auto token_iter = m_TokenTable.find(AccountNo);

			// ����Ű �����Ϸ� ���Դµ� �ش� AccountNo�� �ش��ϴ� ������ ������ �ȵ�
			if (token_iter == m_TokenTable.end())
			{
				m_SessionKeyNotFound++; // ����͸��� ī����				
				IsLogin = false;
				LoginStatus = FALSE;
				break;
			}

			// ��ū ���̺� �ִ� ����Ű ���ͼ� Ŭ�� ���� ����Ű�� ��
			Token *pToken = token_iter->second;
			char* TableSessionKey = pToken->SessionKey;			

			////////////////////////////////////////////////////////////////////////////////
			// ����Ű �� 
			// memcmp�� ������带 ���ϱ� ���� 8����Ʈ�� �޸� ������
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
				// ���� Ű�� �ٸ��ٸ� �α��� ����
				m_InvalidSessionKey++; // ����͸��� ī����				
				IsLogin = false;
				LoginStatus = FALSE;
				break;
			}
			////////////////////////////////////////////////////////////////////////////////		

		} while (0);

		ReleaseSRWLockExclusive(&m_lock);
		/////////////////////////////////////////////////////////////////////////////////////////

		// �ش� �÷��̾��� �α��� ���� ������Ʈ
		pPlayer->IsLogIn = IsLogin;

		// ��û�� ���� ���� ��Ŷ �۽�	
		ResLoginProc(pPlayer->SessionID, LoginStatus, AccountNo);	

		// �α��� ������ ���� �� ����
		if (IsLogin == true)
			m_LoginPlayer++;
	}

	void GGM::CNetServerChat::ReqSectorMoveProc(CNetPacket* pPacket, Player* pPlayer)
	{
		//------------------------------------------------------------
		// ä�ü��� ���� �̵� ��û
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
		
		// �α��� ���°� �ƴѵ� ��û ��Ŷ �������� ���� ���´�.	
		if (pPlayer->IsLogIn == false)
		{
			Disconnect(pPlayer->SessionID);
			return;
		}

		// AccountNo ������		
		INT64 AccountNo;
		int Result = pPacket->Dequeue((char*)&AccountNo, sizeof(INT64));
		if (Result == GGM_PACKET_ERROR)
		{
			Disconnect(pPlayer->SessionID);
			return;
		}

		// ���� ���޵� AccountNo�� �÷��̾��� ���� AccountNo�� �ٸ��ٸ� ���� ����
		if (AccountNo != pPlayer->AccountNo)
		{
			Disconnect(pPlayer->SessionID);
			return;
		}

		// �̵��� ������ ������ ���´�.
		// [0] == X
		// [1] == Y
		WORD SectorCoord[2];
		Result = pPacket->Dequeue((char*)SectorCoord, sizeof(SectorCoord));
		if (Result == GGM_PACKET_ERROR)
		{
			Disconnect(pPlayer->SessionID);
			return;
		}

		// ���� ������ ��ǥ�� ��ȿ�� ��ġ���� ���˻� �Ѵ�.
		if (SectorCoord[0] < 0
			|| SectorCoord[0] > SECTOR_WIDTH - 1
			|| SectorCoord[1] < 0
			|| SectorCoord[1] > SECTOR_HEIGHT - 1)
		{
			// Ŭ���̾�Ʈ�� �̻��� ��ǥ �������� ���� ���´�.		
			Disconnect(pPlayer->SessionID);
			return;
		}

		// �α��� �� ���� ��� ���Ϳ��� �������� �ʴٸ� ���Ͱ��� nullptr�̴�.
		// ���Ͱ��� �ʱⰪ�̶�� ���� �÷��̾ ��� ���Ϳ��� �������� ������ �ǹ��Ѵ�.
		// �ʱⰪ�� �ƴ� ��ȿ�� ���Ϳ� �����ִ� ���¶�� ���� ���Ϳ��� �ش� �÷��̾ �����Ѵ�.			
		if (pPlayer->pSector != nullptr)
		{
			// �÷��̾ ���Ϳ� �����ִٸ� �������ش�.
			short SectorIdx = pPlayer->SectorIdx;
			size_t SectorArrSize = pPlayer->pSector->SectorSize;
			Player **SectorArr = pPlayer->pSector->SectorArr;

			// �÷��̾��� �ε����� ���͹迭�� �������� �ƴ϶��
			if (SectorIdx != (SectorArrSize - 1))
			{
				// ������ �ε����� �ش��ϴ� �÷��̾ ������ �÷��̾��� �ڸ��� �ž��ش�.
				SectorArr[SectorIdx] = SectorArr[SectorArrSize - 1];
				SectorArr[SectorIdx]->SectorIdx = SectorIdx;
			}

			// �迭�� Size �ٿ��ش�.
			pPlayer->pSector->SectorSize--;
		}

		// Ŭ�� ��û�� ���� �̵� ��ǥ�� ��ȿ�ϴٸ� �̵������ش�.		
		Sector *pSector = &m_GameMap[SectorCoord[1]][SectorCoord[0]];
		size_t  SectorLastIdx = pSector->SectorSize;
		pSector->SectorArr[SectorLastIdx] = pPlayer;
		
		// �÷��̾� ���� ���� ����	
		pPlayer->pSector = pSector;
		pPlayer->SectorIdx = (WORD)SectorLastIdx;
		pPlayer->pSector->SectorSize++;

		// ���� �̵� ��� �۽�
		ResSectorMoveProc(pPlayer->SessionID, AccountNo, SectorCoord[0], SectorCoord[1]);
	}

	void GGM::CNetServerChat::ReqMsgProc(CNetPacket* pPacket, Player* pPlayer)
	{
		//------------------------------------------------------------
		// ä�ü��� ä�ú����� ��û
		//
		//	{
		//		WORD	Type
		//
		//		INT64	AccountNo
		//		WORD	MessageLen
		//		WCHAR	Message[MessageLen / 2]		// null ������
		//	}
		//
		//------------------------------------------------------------	

		// �α��� ���°� �ƴϰų� ���� ��� ���Ϳ��� �������� �ʴ� �÷��̾��� ä�� ��Ŷó�� �Ұ���
		if (pPlayer->IsLogIn == false || pPlayer->pSector == nullptr)
		{
			Disconnect(pPlayer->SessionID);
			return;
		}

		// AccountNo ������		
		INT64 AccountNo;
		int Result = pPacket->Dequeue((char*)&AccountNo, sizeof(INT64));
		if (Result == GGM_PACKET_ERROR)
		{
			Disconnect(pPlayer->SessionID);
			return;
		}

		// ���� ���޵� AccountNo�� �÷��̾��� ���� AccountNo�� �ٸ��ٸ� ���� ����
		if (AccountNo != pPlayer->AccountNo)
		{
			Disconnect(pPlayer->SessionID);
			return;
		}

		// MessageLen ������
		WORD MessageLen;
		Result = pPacket->Dequeue((char*)&MessageLen, sizeof(WORD));
		if (Result == GGM_PACKET_ERROR)
		{
			Disconnect(pPlayer->SessionID);
			return;
		}

		// �޽��� ������
		char ChatMsg[CHAT_MSG_LEN];
		Result = pPacket->Dequeue(ChatMsg, MessageLen);
		if (Result == GGM_PACKET_ERROR)
		{
			Disconnect(pPlayer->SessionID);
			return;
		}
	
		// �޼��� ����
		ResMsgProc(pPlayer->SessionID, AccountNo, pPlayer, MessageLen, (TCHAR*)ChatMsg);
	}

	void GGM::CNetServerChat::ReqHeartbeatProc(CNetPacket* pPacket, Player* pPlayer)
	{
		// �ϴ� ������ ��Ʈ��Ʈ ��Ŷ ó������ ����	
	}

	void GGM::CNetServerChat::ResLoginProc(ULONGLONG SessionID, BYTE Result, INT64 AccountNo)
	{
		//------------------------------------------------------------
		// ä�ü��� �α��� ����
		//
		//	{
		//		WORD	Type
		//
		//		BYTE	Status				// 0:����	1:����
		//		INT64	AccountNo
		//	}
		//
		//------------------------------------------------------------

		// ���� ���� ��Ŷ �ϳ� �Ҵ� �޴´�.
		CNetPacket *pPacket = CNetPacket::Alloc();

		// ��Ŷ ����
		CreatePacketResLogin(pPacket, Result, AccountNo);

		// ��Ŷ �۽�
		// �α��� ����� �����̸� ��Ŷ �۽Ÿ�, ���ж�� ��Ŷ �۽� �� ���� ����
		if (Result == TRUE)
			SendPacket(SessionID, pPacket);
		else
			SendPacketAndDisconnect(SessionID, pPacket);

		// Alloc �Ҷ� ����ī��Ʈ �ø��Ƿ� �׿� ���� Free�̴�.
		CNetPacket::Free(pPacket);
	}

	void GGM::CNetServerChat::ResSectorMoveProc(ULONGLONG SessionID, INT64 AccountNo, WORD SectorX, WORD SectorY)
	{
		// ���� ���� ��Ŷ �ϳ� �Ҵ� �޴´�.
		CNetPacket *pPacket = CNetPacket::Alloc();

		// ��Ŷ ����
		CreatePacketResSectorMove(pPacket, AccountNo, SectorX, SectorY);

		// ��Ŷ �۽�
		SendPacket(SessionID, pPacket);

		// Alloc �Ҷ� ����ī��Ʈ �ø��Ƿ� �׿� ���� Free�̴�.
		CNetPacket::Free(pPacket);
	}

	void GGM::CNetServerChat::ResMsgProc(ULONGLONG SessionID, INT64 AccountNo, Player * pPlayer, WORD MsgLen, TCHAR * Msg)
	{
		// ���� ���� ��Ŷ �ϳ� �Ҵ� �޴´�.
		CNetPacket *pPacket = CNetPacket::Alloc();

		// ��Ŷ ����
		CreatePacketResMsg(pPacket, AccountNo, pPlayer->ID, pPlayer->Nickname, MsgLen, Msg);

		// �ش� ä�� �޽����� ���� �÷��̾��� ���͸� ������ �ֺ� 9�� ���Ϳ� ��ġ�� �÷��̾�鿡�� ä�� �޽����� ��� �����ش�.
		// ���ڸ޽����� �������� ä�� �޽����� �������� ���� ���ǿ��Ե� �ٽ� ���� �ش�.	

		// ������ �Ǵ� �÷��̾��� �ֺ� 9�� ���� �� ��ȿ�� ������ �� 
		int ValidSectorCount = pPlayer->pSector->ValidCount;

		// ������ �Ǵ� �÷��̾��� �ֺ� 9�� ���� �� ��ȿ�� ������ ������ �迭
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

		// Alloc�� ���� Free
		CNetPacket::Free(pPacket);
	}

	void GGM::CNetServerChat::CreatePacketResLogin(CNetPacket * pPacket, BYTE Result, INT64 AccountNo)
	{
		//------------------------------------------------------------
		// ä�ü��� �α��� ����
		//
		//	{
		//		WORD	Type
		//
		//		BYTE	Status				// 0:����	1:����
		//		INT64	AccountNo
		//	}
		//
		//------------------------------------------------------------

		// �۽��� ��Ŷ�� �����Ѵ�.
		WORD PacketType = en_PACKET_CS_CHAT_RES_LOGIN;
		pPacket->Enqueue((char*)&PacketType, sizeof(WORD));
		pPacket->Enqueue((char*)&Result, sizeof(BYTE));
		pPacket->Enqueue((char*)&AccountNo, sizeof(INT64));
	}

	void GGM::CNetServerChat::CreatePacketResSectorMove(CNetPacket * pPacket, INT64 AccountNo, WORD SectorX, WORD SectorY)
	{
		//------------------------------------------------------------
		// ä�ü��� ���� �̵� ���
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
		
		// �۽��� ��Ŷ�� �����Ѵ�.
		WORD PacketType = en_PACKET_CS_CHAT_RES_SECTOR_MOVE;
		pPacket->Enqueue((char*)&PacketType, sizeof(WORD));
		pPacket->Enqueue((char*)&AccountNo, sizeof(INT64));
		pPacket->Enqueue((char*)&SectorX, sizeof(WORD));
		pPacket->Enqueue((char*)&SectorY, sizeof(WORD));
	}

	void GGM::CNetServerChat::CreatePacketResMsg(CNetPacket *pPacket, INT64 AccountNo, TCHAR * ID, TCHAR * NickName, WORD MsgLen, TCHAR * Msg)
	{
		//------------------------------------------------------------
		// ä�ü��� ä�ú����� ����  (�ٸ� Ŭ�� ���� ä�õ� �̰ɷ� ����)
		//
		//	{
		//		WORD	Type
		//
		//		INT64	AccountNo
		//		WCHAR	ID[20]						// null ����
		//		WCHAR	Nickname[20]				// null ����
		//		
		//		WORD	MessageLen
		//		WCHAR	Message[MessageLen / 2]		// null ������
		//	}
		//
		//------------------------------------------------------------	

		// �۽��� ��Ŷ�� �����Ѵ�.
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
		// ���� �ʱ�ȭ	
		for (int iY = 0; iY < SECTOR_HEIGHT; iY++)
		{
			for (int iX = 0; iX < SECTOR_WIDTH; iX++)
			{
				// �ϳ��� ���Ϳ� �ϴ� õ����� �޸� ���� ������ ��
				//m_GameMap[iY][iX].SectorTable.reserve(100);				

				// ������ ����� �ڱ� ��ǥ�� ��� �ִ´�.
				m_GameMap[iY][iX].SectorX = iX;
				m_GameMap[iY][iX].SectorY = iY;

				m_GameMap[iY][iX].ValidSector[m_GameMap[iY][iX].ValidCount] = &m_GameMap[iY][iX];
				m_GameMap[iY][iX].ValidCount++;

				// �ֺ� ���Ͱ� ������ nullptr ������ �ּҰ� ����		
				if (iY > 0)
				{
					// ��ȿ�� ������ �����͸� �迭�� ���� �����ϰ� ī���͸� ����.
					m_GameMap[iY][iX].ValidSector[m_GameMap[iY][iX].ValidCount] = &m_GameMap[iY - 1][iX];
					m_GameMap[iY][iX].ValidCount++;
				}

				if (iY < SECTOR_HEIGHT - 1)
				{
					// ��ȿ�� ������ �����͸� �迭�� ���� �����ϰ� ī���͸� ����.
					m_GameMap[iY][iX].ValidSector[m_GameMap[iY][iX].ValidCount] = &m_GameMap[iY + 1][iX];
					m_GameMap[iY][iX].ValidCount++;
				}

				if (iY > 0 && iX > 0)
				{
					// ��ȿ�� ������ �����͸� �迭�� ���� �����ϰ� ī���͸� ����.
					m_GameMap[iY][iX].ValidSector[m_GameMap[iY][iX].ValidCount] = &m_GameMap[iY - 1][iX - 1];
					m_GameMap[iY][iX].ValidCount++;
				}

				if (iY > 0 && iX < SECTOR_WIDTH - 1)
				{
					// ��ȿ�� ������ �����͸� �迭�� ���� �����ϰ� ī���͸� ����.
					m_GameMap[iY][iX].ValidSector[m_GameMap[iY][iX].ValidCount] = &m_GameMap[iY - 1][iX + 1];
					m_GameMap[iY][iX].ValidCount++;
				}

				if (iX > 0)
				{
					// ��ȿ�� ������ �����͸� �迭�� ���� �����ϰ� ī���͸� ����.
					m_GameMap[iY][iX].ValidSector[m_GameMap[iY][iX].ValidCount] = &m_GameMap[iY][iX - 1];
					m_GameMap[iY][iX].ValidCount++;
				}

				if (iX < SECTOR_WIDTH - 1)
				{
					// ��ȿ�� ������ �����͸� �迭�� ���� �����ϰ� ī���͸� ����.
					m_GameMap[iY][iX].ValidSector[m_GameMap[iY][iX].ValidCount] = &m_GameMap[iY][iX + 1];
					m_GameMap[iY][iX].ValidCount++;
				}

				if (iX > 0 && iY < SECTOR_HEIGHT - 1)
				{
					// ��ȿ�� ������ �����͸� �迭�� ���� �����ϰ� ī���͸� ����.
					m_GameMap[iY][iX].ValidSector[m_GameMap[iY][iX].ValidCount] = &m_GameMap[iY + 1][iX - 1];
					m_GameMap[iY][iX].ValidCount++;
				}

				if (iX < SECTOR_WIDTH - 1 && iY < SECTOR_HEIGHT - 1)
				{
					// ��ȿ�� ������ �����͸� �迭�� ���� �����ϰ� ī���͸� ����.
					m_GameMap[iY][iX].ValidSector[m_GameMap[iY][iX].ValidCount] = &m_GameMap[iY + 1][iX + 1];
					m_GameMap[iY][iX].ValidCount++;
				}

			}
		}
	}

	void __stdcall GGM::CNetServerChat::NetChatExitFunc(ULONG_PTR Param)
	{
		// �ƹ��͵� ���� ��������ֱ� ���� �뵵
		_tprintf_s(_T("NetChatExitFunc\n"));
	}

	size_t CNetServerChat::GetLoginPlayer() const
	{
		return m_LoginPlayer;
	}

	bool GGM::NetServerChatConfig::LoadConfig(const TCHAR * ConfigFileName)
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