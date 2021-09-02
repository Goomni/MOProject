#pragma once
#include <Windows.h>
#include "MemoryPool\MemoryPool.h"

/////////////// ERROR CODE ///////////////// 
constexpr auto GGM_PACKET_ERROR = -1;
constexpr auto GGM_ERROR_SERIAL_BUFFER_OVERFLOW = 1;
constexpr auto GGM_ERROR_SERIAL_PACKET_FRAGMENT = 2;
/////////////// EXCEPTION CODE /////////////////
constexpr auto DEFAULT_BUFFER_SIZE = 512;

namespace GGM
{
	//////////////////////////////////////////////////////////////////////////////
	// LAN SERVER�� ��� 
	// ���� ���� ��Ʈ��ũ���� ��ſ� ����� �뵵�̹Ƿ� �ִ��� �ܼ��� ���������� �����
	// ��ȣȭ �� ��Ÿ ��ġ�� ���� �ʰ� ������ ����� ���̷ε��� ����� ÷��
	//////////////////////////////////////////////////////////////////////////////
	struct LAN_HEADER
	{
		WORD size;
	};

	/////////////////////////////////////////////////////////////////////////////////////////
	// NET SERVER�� ��� 
	// ������ Ŭ���̾�Ʈ���� ��ſ� ���ǹǷ� Lan Server�� �޸� ��ȣȭ ���� ��ġ�� ���Ե�		
	/////////////////////////////////////////////////////////////////////////////////////////

#pragma pack(push,1)
	struct NET_HEADER
	{
		BYTE PacketCode;
		WORD Length;
		BYTE RandKey;
		BYTE CheckSum;
	};
#pragma pack(pop)

	/////////////////////////////////////////////////////
	// �⺻ ��Ŷ 
	/////////////////////////////////////////////////////	
	class CPacket
	{
	public:
		//////////////////////////////////////////////////
		// public static �Լ�
		//////////////////////////////////////////////////

		// ���� ������ ��Ŷ Ǯ ����
		static bool CreatePacketPool(int NumOfMemBlock);
		static void DeletePacketPool();

		// �ܺο��� ����ȭ ������ �����͸� �����Ҵ�ޱ� ���ؼ� ��� 
		// ��𼭳� ȣ���� �� �ְ� static���� ����
		static CPacket* Alloc();

		// �����Ҵ��� ����ȭ ������ ����� �������� Freeȣ��
		// Free ȣ������ ����ī��Ʈ ��ȯ, �Ҵ����� �Ǿ��ٸ� 0��ȯ
		// ��𼭳� ȣ���� �� �ְ� static���� ����
		static LONG           Free(CPacket *pPacket);

		//////////////////////////////////////////////////
		// public �Լ�
		//////////////////////////////////////////////////
		CPacket(BYTE HeaderSize = sizeof(LAN_HEADER));
		virtual ~CPacket() = default;

		void                  AddRefCnt();

		// ����ī��Ʈ�� ���� ��Ų��. ( �ѹ��� 1�̻��� ���� )
		void                  AddRefCnt(LONG Addend);

		int				GetBufferMemSize() const; // ������ �� �޸�ũ�⸦ ��´�. (�����Ҵ��� �޸𸮿뷮)
		int				GetCurrentUsage() const; // ���� ������ ��뷮�� ��´�.
		int				GetCurrentSpare() const; // ���� ���ۿ� ���� �뷮�� ��´�.	

		int				Enqueue(char *pData, int iSize); // size��ŭ ���ۿ� ������ �ִ´�.
		void			EnqueueHeader(char *pHeader);
		int				Dequeue(char *pDestBuf, int iSize); // size��ŭ ���۷κ��� ������ �̾Ƴ���. (������ ����)
		int				Peek(char *pDestBuf, int iSize); // size��ŭ ���۷κ��� ������ �̾Ƴ���. (������ ����)
		void			EraseData(int iSize);
		void			RelocateWrite(int iSize);

