#pragma once
#include "CNetServer\CNetServer.h"
#include "ConfigLoader\ConfigLoader.h"
#include <unordered_map>

namespace GGM
{
	// ä�� ������ ���	
	constexpr auto ID_LEN = 20;
	constexpr auto NICK_LEN = 20;
	constexpr auto SESSIONKEY_LEN = 64;
	constexpr auto SECTOR_HEIGHT = 50;
	constexpr auto SECTOR_WIDTH = 50;	
	constexpr auto NUM_CHAT_WORK_TYPE = 3;
	constexpr auto NUM_CHAT_PACKET_TYPE = 7;	
	constexpr auto MAX_WORKLOAD = 5000000;	
	constexpr auto CHAT_MSG_LEN = 512;	
	constexpr auto TOKEN_ERASE_PERIOD = 60000;

	// ���� �������� 
	// �ܺ� CONFIG ���Ϸκ��� �ļ��� �̿��ؼ� ������ �´�.
	class NetServerChatConfig : public CConfigLoader
	{
	public:

		TCHAR  BindIP[16];
		USHORT Port;
		DWORD  ConcurrentThreads;
		DWORD  MaxThreads;
		bool   IsNoDelay;
		WORD   MaxSessions;
		BYTE   PacketCode;
		BYTE   PacketKey1;
		BYTE   PacketKey2;
		int    LogLevel;

	public:

		virtual bool LoadConfig(const TCHAR* ConfigFileName) override;

	};

	// �ϰ� Ÿ��
	enum CHAT_WORK_TYPE
	{
		PACKET = 0,
		JOIN = 1, 		
		LEAVE = 2		
	};

	// ������Ʈ ������� �Ѱ��ִ� �ϰ��� ����
	struct CHAT_WORK
	{
		BYTE       type;
		ULONGLONG  SessionID;
		CNetPacket *pPacket;
	};
	
	// ä�� ���� ���� ��� �÷��̾� ��ü
	struct Sector;	
	struct Player
	{
		ULONGLONG SessionID = INIT_SESSION_ID; // �÷��̾� �ĺ��� 
		INT64     AccountNo; // ������ȣ		
		TCHAR     ID[ID_LEN]; // �÷��̾��� ���̵�� �г���
		TCHAR     Nickname[NICK_LEN];
		Sector   *pSector = nullptr; // ���� �÷��̾ ���� ����
		WORD      SectorIdx; // ���� �÷��̾ ���� ������ �迭���� �÷��̾��� ��ġ
		bool      IsLogIn = false;		
	};

	///////////////////////////////////////////////////////
	// ���� ����ü
	// ä���� �Է��� Ŭ���̾�Ʈ�� �ֺ� ���� 9���� ä���� ������
	///////////////////////////////////////////////////////
	struct Sector
	{
		Player **SectorArr = nullptr;
		size_t   SectorSize = 0;

		// ���� ���� �������� ������ ��ȿ�� ������ ������ �� ������
		// �ڱ��ڽ� + �ֺ� 8��, �ִ� 9���� ��ȿ���Ͱ� ����
		BYTE     ValidCount = 0;
		Sector*  ValidSector[9];

		WORD     SectorX; // ���� ��ǥ
		WORD     SectorY;		
	};

	// ���漱��

	// �α��� ������ �����ϱ� ���� LanClient�� Config
	class CLanClientChat;
	class LanClientChatConfig;

	// CLanClientChat���� ����ϴ� ������ū
	struct Token;

	// ����͸� ������ �����ϱ� ���� LanClient�� Config
	class CLanClientMonitor;
	class LanClientMonitorConfig;	

	/////////////////////////////////////
	// ä�ü��� : NetServer�� ��ӹ���
	/////////////////////////////////////
	class CNetServerChat : public CNetServer
	{
	public:

		CNetServerChat() = delete;
		CNetServerChat(
			NetServerChatConfig *pNetConfig, 
			LanClientChatConfig *pLanChatConfig, 
			LanClientMonitorConfig *pLanMonitorConfig
		);

		virtual ~CNetServerChat();

