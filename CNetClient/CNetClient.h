#ifndef _C_NET_CLIENT_H_
#define _C_NET_CLIENT_H_

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <WS2tcpip.h>
#include <winsock2.h>
#include "RingBuffer\RingBuffer.h"
#include "SerialBuffer\SerialBuffer.h"
#include "MemoryPool\MemoryPool.h"
#include "LockFreeStack\LockFreeStack.h"
#include "LockFreeQueue\LockFreeQueue.h"
#include "Define\GGM_CONSTANTS.h"
#pragma  comment(lib, "Ws2_32.lib")

namespace GGM
{
	////////////////////////////////////////////////////
	// ���� ����ü Session			
	////////////////////////////////////////////////////
	struct Session
	{
		SOCKET							socket = INVALID_SOCKET;
		LONG			                IoCount = 0;
		LONG							IsReleased = FALSE;
		ULONGLONG                       SessionID = 0;
		ULONG   					    SentPacketCount = 0; // SendPost ���ο��� Send��û�� ��Ŷ ����							         
		OVERLAPPED		                RecvOverlapped; // ���ſ� OVERLAPPED ����ü
		OVERLAPPED		                SendOverlapped; // �۽ſ� OVERLAPPED ����ü		
		CRingBuffer		                RecvQ; // ���ſ� ������				 
		CLockFreeQueue<CNetPacket*>     SendQ; // �۽ſ� ����ȭ ������ �����͸� ���� ������ ť
		CNetPacket  		            *PacketArray[MAX_PACKET_COUNT]; // SendQ���� �̾Ƴ� ����ȭ ������ �����͸� ���� �迭
		LONG			                IsSending = FALSE; // ���� �÷��� 										
	};

	///////////////////////////////////////////////////////////////////
	// �ܺ� ��Ʈ��ũ�� Ŭ���̾�Ʈ�� ��Ʈ��ũ ������ CNetClient
	// - IOCP ���
	// - Ŭ���� ���ο� ��Ŀ�����带 ����
	// - �̺�Ʈ �ڵ鸵 �Լ��� ���������Լ��� �����Ͽ� �ܺο��� ��ӹ޾� ����
	// - ���ο��� ��ü������ ������ ������
	///////////////////////////////////////////////////////////////////
	class CNetClient
	{
	public:

		/////////////////////////////////////////////////////////////////
		// * ��Ʈ��ũ ���̺귯�� ���� �����Լ�
		// * ��� �̺�Ʈ �ڵ鸵 �Լ��� ���������Լ��μ� ��ӹ޴� �ʿ��� �����Ѵ�.
		/////////////////////////////////////////////////////////////////		

		// NetServer���� ������ ������ ���� ȣ�� 
		virtual void OnConnect() = 0;

		// �� ���ǿ� ���� I/O Count�� �����ǰ� ���� �� ���ҽ��� �����Ǿ��� �� ȣ��ȴ�. 
		virtual void OnDisconnect() = 0;

		// ��Ŷ ���� �Ϸ� �� ȣ��
		virtual void OnRecv(CNetPacket *Packet) = 0;

		// ��Ŷ �۽� �Ϸ� �� ȣ��
		virtual void OnSend(int SendSize) = 0;

		// ��Ŀ������ GQCS �ٷ� �ϴܿ��� ȣ�� 
		virtual void OnWorkerThreadBegin() = 0;

		// ��Ŀ������ 1���� ���� �� ȣ��
		virtual void OnWorkerThreadEnd() = 0;

		// ���������� ����Ǵ� �Լ����� ������ �߻��� �� �� �Լ��� ȣ���ؼ� �˷��� 
		virtual void OnError(int ErrorNo, const TCHAR* ErrorMsg) = 0;

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
		CNetClient() = default;

		///////////////////////////////////////////////////////////////////////////////////
		// �Ҹ��� 
		// ����1 : ����
		// ��ȯ  : ����
		// ���  : Stop �Լ����� ��� ������ ������ ���̹Ƿ� �Ҹ��ڿ��� ������ ����.
		// ����  : 
		///////////////////////////////////////////////////////////////////////////////////
		virtual ~CNetClient() = default;

