#ifndef _C_NET_SERVER_H_
#define _C_NET_SERVER_H_

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <WS2tcpip.h>
#include <winsock2.h>
#include <Mstcpip.h>
#include "RingBuffer\RingBuffer.h"
#include "Packet\Packet.h"
#include "MemoryPool\MemoryPool.h"
#include "LockFreeStack\LockFreeStack.h"
#include "LockFreeQueue\LockFreeQueue.h"
#include "Define\GGM_CONSTANTS.h"
#include <unordered_set>
#include <unordered_map>
#pragma  comment(lib, "Ws2_32.lib")

namespace GGM
{
	///////////////////////////////////////////////////////////////////
	// CNetClient���� ��ſ� ����� Ŭ���� CNetServer
	// - �̺�Ʈ �ڵ鷯�� ������ �߻� Ŭ���� 
	// - ���ο��� ��ü������ ������ ������
	// - IOCP�� ����� ��Ŀ�����尡 ��Ʈ��ũ I/O ó��
	///////////////////////////////////////////////////////////////////

	////////////////////////////////////////////////////
	// CNetServer ���� ����ü NetSession			
	////////////////////////////////////////////////////
	struct NetSession
	{		
		SOCKET							socket = INVALID_SOCKET;
		ULONGLONG						SessionID; // ���� ���ӽ� �߱޹޴� ����ũ�� ���̵�		
		LONG			                IoCount = 0;
		LONG							IsReleased = FALSE;					 
		LONG			                IsSending = FALSE; // Send �÷��� 	
		ULONG   					    SentPacketCount = 0; // SendPost ���ο��� Send��û�� ��Ŷ ����	
		bool                            IsSendAndDisconnect = false; // ������ ���� �÷��� 
		bool                            IsCanceled = false;
		CNetPacket                      *DisconnectPacket = nullptr;
		OVERLAPPED		                RecvOverlapped; // ���ſ� OVERLAPPED ����ü
		OVERLAPPED		                SendOverlapped; // �۽ſ� OVERLAPPED ����ü		
		CRingBuffer						RecvQ; // ���ſ� ������				 
		CLockFreeQueue<CNetPacket*>     SendQ; // �۽ſ� ������ ť
		CNetPacket   		            *PacketArray[MAX_PACKET_COUNT]; // SendQ���� �̾Ƴ� ����ȭ ������ �����͸� ���� �迭		
		IN_ADDR                         SessionIP;
		USHORT                          SessionPort;
		WORD							SessionSlot; // ���� ���� �迭�� �ε��� 			
	};

	class CNetServer
	{
	public:	
		// Accept Total Count
		ULONGLONG m_AcceptTotal = 0;

		// Accept TPS
		ULONGLONG m_AcceptTPS = 0;

		// Update TPS
		ULONGLONG m_UpdateTPS = 0;		

		// Send TPS
		LONG64 m_SendTPS = 0;

		// Recv TPS
		LONG64 m_RecvTPS = 0;		

		// Session Key Not Found
		ULONGLONG m_SessionKeyNotFound = 0;

		// Invalid Session Key
		ULONGLONG m_InvalidSessionKey = 0;

	public:		

		// ��� �ʱ�ȭ�� Start�Լ��� ����, ��� ������ Stop�Լ��� ���� ����
		CNetServer() = default;	
		virtual ~CNetServer() = default;

		///////////////////////////////////////////////////////////////////////////////////
		// Start 
		// ����1 : TCHAR* BindIP [ ���� �ʱ�ȭ�� bind �� IP ]
		// ����2 : USHORT port [ ���� �ʱ�ȭ�� bind �� port ] 
		// ����3 : DWORD ConcurrentThreads [ IOCP�� ���ÿ� ������ ������ ���� ] 
		// ����4 : DWORD MaxThreads [ ������ �ִ� ������ ���� ]
		// ����5 : bool IsNoDelay [ TCP_NODELAY �ɼ� ON/OFF ���� ]
		// ����6 : DWORD MaxSessions [ �ִ� ������ �� ]
		// ����7 : BYTE PacketCode [ ��Ŷ �ڵ� ]
		// ����8 : BYTE PacketKey1 [ ��Ŷ XOR Ű]		
		// ��ȯ  : ������ true, ���н� false 
		// ���  : ���� ���� �� �ʱ�ȭ
		// ����  : �ܺο��� ���� ���� ������ ȣ���Ѵ�.
		///////////////////////////////////////////////////////////////////////////////////
		bool Start(
			TCHAR * BindIP,
			USHORT port,
			DWORD ConcurrentThreads,
			DWORD MaxThreads,
			bool IsNoDelay,
			WORD MaxSessions,
			BYTE PacketCode,
			BYTE PacketKey		
		);

		///////////////////////////////////////////////////////////////////////////////////
		// Stop 
		// ����1 : ����
		// ��ȯ  : ����
		// ���  : ���� ���� ����, ��� ���� ���� ���ҽ� ����		
		///////////////////////////////////////////////////////////////////////////////////
		void Stop();