		// ä�ü��� ����� 
		void StopChatServer();		

		// ����͸� ���� ���
		void PrintInfo();

		// �α����� ���� ��				
		size_t GetLoginPlayer() const;

	protected:

		//////////////////////////////////////////////
		// ���� �̺�Ʈ �ڵ鸵 �Լ�
		//////////////////////////////////////////////

		// Accept ���� �� ������ Ȯ���Ǹ� ȣ�� 
		virtual void OnClientJoin(const SOCKADDR_IN& ClientAddr, ULONGLONG SessionID) override;

		// �ش� ���ǿ� ���� ��� ���ҽ��� �����Ǹ� ȣ��  
		virtual void OnClientLeave(ULONGLONG SessionID) override;

		// Accept ���� ȣ�� 
		virtual bool OnConnectionRequest(const SOCKADDR_IN& ClientAddr) override;

		// ��Ŷ ���� �Ϸ� �� ȣ��
		virtual void OnRecv(ULONGLONG SessionID, CNetPacket *pPacket) override;

		// ��Ŷ �۽� �Ϸ� �� ȣ��
		virtual void OnSend(ULONGLONG SessionID, int SendSize) override;

		// ��Ŀ������ GQCS �ٷ� �ϴܿ��� ȣ�� 
		virtual void OnWorkerThreadBegin() override;

		// ��Ŀ������ 1���� ���� �� ȣ��
		virtual void OnWorkerThreadEnd() override;

		// ���������� ����Ǵ� �Լ����� ������ �߻��� �� �� �Լ��� ȣ���ؼ� �˷��� 
		virtual void OnError(int ErrorNo, const TCHAR* ErrorMsg) override;

		///////////////////////////////////////////////////////////
		// UpdateThread 
		// ����  : LPVOID Param [ this ������ ]
		// ��ȯ  : ������ �Լ� ��ȯ 
		// ���  : ä�ü����� ���� ������Ʈ ������
		///////////////////////////////////////////////////////////
		static unsigned int __stdcall UpdateThread(LPVOID Param);

		///////////////////////////////////////////////////////////
		// TokenThread 
		// ����  : LPVOID Param [ this ������ ]
		// ��ȯ  : ������ �Լ� ��ȯ 
		// ���  : �ֱ������� ������ ���� ��ū�� �����Ѵ�.
		///////////////////////////////////////////////////////////
		static unsigned int __stdcall TokenThread(LPVOID Param);

		/////////////////////////////////////////////////////////////
		// On [CHAT_WORK_TYPE] Proc 
		// ����  : ����
		// ��ȯ  : ���� ó���� true ���н�, false 
		// ���  : NetServer�κ��� ���� �ϰ��� Ÿ�Ժ��� ó��
		/////////////////////////////////////////////////////////////

		using CHAT_WORK_PROC = bool(CNetServerChat::*)(CHAT_WORK*);
		bool OnJoinProc(CHAT_WORK*);
		bool OnRecvProc(CHAT_WORK*);
		bool OnLeaveProc(CHAT_WORK*);

		/////////////////////////////////////////////////////////////
		// Req [PACKET_TYPE] Proc 
		// ����  : CNetPacket* [ ������ ������ ��Ŷ ������ ]
		// ��ȯ  : ���� ó���� true ���� ��, false 
		// ���  : ��ŶŸ�Ժ� ��Ŷó��
		/////////////////////////////////////////////////////////////

		// CHAT_PACKET_PROC�� �Լ�������		
		using CHAT_PACKET_PROC = void(CNetServerChat::*)(CNetPacket*, Player*);
		void ReqLoginProc(CNetPacket* pPacket, Player* pPlayer);
		void ReqSectorMoveProc(CNetPacket* pPacket, Player* pPlayer);
		void ReqMsgProc(CNetPacket* pPacket, Player* pPlayer);
		void ReqHeartbeatProc(CNetPacket* pPacket, Player* pPlayer);