		///////////////////////////////////////////////////////////////////////////////////
		// Start 
		// ����1 : TCHAR* ConnectIP [ ������ NetServer IP ]
		// ����2 : USHORT port [ ������ NetServer port ] 
		// ����3 : DWORD ConcurrentThreads [ IOCP�� ���ÿ� ������ ������ ���� ] 
		// ����4 : DWORD MaxThreads [ ������ �ִ� ������ ���� ]
		// ����5 : bool IsNagleOn [ TCP_NODELAY �ɼ� ON/OFF ���� ]	
		// ��ȯ  : ������ true, ���н� false Ȥ��, �����ڵ�(WSAStartup ������)
		// ���  : NetClient ���� �� �ʱ�ȭ
		// ����  : �ܺο��� NetClient ���� ������ ȣ���Ѵ�.
		///////////////////////////////////////////////////////////////////////////////////
		bool Start(
			TCHAR* ConnectIP,
			USHORT port,
			DWORD ConcurrentThreads,
			DWORD MaxThreads,
			bool  IsNoDelay,
			bool  IsReconnect,
			int   ReconnectDelay,
			BYTE PacketCode,
			BYTE PacketKey1			
		);

		///////////////////////////////////////////////////////////////////////////////////
		// Stop 
		// ����1 : ����
		// ��ȯ  : ����
		// ���  : NetClient ���� ����, ��� ���� ���� ���ҽ� ����		
		///////////////////////////////////////////////////////////////////////////////////
		void Stop();

		///////////////////////////////////////////////////////////////////////////////////
		// Disconnect		
		// ��ȯ  : true [ ���� ] // false [ ���� ]	
		// ����  : �ܼ� ���� ����, ���� ���ҽ��� �Ϻ��ϰ� �����ϴ� ���� ReleaseSession �Լ���
		///////////////////////////////////////////////////////////////////////////////////
		bool Disconnect();

		///////////////////////////////////////////////////////////////////////////////////
		// SendPacket		
		// ����  : CNetPacket *Packet [ ��Ŷ ������ ��� ����ȭ ������ ������ ]
		// ��ȯ  : true [ ���� ] // false [ ���� ]
		// ���  : NetServer���� ��Ŷ�� ������ �� ���				
		///////////////////////////////////////////////////////////////////////////////////
		bool SendPacket(CNetPacket *Packet);

	private:

		////////////////////////////////////////////////////
		// ��Ʈ��ũ ���̺귯�� private ��� �Լ�	
		// ������ �����Ǹ� ������ ������ �ڵ� ȣ��� 
		////////////////////////////////////////////////////	

		///////////////////////////////////////////////////////////////////////////////////
		// WorkerThread  : ������ �Լ��� ����ؾ��ϹǷ� static
		// ����1 : LPVOID Param [ CNetClient�� this ������ ]
		// ��ȯ  : ������ �Լ� ��ȯ 
		// ���  : I/O�� ����� ��Ŀ ������ 
		// ����  : ���� �̺�Ʈ �ڵ鸵 �Լ� OnRecv, OnSend
		///////////////////////////////////////////////////////////////////////////////////
		static unsigned int WINAPI WorkerThread(LPVOID Param);

		///////////////////////////////////////////////////////////////////////////////////
		// CheckPacket 
		// ����  : ����
		// ��ȯ  : CNetClient ������ ����ϴ� RecvQ�� �ּ�
		// ���  : ��Ʈ��ũ Ŭ���� ������ �ϼ��� ��Ŷ�� �����ϸ� ����� ���� OnRecv�� ����
		// ����  : ���� �̺�Ʈ �ڵ鸵 �Լ� OnRecv
		///////////////////////////////////////////////////////////////////////////////////
		bool CheckPacket(CRingBuffer *pRecvQ);

