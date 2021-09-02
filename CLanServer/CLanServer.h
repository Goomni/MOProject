#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <WS2tcpip.h>
#include <winsock2.h>
#include "RingBuffer\RingBuffer.h"
#include "Packet\Packet.h"
#include "MemoryPool\MemoryPool.h"
#include "LockFreeStack\LockFreeStack.h"
#include "LockFreeQueue\LockFreeQueue.h"
#include "Define\GGM_CONSTANTS.h"
#pragma  comment(lib, "Ws2_32.lib")

namespace GGM
{
	////////////////////////////////////////////////////
	// CLanServer ���� ����ü LanSession			
	////////////////////////////////////////////////////
	struct LanSession
	{
		SOCKET							socket = INVALID_SOCKET;
		ULONGLONG						SessionID; // ���� ���ӽ� �߱޹޴� ����ũ�� ���̵�				
		LONG			                IoCount = 0;
		LONG							IsReleased = FALSE;
		LONG			                IsSending = FALSE; // ���� �÷��� 		
		WORD							SessionSlot; // ���� ���� �迭�� �ε��� 			
		bool                            IsCanceled = false;
		ULONG   					    SentPacketCount = 0; // SendPost ���ο��� Send��û�� ��Ŷ ����							         
		OVERLAPPED		                RecvOverlapped; // ���ſ� OVERLAPPED ����ü
		OVERLAPPED		                SendOverlapped; // �۽ſ� OVERLAPPED ����ü		
		CRingBuffer		                RecvQ; // ���ſ� ������				 
		CLockFreeQueue<CPacket*>        SendQ; // �۽ſ� ����ȭ ������ �����͸� ���� ������ ť
		CPacket		                   *PacketArray[MAX_PACKET_COUNT]; // SendQ���� �̾Ƴ� ����ȭ ������ �����͸� ���� �迭			
	};

	///////////////////////////////////////////////////////////////////
	// ���� ��Ʈ��ũ�� ������ ��ſ� ����� Ŭ���� CLanServer
	// - IOCP ���
	// - Ŭ���� ���ο� ��Ŀ�����带 ����
	// - �̺�Ʈ �ڵ鸵 �Լ��� ���������Լ��� �����Ͽ� �ܺο��� ��ӹ޾� ����
	// - ���ο��� ��ü������ ������ ������
	///////////////////////////////////////////////////////////////////
	class CLanServer
	{	
	public:

		// ����͸��� ������ 
		volatile LONG RecvTps = 0;
		volatile LONG SendTps = 0;
		ULONGLONG AcceptTotal = 0;
		
	public:

		////////////////////////////////////////////////////
		// ��Ʈ��ũ ���̺귯�� public ��� �Լ�		
		////////////////////////////////////////////////////

		///////////////////////////////////////////////////////////////////////////////////
		// ������ 
		// ����1 : ����
		// ��ȯ  : ����
		// ���  : Start �Լ����� �ʱ�ȭ�� �ʿ��� ��� ������ ���� ���̹Ƿ� �����ڿ��� ������ ����.
		// ����  : 
		///////////////////////////////////////////////////////////////////////////////////
		CLanServer() = default;

		///////////////////////////////////////////////////////////////////////////////////
		// �Ҹ��� 
		// ����1 : ����
		// ��ȯ  : ����
		// ���  : Stop �Լ����� ��� ������ ������ ���̹Ƿ� �Ҹ��ڿ��� ������ ����.
		// ����  : 
		///////////////////////////////////////////////////////////////////////////////////
		virtual ~CLanServer() = default;

		///////////////////////////////////////////////////////////////////////////////////
		// Start 
		// ����1 : TCHAR* BindIP [ ���� �ʱ�ȭ�� bind �� IP ]
		// ����2 : USHORT port [ ���� �ʱ�ȭ�� bind �� port ] 
		// ����3 : DWORD ConcurrentThreads [ IOCP�� ���ÿ� ������ ������ ���� ] 
		// ����4 : DWORD MaxThreads [ ������ �ִ� ������ ���� ]
		// ����5 : bool IsNagleOn [ TCP_NODELAY �ɼ� ON/OFF ���� ]
		// ����6 : DWORD MaxSessions [ �ִ� ������ �� ]
		// ��ȯ  : ������ true, ���н� false Ȥ��, �����ڵ�(WSAStartup ������)
		// ���  : ���� ���� �� �ʱ�ȭ
		// ����  : �ܺο��� ���� ���� ������ ȣ���Ѵ�.
		///////////////////////////////////////////////////////////////////////////////////
		bool Start(
			TCHAR* BindIP,
			USHORT port,
			DWORD ConcurrentThreads,
			DWORD MaxThreads,
			bool  IsNoDelay,
			WORD  MaxSessions
		);