		/////////////////////////////////////////////////////////////
		// Res [PACKET_TYPE] Proc 			
		// ���  : Req ��Ŷ�� ���� Res ��Ŷ ó��
		/////////////////////////////////////////////////////////////
		void ResLoginProc(ULONGLONG SessionID, BYTE Result, INT64 AccountNo);
		void ResSectorMoveProc(ULONGLONG SessionID, INT64 AccountNo, WORD SectorX, WORD SectorY);
		void ResMsgProc(ULONGLONG SessionID, INT64 AccountNo, Player* pPlayer, WORD MsgLen, TCHAR *Msg);

		/////////////////////////////////////////////////////////////
		// CreatePacket [PACKET_TYPE] 	
		// ���  : Req ��Ŷ�� ���� Res ��Ŷ ���� 
		/////////////////////////////////////////////////////////////	
		void CreatePacketResLogin(CNetPacket* pPacket, BYTE Result, INT64 AccountNo);
		void CreatePacketResSectorMove(CNetPacket* pPacket, INT64 AccountNo, WORD SectorX, WORD SectorY);
		void CreatePacketResMsg(CNetPacket*, INT64 AccountNo, TCHAR *ID, TCHAR *NickName, WORD MsgLen, TCHAR *Msg);

		/////////////////////////////////////////////////////////////
		// InitSector
		// ����  : ����
		// ��ȯ  : ����
		// ���  : ���� �ʱ�ȭ 
		/////////////////////////////////////////////////////////////
		void InitSector();

		////////////////////////////////////////////////////////////////////
		// ExitFunc
		// ����  : ����
		// ��ȯ  : ����
		// ���  : ����� ���� �Լ� ������Ʈ �����忡 �Ҵ�� APC Queue�� ��
		///////////////////////////////////////////////////////////////////
		static void __stdcall NetChatExitFunc(ULONG_PTR Param);		

	protected:

		// ��� �÷��̾ �������� ������ �ڷᱸ��
		Player                                 *m_PlayerArr = nullptr;
		size_t                                  m_PlayerCount = 0;		
		size_t                                  m_LoginPlayer = 0;

		// ä�ü����� ����ϴ� �Ѱ��� ���� �ǹ��ϴ� ������ ����, 2���� �迭�� ����
		Sector                                 m_GameMap[SECTOR_HEIGHT][SECTOR_WIDTH];		

		// �ϰ� �Ҵ����� �޸� Ǯ 
		CTlsMemoryPool<CHAT_WORK>              *m_pWorkPool;

		// ��Ʈ��ũ ������ ��Ŀ�����尡 ������ ������ ������Ʈ �����忡�� �ϰ��� ������ ť		
		CLockFreeQueue<CHAT_WORK*>             m_WorkQ;

		// ť�� ���� ������ ������(NetServer�� ������)�� �Һ��� ������(ChatServer)�� ��ſ� �̺�Ʈ ��ü
		HANDLE                                 m_WorkEvent;	

		// Update ������ �ڵ�
		HANDLE                                 m_hUpdateThread;	

		// ��ū ���� ������ �ڵ�           
		// ���� ��ū�� �������� �����ð� ä�ü����� ������ �ʴ� ������ ��ū ����ó�� 
		HANDLE                                 m_hTokenCollector;

		// LanClientChat �� ������
		// �α��� ������ ����ϴµ� ����Ѵ�.
		CLanClientChat                        *m_pLanChat;

		// ���� ��ū �����
		std::unordered_map<INT64, Token*>     m_TokenTable;

		// ��ū ���̺� ��
		SRWLOCK                               m_lock;

		// ��ū �Ҵ����� �޸� Ǯ 
		CTlsMemoryPool<Token>                 *m_pTokenPool;

		// LanClientMonitor�� ������
		CLanClientMonitor                     *m_pLanMonitor;		

		// CHAT_WORK_TYPE (Join, Leave, Packet)�� ���� ���ν���
		CHAT_WORK_PROC                         m_ChatWorkProc[NUM_CHAT_WORK_TYPE];

		// CHAT_PACKET_TYPE�� ���� ���ν���
		CHAT_PACKET_PROC                       m_PacketProc[NUM_CHAT_PACKET_TYPE+1];
	
	};
}