		const char*	    GetBufferPtr() const;
		const char*	    GetReadPtr() const; // ���� front�� �ּҰ� ��ȯ	
		const char*	    GetWritePtr() const; // ���� rear�� �ּҰ� ��ȯ		

		bool        IsEmpty() const;
		bool        IsFull() const;
		void        InitBuffer();	

		int         GetPacketError() const;

		//////////////////////// ������ �����ε� /////////////////////////////
		template <typename T>
		CPacket& operator<<(const T &Value);

		template <typename T>
		CPacket& operator>>(const T &Value);		

	protected:		
		char      m_pSerialBuffer[DEFAULT_BUFFER_SIZE];
		int       m_BufferMemSize = DEFAULT_BUFFER_SIZE;
		int	      m_Front = 0;
		int       m_Rear;
		int		  m_InitRear;
		int       m_Error = 0;	
		LONG	  m_RefCnt; // �ش� ����ȭ ������ ���� ī��Ʈ			
	public:
		// ��Ŷ Ǯ
		static    CTlsMemoryPool<CPacket> *PacketPool;	
	};

	template <typename T>
	inline CPacket& CPacket::operator<<(const T &Value)
	{
		Enqueue((char*)&Value, sizeof(Value));
		return *this;
	}

	template<typename T>
	inline CPacket& CPacket::operator>>(const T &Value)
	{
		Dequeue((char*)&Value, sizeof(Value));
		return *this;
	}

////////////////////////////////////////////////////////////////////////////////////////////////////////

	/////////////////////////////////////////////////////
	// NetServer ��Ŷ : CPacket ��� ( NetServer ����)
	/////////////////////////////////////////////////////
	
	constexpr auto NET_HEADER_SIZE = 5;

	class CNetPacket : public CPacket
	{
	public:

		//////////////////////////////////////////////////
		// public static �Լ�
		//////////////////////////////////////////////////

		// ���� ������ ��Ŷ Ǯ ����
		static bool           CreatePacketPool(int NumOfPacketChunk);
		static void           DeletePacketPool();

		// ���� ������ ��Ŷ �ڵ� �� XOR Ű ����
		static void           SetPacketCode(BYTE Code, BYTE FixedKey);

		// �ܺο��� ����ȭ ������ �����͸� �����Ҵ�ޱ� ���ؼ� ��� 
		// ��𼭳� ȣ���� �� �ְ� static���� ����
		static CNetPacket*    Alloc();

		// �����Ҵ��� ����ȭ ������ ����� �������� Freeȣ��
		// Free ȣ������ ����ī��Ʈ ��ȯ, �Ҵ����� �Ǿ��ٸ� 0��ȯ
		// ��𼭳� ȣ���� �� �ְ� static���� ����
		static LONG           Free(CNetPacket *pPacket);

		//////////////////////////////////////////////////
		// public �Լ�
		//////////////////////////////////////////////////

		// �����ڿ� �Ҹ���
		CNetPacket(BYTE HeaderSize = sizeof(NET_HEADER));
		virtual ~CNetPacket() = default;

		// ���� �ʱ�ȭ
		void        InitBuffer();

	public:

		//////////////////////////////////////////////////
		// private �Լ�
		//////////////////////////////////////////////////

		// ��Ŷ �۽� ��, ��ȣȭ ó�� �� Net Server ������ ��� ����
		void Encode();

		// ��Ŷ ���� ��, ��ȣȭ ó�� 
		bool Decode(NET_HEADER *pHeader);

	private:

		// ���ڵ� ���� �÷���
		bool      m_IsEncoded = false;

		// 1byte ��Ŷ �ڵ�
		static    BYTE      PacketCode;

		// 1byte ���� XOR
		static    BYTE      PacketKey;		

		// ��Ŷ Ǯ ( ����͸� �������� �ۺ����� ǰ )
	public:
		static    CTlsMemoryPool<CNetPacket> *PacketPool;	

		friend class CNetServer;
		friend class CNetClient;
		friend class CMMOSession;
		friend class CMMOServer;
	};

}