		///////////////////////////////////////////////////////////////////////////////////
		// Stop 
		// ����1 : ����
		// ��ȯ  : ����
		// ���  : ���� ���� ����, ��� ���� ���� ���ҽ� ����
		// ����  : 
		///////////////////////////////////////////////////////////////////////////////////
		void Stop();

		///////////////////////////////////////////////////////////////////////////////////
		// GetSessionCount 
		// ����1 : ����
		// ��ȯ  : ULONGLONG
		// ���  : ���� �������� Ŭ���̾�Ʈ�� �� ��ȯ
		// ����  : 
		///////////////////////////////////////////////////////////////////////////////////
		ULONGLONG GetSessionCount() const;

		///////////////////////////////////////////////////////////////////////////////////
		// Disconnect
		// ����1 : ULONGLONG SessionID [ ������ ������ ������ �ĺ��� ]
		// ��ȯ  : true [ ���� ] // false [ ���� ]
		// ���  : �ܺο��� SessionID ������ ���� ���ᰡ �ʿ��� �� ��� 
		// ����  : �ܼ� ���� ����, ���� ���ҽ��� �Ϻ��ϰ� �����ϴ� ���� ReleaseSession �Լ���
		///////////////////////////////////////////////////////////////////////////////////
		bool Disconnect(ULONGLONG SessionID);

		///////////////////////////////////////////////////////////////////////////////////
		// SendPacket
		// ����1 : ULONGLONG SessionID [ ��Ŷ�� �۽��� ������ �ĺ��� ]
		// ����2 : CPacket *Packet [ ��Ŷ ������ ��� ����ȭ ������ ������ ]
		// ��ȯ  : true [ ���� ] // false [ ���� ]
		// ���  : �ش� ���ǿ��� ��Ŷ�� ������ �� ���		
		// ����  : ���� ���� �۽� �����۷� ��Ŷ ���� ���簡 �Ϸ�� ���Ŀ��� �ش� ���� �޸� ���� 
		///////////////////////////////////////////////////////////////////////////////////
		bool SendPacket(ULONGLONG SessionID, CPacket *Packet);	

	protected:

		/////////////////////////////////////////////////////////////////
		// * ��Ʈ��ũ I/O �̺�Ʈ �ڵ鷯 		
		/////////////////////////////////////////////////////////////////

		// Accept ���� �� ������ Ȯ���Ǹ� ȣ�� 
		virtual void OnClientJoin(const SOCKADDR_IN& ClientAddr, ULONGLONG SessionID) = 0;

		// �ش� ���ǿ� ���� ��� ���ҽ��� �����Ǹ� ȣ��  
		virtual void OnClientLeave(ULONGLONG SessionID) = 0;

		// Accept ���� ȣ�� 
		virtual bool OnConnectionRequest(const SOCKADDR_IN& ClientAddr) = 0;

		// ��Ŷ ���� �Ϸ� �� ȣ��
		virtual void OnRecv(ULONGLONG SessionID, CPacket *Packet) = 0;

		// ��Ŷ �۽� �Ϸ� �� ȣ��
		virtual void OnSend(ULONGLONG SessionID, int SendSize) = 0;

		// ��Ŀ������ GQCS �ٷ� �ϴܿ��� ȣ�� 
		virtual void OnWorkerThreadBegin() = 0;

		// ��Ŀ������ 1���� ���� �� ȣ��
		virtual void OnWorkerThreadEnd() = 0;

		// ���������� ����Ǵ� �Լ����� ������ �߻��� �� �� �Լ��� ȣ���ؼ� �˷��� 
		virtual void OnError(int ErrorNo, const TCHAR* ErrorMsg) = 0;

	private:

		////////////////////////////////////////////////////
		// ��Ʈ��ũ ���̺귯�� private ��� �Լ�	
		// ������ �����Ǹ� ������ ������ �ڵ� ȣ��� 
		////////////////////////////////////////////////////	

		///////////////////////////////////////////////////////////////////////////////////
		// AcceptThread : ������ �Լ��� ����ؾ��ϹǷ� static
		// ����1 : LPVOID Param [ CLanServer�� this ������ ]
		// ��ȯ  : ������ �Լ� ��ȯ 
		// ���  : accept, ���� �Ϸ� �� �������̺� ���� ���
		// ����  : ���� �̺�Ʈ �ڵ鸵 �Լ� OnConnectionRequest, OnClientJoin
		///////////////////////////////////////////////////////////////////////////////////
		static unsigned int WINAPI AcceptThread(LPVOID Param);

		///////////////////////////////////////////////////////////////////////////////////
		// WorkerThread  : ������ �Լ��� ����ؾ��ϹǷ� static
		// ����1 : LPVOID Param [ CLanServer�� this ������ ]
		// ��ȯ  : ������ �Լ� ��ȯ 
		// ���  : I/O�� ����� ��Ŀ ������ 
		// ����  : ���� �̺�Ʈ �ڵ鸵 �Լ� OnRecv, OnSend
		///////////////////////////////////////////////////////////////////////////////////
		static unsigned int WINAPI WorkerThread(LPVOID Param);			

