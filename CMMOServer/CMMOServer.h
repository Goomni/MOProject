#include "CMMOSession.h"
#pragma comment(lib, "Winmm.lib")

namespace GGM
{
	//////////////////////////////////////////////////////////////////////////////
	// MMO ���Ӽ����� Ưȭ�� ������ CMMOServer Class 
	// * CNetServer�� �޸� ��Ʈ��ũ �ۼ��� ������Ӹ� �ƴ϶� ������ ó�� �����嵵 ����
	// * Auth ������, Game �����尡 ������ ó���� ���þ� ������
	// * ��Ʈ��ũ�� ���� ��ü�� �������� �÷��̾� ��ü�� ��Ӱ���� ���� 
	// * CNetServer�� �޸� �����庰�� ���� ������ �����Ͽ� ����ȭ ������ �ܼ�����
	// * ���� ������� �� �������� ��� ( Game Thread )	
	//////////////////////////////////////////////////////////////////////////////
	class CMMOServer
	{
	public:

		// ����͸��� ������
		ULONGLONG m_AcceptTotal = 0;
		ULONGLONG m_AcceptTPS = 0;

		// Auth �� Game Loop per Sec
		LONG      m_AuthLoopPerSec = 0;
		LONG      m_AuthLoopPerSecMonitor = 0;
		LONG      m_GameLoopPerSec = 0;
		LONG      m_GameLoopPerSecMonitor = 0;

		//Recv, Send TPS
		LONG      m_RecvTPS = 0;
		LONG      m_SendTPS = 0;

		// SessionCount		
		LONG      m_AuthSession = 0;		
		LONG      m_GameSession = 0;	
		LONG      m_AuthTimeoutDisconnectCount = 0;
		LONG      m_GameTimeoutDisconnectCount = 0;

	public:

		// �⺻������ ��� �ʱ�ȭ�� ������ Start�� Stop �Լ����� �ǽ��� ���̱� ������ �����ڿ� �Ҹ��ڴ� ���� ����
		CMMOServer() = default;
		virtual ~CMMOServer() = default;
		
		////////////////////////////////////////////////////////////
		// Start
		// ����
		// * TCHAR * BindIP [ ������ Bind IP ] 
		// * USHORT port    [ ���� ��Ʈ ]
		// * DWORD ConcurrentThreads [ IOCP�� ����� ��Ŀ������ ���ý��� ���� ]
		// * DWORD MaxThreads [ IOCP�� ����� ��Ŀ������ �ִ� ���� ���� ]
		// * bool IsNoDelay [ TCP_NODELAY �ɼ� �÷��� ]
		// * WORD MaxSessions [ �ִ� ���� ������ �� ]
		// * BYTE PacketCode [ ��Ŷ ��ȿ�� �˻� �ڵ� ]
		// * BYTE PacketKey [ ��Ŷ ��ȣȭ ��ĪŰ ]		
		// * CMMOSession *pSessionArr [���������� ������ �÷��̾� ��ü�迭�� ������]	
		// * int          PlayerSize  [���������� ������ �÷��̾� ��ü�� ������] 
		// * WORD         SendSleepTime [Send Thread Loop Sleep Time]	
		// * WORD         AuthSleepTime [Auth Thread Loop Sleep Time]	
		// * WORD         GameSleepTime [Game Thread Loop Sleep Time]	
		// * WORD         AuthPacketPerLoop [ Auth ������ �� ������ ���Ǵ� ó�� ��Ŷ ���� ]
		// * WORD         GamePacketPerLoop [ Game ������ �� ������ ���Ǵ� ó�� ��Ŷ ���� ]
		// * WORD         AcceptPerLoop [ Auth �����尡 �� ������ Accept ó���� ���� ]
		// * WORD         AuthToGamePerLoop [ Auth �����尡 �� ������ Game Mode�� �̰��� ���� ]
		// * ULONGLONG    TimeoutLimit [ ���� Ÿ�Ӿƿ� ó�� �ð� ]
		// * bool         IsTimeoutOn [ ���� Ÿ�Ӿƿ� ��� ��� ���� ]
		// ��ȯ : ����
		// ��� : ���� ���� 
		////////////////////////////////////////////////////////////
		bool    Start(
			TCHAR		*BindIP,
			USHORT		 port,
			DWORD		 ConcurrentWorkerThreads,
			DWORD		 MaxWorkerThreads,
			bool		 IsNoDelay,
			WORD		 MaxSessions,
			BYTE		 PacketCode,
			BYTE		 PacketKey,
			CMMOSession *pPlayerArr,
			int          PlayerSize,
			WORD         SendSleepTime,
			WORD         AuthSleepTime,
			WORD         GameSleepTime,
			WORD         ReleaseSleepTime,
			WORD         AuthPacketPerLoop,
			WORD         GamePacketPerLoop,
			WORD         AcceptPerLoop,
			WORD         AuthToGamePerLoop,
			ULONGLONG    TimeoutLimit,
			bool         IsTimeoutOn
		);