		///////////////////////////////////////////////////////////////////////////////////
		// GetSessionCount 
		// ����1 : ����
		// ��ȯ  : ULONGLONG
		// ���  : ���� �������� Ŭ���̾�Ʈ�� �� ��ȯ	
		///////////////////////////////////////////////////////////////////////////////////
		ULONGLONG GetSessionCount() const;

		///////////////////////////////////////////////////////////////////////////////////
		// Disconnect
		// ����1 : ULONGLONG SessionID [ ������ ������ ������ �ĺ��� ]
		// ��ȯ  : true [ ���� ] // false [ ���� ]
		// ���  : ������ ���� ���� ��û
		///////////////////////////////////////////////////////////////////////////////////
		bool Disconnect(ULONGLONG SessionID);

		///////////////////////////////////////////////////////////////////////////////////
		// SendPacket
		// ����1 : ULONGLONG SessionID [ ��Ŷ�� �۽��� ������ �ĺ��� ]
		// ����2 : CNetPacket *Packet [ ��Ŷ ������ ��� ����ȭ ������ ������ ]
		// ��ȯ  : true [ ���� ] // false [ ���� ]
		// ���  : �ش� ���ǿ��� ��Ŷ�� ������ �� ���			
		///////////////////////////////////////////////////////////////////////////////////
		bool SendPacket(ULONGLONG SessionID, CNetPacket *Packet);		

		///////////////////////////////////////////////////////////////////////////////////
		// SendPacketAndDisconnect
		// ����1 : ULONGLONG SessionID [ ��Ŷ�� �۽��� ������ �ĺ��� ]
		// ����2 : CNetPacket *Packet [ ��Ŷ ������ ��� ����ȭ ������ ������ ]
		// ��ȯ  : true [ ���� ] // false [ ���� ]
		// ���  : �ش� ���ǿ��� ��Ŷ�� �����ϰ� ������ ���´�.		
		///////////////////////////////////////////////////////////////////////////////////
		bool SendPacketAndDisconnect(ULONGLONG SessionID, CNetPacket *Packet);

	protected:	

		// Accept ���� �� ������ Ȯ���Ǹ� ȣ�� 
		virtual void OnClientJoin(const SOCKADDR_IN& ClientAddr, ULONGLONG SessionID) = 0;

		// �ش� ������ ������Ǹ� ȣ�� 
		virtual void OnClientLeave(ULONGLONG SessionID) = 0;

		// Accept ���� ȣ��, ������ �������� Ŭ���̾�Ʈ�� ��ȿ�� �˻�� Ȱ��
		virtual bool OnConnectionRequest(const SOCKADDR_IN& ClientAddr) = 0;

		// ��Ŷ ���� �Ϸ� �� ȣ��
		virtual void OnRecv(ULONGLONG SessionID, CNetPacket *Packet) = 0;

		// ��Ŷ �۽� �Ϸ� �� ȣ��
		virtual void OnSend(ULONGLONG SessionID, int SendSize) = 0;

		// ��Ŀ������ GQCS �ٷ� �ϴܿ��� ȣ�� 
		virtual void OnWorkerThreadBegin() = 0;

		// ��Ŀ������ 1���� ���� �� ȣ��
		virtual void OnWorkerThreadEnd() = 0;

		// ���������� ����Ǵ� �Լ����� ������ �߻��� �� �� �Լ��� ȣ���ؼ� �˷��� 
		virtual void OnError(int ErrorNo, const TCHAR* ErrorMsg) = 0;

		virtual void OnSemError(ULONGLONG SessionID) = 0;


	private:

		////////////////////////////////////////////////////
		// ��Ʈ��ũ ���̺귯�� private ��� �Լ�			
		// ������ �����Ǹ� ������ ������ �ڵ� ȣ��� 
		////////////////////////////////////////////////////

		///////////////////////////////////////////////////////////////////////////////////
		// AcceptThread 
		// ����  : LPVOID Param [ CNetServer�� this ������ ]
		// ��ȯ  : ������ �Լ� ��ȯ 
		// ���  : accept, ���� �Ϸ� �� �������̺� ���� ���
		// ����  : ���� �̺�Ʈ �ڵ鸵 �Լ� OnConnectionRequest, OnClientJoin
		///////////////////////////////////////////////////////////////////////////////////
		static unsigned int WINAPI AcceptThread(LPVOID Param);

		///////////////////////////////////////////////////////////////////////////////////
		// WorkerThread
		// ����  : LPVOID Param [ CNetServer�� this ������ ]
		// ��ȯ  : ������ �Լ� ��ȯ 
		// ���  : I/O�� ����� ��Ŀ ������ 
		// ����  : ���� �̺�Ʈ �ڵ鸵 �Լ� OnRecv, OnSend
		///////////////////////////////////////////////////////////////////////////////////
		static unsigned int WINAPI WorkerThread(LPVOID Param);