		///////////////////////////////////////////////////////////////////////////////////
		// CheckPacket 
		// ����1 : Session *pSession [ ���� ����ü ������ ]
		// ��ȯ  : ����
		// ���  : ��Ʈ��ũ Ŭ���� ������ �ϼ��� ��Ŷ�� �����ϸ� ����� ���� OnRecv�� ����
		// ����  : ���� �̺�Ʈ �ڵ鸵 �Լ� OnRecv
		///////////////////////////////////////////////////////////////////////////////////
		void CheckPacket(LanSession *pSession);

		///////////////////////////////////////////////////////////////////////////////////
		// SendPost
		// ����1 : Session *pSession [ ���� ����ü ������ ]
		// ��ȯ  : ������ true, ���н� false 
		// ���  : WSASend ��� 
		// ����  : SendPacket�� ȣ��, OnSend �� ȣ�� 
		///////////////////////////////////////////////////////////////////////////////////		
		bool SendPost(LanSession *pSession);

		///////////////////////////////////////////////////////////////////////////////////
		// RecvPost
		// ����1 : Session *pSession [ ���� ����ü ������ ]
		// ��ȯ  : ������ true, ���н� false 
		// ���  : WSARecv ���
		// ����  : �񵿱� Recv�� �׻� ��ϵǾ� �ִ�.
		///////////////////////////////////////////////////////////////////////////////////		
		bool RecvPost(LanSession *pSession);

		///////////////////////////////////////////////////////////////////////////////////
		// ReleaseSession
		// ����1 : Session *pSession [ ���� ����ü ������ ]
		// ��ȯ  : ����
		// ���  : ���� ����
		// ����  : �� �Լ��� I/O Count�� 0�� ���� ȣ��Ǿ�� �Ѵ�.
		///////////////////////////////////////////////////////////////////////////////////	
		void ReleaseSession(LanSession *pSession);

		///////////////////////////////////////////////////////////////////////////////////
		// SessionAcquireLock
		// ����1 : Session *pSession [ ���� ����ü ������ ]
		// ��ȯ  : bool (������ true, ���н� false)
		// ���  : ��Ƽ ������ ȯ�濡�� ������ ��ȣ�ϴ� ���� ȹ�� (���Ͷ�)		
		///////////////////////////////////////////////////////////////////////////////////	
		bool SessionAcquireLock(LanSession *pSession, ULONGLONG LocalSessionID);

		///////////////////////////////////////////////////////////////////////////////////
		// SessionReleaseLock
		// ����1 : Session *pSession [ ���� ����ü ������ ]
		// ��ȯ  : bool (������ true, ���н� false)
		// ���  : ��Ƽ ������ ȯ�濡�� ������ ��ȣ�ϴ� ���� ��ȯ (���Ͷ�)	
		///////////////////////////////////////////////////////////////////////////////////	
		void SessionReleaseLock(LanSession *pSession);

		bool WSAInit(TCHAR * BindIP, USHORT port, bool IsNoDelay);
		bool SessionInit(WORD MaxSessions);
		bool ThreadInit(DWORD ConcurrentThreads, DWORD MaxThreads);
	
	private:	

		////////////////////////////////////////////////////
		// ��Ʈ��ũ ���̺귯�� private ��� ����		
		////////////////////////////////////////////////////
		
		// ��Ŀ������� ����� IOCP�� �ڵ� [ ���ý��� ������ ����, ���� ������ ���� ����ڰ� ���� ]
		HANDLE  m_hWorkerIOCP = NULL;	

		// ��Ŀ������ �ڵ� �迭 [������ ������ ����ڰ� ����] 
		HANDLE *m_ThreadHandleArr = nullptr;

		// ��Ŀ������ ���̵� �迭 [������ ������ ����ڰ� ����]
		DWORD *m_ThreadIDArr = nullptr;

		// ���� ���� �迭		
		LanSession * m_SessionArray = nullptr;

		// ���� ���԰����� �迭�� �ε����� �����ϰ� �ִ� ����
		CLockFreeStack<WORD> *m_NextSlot;		

		// ���� ���� 
		SOCKET     m_Listen = INVALID_SOCKET;		

		// ���� �� ���� ���� �� 
		ULONGLONG  m_SessionCount = 0;

		// �ִ� ���� ���� Ŭ���̾�Ʈ�� �� 
		DWORD      m_MaxSessions = 0;

		// ������ �������� ���� 
		DWORD      m_IOCPThreadsCount = 0;	

		// ���� ��������
		OVERLAPPED m_DummyOverlapped;

	};
}