		////////////////////////////////////////////////////////////
		// Stop
		// ���� : ����
		// ��ȯ : ����
		// ��� : ���� ���� ����
		////////////////////////////////////////////////////////////
		void Stop();

		///////////////////////////////////////////////////////////////////////////////////
		// GetSessionCount 
		// ����  : ����
		// ��ȯ  : ULONGLONG
		// ���  : ���� �������� ���� �� ��ȯ	
		///////////////////////////////////////////////////////////////////////////////////
		ULONGLONG GetSessionCount() const;

	protected:

		////////////////////////////////////////////////////////////
		// �̺�Ʈ �ڵ鷯 ���� ���� �Լ� 
		////////////////////////////////////////////////////////////

		// �⺻������ ���ǰ� ���õ� �̺�Ʈ �ڵ鷯���� CMMOSession�� ����
		// ���ǰ� �������� �����庰 Update�� �̰����� ó�� 
		// Ŭ���̾�Ʈ�� ��û���� �������� ���� ���� ������ ó���� ���� �Լ� 
		virtual void OnAuth_Update() = 0;
		virtual void OnGame_Update() = 0;

		// ���������� ����Ǵ� �Լ����� ������ �߻��� �� �� �Լ��� ȣ���ؼ� �˷��� 
		virtual void OnError(int ErrorNo, const TCHAR* ErrorMsg) = 0;	
		virtual void OnSemError(USHORT Slot) = 0;
		virtual void OnTimeOut(ULONGLONG SessionID, WORD SessionIndex, ULONGLONG Timeout) = 0;

	private:

		////////////////////////////////////////////////////////////
		// Worker, Accept, Auth, Game ������ ���� ����
		////////////////////////////////////////////////////////////

		static unsigned int __stdcall WorkerThread(LPVOID Param);
		static unsigned int __stdcall AcceptThread(LPVOID Param);
		static unsigned int __stdcall SendThread(LPVOID Param);
		static unsigned int __stdcall AuthThread(LPVOID Param);
		static unsigned int __stdcall GameThread(LPVOID Param);		

		///////////////////////////////////////////////////////////////////////////////////
		// CheckPacket 
		// ����  : CMMOSession *pSession [ ���� ����ü ������ ]
		// ��ȯ  : bool ( ���Ź��� ��Ŷó���� ���������� true, ������ �߰ߵǸ� false)
		// ��� 
		// * ��Ʈ��ũ Ŭ���� ������ �ϼ��� ��Ŷ�� �����ϸ� ����� ���� ��Ŷť�� ����		
		// * ������ ��Ŷ���� ������ �߰ߵǸ� �ش� ���� ���� ���� ������ ���� ����
		///////////////////////////////////////////////////////////////////////////////////
		bool CheckPacket(CMMOSession *pSession);

		///////////////////////////////////////////////////////////////////////////////////
		// SendPost
		// ����  : CMMOSession *pSession [ ���� ����ü ������ ]
		// ��ȯ  : ������ true, ���н� false 
		// ���  : WSASend ��� 
		// ����  : SendThread���� ȣ��
		///////////////////////////////////////////////////////////////////////////////////		
		bool SendPost(CMMOSession *pSession);