		///////////////////////////////////////////////////////////////////////////////////
		// SendPost
		// ����  : ����
		// ��ȯ  : ������ true, ���н� false 
		// ���  : WSASend ��� 
		// ����  : SendPacket�� ȣ��, OnSend �� ȣ�� 
		///////////////////////////////////////////////////////////////////////////////////		
		bool SendPost(
			LONG                           *pIsSending,
			CLockFreeQueue<CNetPacket*>    *pSendQ,
			LPOVERLAPPED                    pOverlapped,
			CNetPacket                    **PacketArray,
			ULONG                          *pSentPacketCount,
			LONG				           *pIoCount,
			SOCKET                          sock
		);

		///////////////////////////////////////////////////////////////////////////////////
		// RecvPost
		// ����  : ����
		// ��ȯ  : ������ true, ���н� false 
		// ���  : WSARecv ���
		// ����  : �񵿱� Recv�� �׻� ��ϵǾ� �ִ�.
		///////////////////////////////////////////////////////////////////////////////////		
		bool RecvPost(
			CRingBuffer  *pRecvQ,
			LPOVERLAPPED  pOverlapped,
			LONG         *pIoCount,
			SOCKET        sock
		);

		///////////////////////////////////////////////////////////////////////////////////
		// ReleaseSession
		// ����  : ����
		// ��ȯ  : ����
		// ���  : ���� ����
		// ����  : �� �Լ��� I/O Count�� 0�� ���� ȣ��Ǿ�� �Ѵ�.
		///////////////////////////////////////////////////////////////////////////////////	
		void ReleaseSession();

		///////////////////////////////////////////////////////////////////////////////////
		// SessionAcquireLock
		// ����  : ULONGLONG LocalSessionID [ �ش� ������ �������� �õ��� ������ ���� ���̵� ] 
		// ��ȯ  : bool (������ true, ���н� false)
		// ���  : ��Ƽ ������ ȯ�濡�� ������ ��ȣ�ϴ� ���� ȹ�� (���Ͷ�)		
		///////////////////////////////////////////////////////////////////////////////////	
		bool SessionAcquireLock(ULONGLONG LocalSessionID);

		///////////////////////////////////////////////////////////////////////////////////
		// SessionReleaseLock
		// ����1 : Session *pSession [ ���� ����ü ������ ]
		// ��ȯ  : bool (������ true, ���н� false)
		// ���  : ��Ƽ ������ ȯ�濡�� ������ ��ȣ�ϴ� ���� ��ȯ (���Ͷ�)	
		///////////////////////////////////////////////////////////////////////////////////	
		void SessionReleaseLock();

		///////////////////////////////////////////////////////////////////////////////////
		// CreateSocket
		// ����  : ����
		// ��ȯ  : bool (������ true, ���н� false)
		// ���  : ���� ����
		///////////////////////////////////////////////////////////////////////////////////	
		bool CreateSocket();

		///////////////////////////////////////////////////////////////////////////////////
		// Connect
		// ����  : ����
		// ��ȯ  : bool (������ true, ���н� false)
		// ���  : LanServer�� ����
		///////////////////////////////////////////////////////////////////////////////////	
		bool Connect();

	private:

		////////////////////////////////////////////////////
		// ��Ʈ��ũ ���̺귯�� private ��� ����		
		////////////////////////////////////////////////////

		// ���� ���� ����
		Session m_MySession;

		// NO_DELAY �ɼ�
		bool    m_IsNoDelay;

		// �翬�� �ɼ�
		bool    m_IsReconnect;

		// �翬�� ������
		int     m_ReconnectDelay;

		// start �Լ� ȣ�� ����		
		LONG    m_IsStarted = FALSE;

		// CLanServer�� �ּ� ����ü
		SOCKADDR_IN m_ServerAddr;

		// ��Ŀ������� ����� IOCP�� �ڵ� [ ���ý��� ������ ����, ���� ������ ���� ����ڰ� ���� ]
		HANDLE  m_hWorkerIOCP = NULL;

		// ��Ŀ������ �ڵ� �迭 [������ ������ ����ڰ� ����] 
		HANDLE *m_ThreadHandleArr = nullptr;

		// ��Ŀ������ ���̵� �迭 [������ ������ ����ڰ� ����]
		DWORD *m_ThreadIDArr = nullptr;

		// ������ �������� ���� 
		DWORD     m_IOCPThreadsCount = 0;
	};
}

#endif

