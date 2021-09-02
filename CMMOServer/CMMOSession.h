#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <WS2tcpip.h>
#include <winsock2.h>
#include <Mstcpip.h>
#include "RingBuffer\RingBuffer.h"
#include "Packet\Packet.h"
#include "LockFreeQueue\LockFreeQueue.h"
#include "ListQueue\ListQueue.h"
#include "Define\GGM_CONSTANTS.h"
#pragma  comment(lib, "Ws2_32.lib")

namespace GGM
{
	///////////////////////////////////////////////////////////////////////////////////////////
	// ������ ���� ��� �����带 �������� ��� 
	// ���ǿ� �����ϴ� ���� �ش� ����� ������ (Auth, Game ���� �ϳ�) + SendThread
	// NONE >> AUTH >> GAME >> LOGOUT �� �ܹ������� ��带 ���������μ� ����ȭ������ �����ϰ� �� 
	///////////////////////////////////////////////////////////////////////////////////////////
	enum class SESSION_MODE
	{
		MODE_NONE = 0,
		MODE_AUTH,
		MODE_AUTH_TO_GAME,
		MODE_GAME,		
		MODE_WAIT_LOGOUT
	};

	/////////////////////////////////////////////////////////////////////////////////////////////////
	// CMMOServer�� ��Ʈ��ũ ������ ���� ������ �Ǵ� CMMOSession Ŭ����
	// ���� ��Ʈ��ũ ó���� ���� ������� ��� �̰��� ��ġ��
	// CMMOServer�� ��ӹ��� ���Ӽ����� ���ÿ� CMMOSession�� ��ӹ޾� �÷��̾ ���� �� ����
	// ��Ʈ��ũ ����� ���� ���� �ڷᱸ���� ������ ����� �÷��̾� ���� �ڷᱸ���� �и����� �ʴ°��� Ư¡
	// ���ǰ� �÷��̾ ��Ī��Ű�� SessionID�� Ȱ���� �˻��� �����ϰ� �ٷ� ����, ����� �� �ֵ��� �� 
	/////////////////////////////////////////////////////////////////////////////////////////////////
	class CMMOSession
	{
	public:

		CMMOSession() = default;
		virtual ~CMMOSession() = default;

		///////////////////////////////////////////////////////////////////////////////////////
		// �ܺο��� ��Ʈ��ũ ��⿡ ��Ŷ �۽�, ���� ����, ��� ���� ��û         
		///////////////////////////////////////////////////////////////////////////////////////

		// �ش� ���ǿ��� ��Ŷ �۽� ��û 
		void SendPacket(CNetPacket *pPacket);

		// �ش� ���ǿ��� ��Ŷ �۽� �� ���� ���� ��û
		void SendPacketAndDisconnect(CNetPacket *pPacket);

		// �ش� ���� ���� ���� ��û
		void Disconnect();

		// �ش� ������ ��带 Auth���� Game���� ����
		// �ܺο��� ���� ������ ��带 �����ϴ� ���� �����Ѵ�. 
		void SetAuthToGame();		

	protected:

		///////////////////////////////////////////////////////////////////////////////////////
		// �̺�Ʈ �ڵ鸵�� ���� ���� �Լ�
		// CMMOSession�� ��ӹ��� ������ ����� �÷��̾�� �Ʒ��� �Լ����� ����
		///////////////////////////////////////////////////////////////////////////////////////

		// Auth Thread 
		virtual void OnAuth_ClientJoin() = 0;
		virtual void OnAuth_ClientLeave(bool IsLogOut) = 0;
		virtual void OnAuth_ClientPacket(CNetPacket *pPacket) = 0;

		// Game Thread 
		virtual void OnGame_ClientJoin() = 0;
		virtual void OnGame_ClientLeave() = 0;
		virtual void OnGame_ClientPacket(CNetPacket *pPacket) = 0;

		// Release
		virtual void OnClientRelease() = 0;	

		virtual void OnError(int ErrorNo, const TCHAR* ErrorMsg) = 0;

		friend class CMMOServer;

	private:		

		///////////////////////////////////////////////////////////////////////////////////
		// DecreasIoCount
		// ����  : ����
		// ��ȯ  : ����
		// ���  : IoCount 1 ���� >> 0�̸� �ش� �α׾ƿ� �÷��� �� 		
		///////////////////////////////////////////////////////////////////////////////////	
		void DecreaseIoCount();

	private:

		///////////////////////////////////////////////////////////////////////////////////////
		// ��Ʈ��ũ ��⸸�� �����ϴ� �κ�
		///////////////////////////////////////////////////////////////////////////////////////

		volatile LONG               m_IoCount = 0;
		bool			            m_IsSending = false;		
		bool                        m_IsCanceled = false;
		bool                        m_IsSendAndDisconnect = false; // ������ ���� �÷��� 
		bool                        m_IsLogout = true; // �α׾ƿ� �÷���
		bool                        m_IsAuthToGame = false; // Auth �����忡�� Game ������� �̵� �÷���, �ܺο��� ��û 
		ULONG   					m_SentPacketCount = 0; // SendPost ���ο��� Send��û�� ��Ŷ ����		
		CNetPacket                 *m_pDisconnectPacket = nullptr; // ������ ���⿡ �ش��ϴ� ��Ŷ		
		SESSION_MODE                m_Mode = SESSION_MODE::MODE_NONE;	
		SOCKET                      m_socket;
		ULONGLONG                   m_HeartbeatTime;
		ULONGLONG                   m_SessionID;
		USHORT                      m_SessionSlot;				
		OVERLAPPED                  m_RecvOverlapped;
		OVERLAPPED                  m_SendOverlapped;
		CRingBuffer                 m_RecvQ;
		CListQueue<CNetPacket*>     m_SendQ;
		CListQueue<CNetPacket*>     m_PacketQ;
		CNetPacket                 *m_PacketArray[MAX_PACKET_COUNT]; // �Ϸ������ÿ� Free�� ��Ŷ�� �迭
		IN_ADDR					    m_SessionIP;
		USHORT                      m_SessionPort;

	};
}