		///////////////////////////////////////////////////////////////////////////////////
		// RecvPost
		// ����1 : CMMOSession *pSession [ ���� ����ü ������ ]
		// ��ȯ  : ������ true, ���н� false 
		// ���  : WSARecv ���
		// ����  : �񵿱� Recv�� �׻� ��ϵǾ� �ִ�.
		///////////////////////////////////////////////////////////////////////////////////		
		bool RecvPost(CMMOSession *pSession);	

		void ReleaseSession(CMMOSession *pSession);

		bool WSAInit(TCHAR * BindIP, USHORT port, bool IsNoDelay);
		bool SessionInit(WORD MaxSessions, CMMOSession *pPlayerArr, int PlayerSize);
		bool ThreadInit(DWORD ConcurrentThreads, DWORD MaxThreads);

	private:

		// ���� �ּ� ����ü
		SOCKADDR_IN                   m_ServerAddr;

		// CMMOSession ������ �迭, ������ ��Ʈ���� Start �Լ��� �������� ������ �ʱ�ȭ ��
		CMMOSession					**m_SessionArr = nullptr;	

		// ��Ŀ������� ����� IOCP�� �ڵ� [ ���ý��� ������ ����, ���� ������ ���� ����ڰ� ���� ]
		HANDLE						  m_hWorkerIOCP = NULL;

		// ������ �ڵ�  
		HANDLE						 *m_WorkerThreadHandleArr = nullptr;
		HANDLE                        m_AcceptThread = nullptr;
		HANDLE                        m_SendThread[SEND_THREAD_COUNT];
		HANDLE                        m_AuthThread = nullptr;
		HANDLE                        m_GameThread = nullptr;
		HANDLE                        m_ReleaseThread = nullptr;

		// ������ ���̵�
		DWORD						 *m_WorkerThreadIDArr = nullptr;		
		DWORD                         m_AcceptThreadID;
		DWORD                         m_SendThreadID[SEND_THREAD_COUNT];
		DWORD                         m_AuthThreadID;
		DWORD                         m_GameThreadID;
		DWORD                         m_ReleaseThreadID;

		// ��Ʈ��Ʈ üũ�� Ÿ�Ӿƿ� ���� �ð�
		ULONGLONG                     m_TimeoutLimit;
		bool                          m_IsTimeout;

	public:

		// Accept Thread�� Auth Thread ������ ������ ť, Ŭ���̾�Ʈ�� ���� �����ϸ� �� ������ AcceptThread�� ��ť		
		CLockFreeQueue<WORD>          *m_AcceptQueue = nullptr;	

		// ���԰����� ������ ������ ���� 				
		CListQueue<WORD>             *m_NextSession = nullptr;

		// ���� ���� 
		SOCKET						  m_Listen = INVALID_SOCKET;

		// ���� �� ���� ���� �� 
		ULONGLONG					  m_SessionCount = 0;
	
		// ������ �ִ� ������ �� 
		DWORD						  m_MaxSessions;

		// ������ ��Ŀ �������� ���� 
		DWORD						  m_WorkerThreadsCount = 0;

		// ���� Stop����
		bool                          m_IsExit = false;

		// �� ������ SleepTime
		WORD                          m_SendSleepTime;
		WORD                          m_AuthSleepTime;
		WORD                          m_GameSleepTime;
		WORD                          m_ReleaseSleepTime;

		// �� ������ ���Ǻ� ��Ŷó�� Ƚ��
		WORD                          m_AuthPacketPerLoop;
		WORD                          m_GamePacketPerLoop;

		// �� ������ AcceptQueue���� Dequeue�� ���� ���� 
		WORD                          m_AcceptPerLoop;

		// �� ������ Auth To Game ���� Game���� �ٲ� ����
		WORD                          m_AuthToGamePerLoop;
	};
}