		///////////////////////////////////////////////////////////////////////////////////
		// CheckPacket 
		// ����  : NetSession *pSession [ ���� ����ü ������ ]
		// ��ȯ  : bool ( ���Ź��� ��Ŷó���� ���������� true, �̻��� ��Ŷ�� ���޵Ǹ� false)
		// ���  : ��Ʈ��ũ Ŭ���� ������ �ϼ��� ��Ŷ�� �����ϸ� ����� ���� OnRecv�� ����
		// ����  : ���� �̺�Ʈ �ڵ鸵 �Լ� OnRecv
		///////////////////////////////////////////////////////////////////////////////////
		bool CheckPacket(NetSession *pSession);

		///////////////////////////////////////////////////////////////////////////////////
		// SendPost
		// ����1 : NetSession *pSession [ ���� ����ü ������ ]
		// ��ȯ  : ������ true, ���н� false 
		// ���  : WSASend ��� 
		// ����  : SendPacket�� ȣ��, OnSend �� ȣ�� 
		///////////////////////////////////////////////////////////////////////////////////		
		bool SendPost(NetSession *pSession);

		///////////////////////////////////////////////////////////////////////////////////
		// RecvPost
		// ����1 : NetSession *pSession [ ���� ����ü ������ ]
		// ��ȯ  : ������ true, ���н� false 
		// ���  : WSARecv ���
		// ����  : �񵿱� Recv�� �׻� ��ϵǾ� �ִ�.
		///////////////////////////////////////////////////////////////////////////////////		
		bool RecvPost(NetSession *pSession);

		///////////////////////////////////////////////////////////////////////////////////
		// ReleaseSession
		// ����1 : NetSession *pSession [ ���� ����ü ������ ]
		// ��ȯ  : ����
		// ���  : ���� ����
		// ����  : �� �Լ��� I/O Count�� 0�� ���� ȣ��Ǿ�� �Ѵ�.
		///////////////////////////////////////////////////////////////////////////////////	
		void ReleaseSession(NetSession *pSession);

		///////////////////////////////////////////////////////////////////////////////////
		// SessionAcquireLock
		// ����1 : NetSession *pSession [ ���� ����ü ������ ]
		// ��ȯ  : bool (������ true, ���н� false)
		// ���  : ��Ƽ ������ ȯ�濡�� ������ ��ȣ�ϴ� ���
		///////////////////////////////////////////////////////////////////////////////////	
		bool SessionAcquireLock(NetSession * pSession, ULONGLONG LocalSessionID);

		///////////////////////////////////////////////////////////////////////////////////
		// SessionReleaseLock
		// ����1 : NetSession *pSession [ ���� ����ü ������ ]
		// ��ȯ  : ����
		// ���  : ��Ƽ ������ ȯ�濡�� ������ ��ȣ�ϴ� ���
		///////////////////////////////////////////////////////////////////////////////////	
		void SessionReleaseLock(NetSession *pSession);

		// Start�Լ��� �������� ���� �ʱ�ȭ �Լ���
		bool WSAInit(TCHAR * BindIP, USHORT port, bool IsNoDelay);
		bool SessionInit(WORD MaxSessions);
		bool ThreadInit(DWORD ConcurrentThreads, DWORD MaxThreads);

	private:

		////////////////////////////////////////////////////
		// ��Ʈ��ũ ���̺귯�� private ��� ����		
		////////////////////////////////////////////////////

		// ���� �ּ� ����ü
		SOCKADDR_IN           m_ServerAddr;	

		// ��Ŀ������� ����� IOCP�� �ڵ� [ ���ý��� ������ ����, ���� ������ ���� ����ڰ� ���� ]
		HANDLE                m_hWorkerIOCP = NULL;

		// ��Ŀ������ �ڵ� �迭 [������ ������ ����ڰ� ����] 
		HANDLE               *m_ThreadHandleArr = nullptr;

		// ��Ŀ������ ���̵� �迭 [������ ������ ����ڰ� ����]
		DWORD                *m_ThreadIDArr = nullptr;

		// ���� ���� �迭		
		NetSession           *m_SessionArray = nullptr;

		// ���� ���԰����� �迭�� �ε����� �����ϰ� �ִ� ����
		CLockFreeStack<WORD> *m_NextSlot;

		// ���� ���� 
		SOCKET                m_Listen = INVALID_SOCKET;

		// ���� �� ���� ���� �� 
		ULONGLONG             m_SessionCount = 0;

		// �ִ� ���� ���� Ŭ���̾�Ʈ�� �� 
		DWORD                 m_MaxSessions = 0;

		// ������ �������� ���� 
		DWORD                 m_IOCPThreadsCount = 0;	

		// ���� ��������
		OVERLAPPED            m_DummyOverlapped;
	};
}

#endif




