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
	// LAN SERVER의 헤더 
	// 오직 내부 네트워크간의 통신에 사용할 용도이므로 최대한 단순한 프로토콜을 사용함
	// 암호화 및 기타 장치를 두지 않고 오로지 헤더에 페이로드의 사이즈만 첨부
	//////////////////////////////////////////////////////////////////////////////
	struct LAN_HEADER
	{
		WORD size;
	};

	/////////////////////////////////////////////////////////////////////////////////////////
	// NET SERVER의 헤더 
	// 실제로 클라이언트와의 통신에 사용되므로 Lan Server와 달리 암호화 등의 장치가 포함됨		
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
	// 기본 패킷 
	/////////////////////////////////////////////////////	
	class CPacket
	{
	public:
		//////////////////////////////////////////////////
		// public static 함수
		//////////////////////////////////////////////////

		// 서버 구동시 패킷 풀 생성
		static bool CreatePacketPool(int NumOfMemBlock);
		static void DeletePacketPool();

		// 외부에서 직렬화 버퍼의 포인터를 동적할당받기 위해서 사용 
		// 어디서나 호출할 수 있게 static으로 선언
		static CPacket* Alloc();

		// 동적할당한 직렬화 버퍼의 사용이 끝났으면 Free호출
		// Free 호출이후 참조카운트 반환, 할당해제 되었다면 0반환
		// 어디서나 호출할 수 있게 static으로 선언
		static LONG           Free(CPacket *pPacket);

		//////////////////////////////////////////////////
		// public 함수
		//////////////////////////////////////////////////
		CPacket(BYTE HeaderSize = sizeof(LAN_HEADER));
		virtual ~CPacket() = default;

		void                  AddRefCnt();

		// 참조카운트를 증가 시킨다. ( 한번에 1이상을 더함 )
		void                  AddRefCnt(LONG Addend);

		int				GetBufferMemSize() const; // 버퍼의 총 메모리크기를 얻는다. (동적할당한 메모리용량)
		int				GetCurrentUsage() const; // 현재 버퍼의 사용량을 얻는다.
		int				GetCurrentSpare() const; // 현재 버퍼에 남은 용량을 얻는다.	

		int				Enqueue(char *pData, int iSize); // size만큼 버퍼에 데이터 넣는다.
		void			EnqueueHeader(char *pHeader);
		int				Dequeue(char *pDestBuf, int iSize); // size만큼 버퍼로부터 데이터 뽑아낸다. (데이터 삭제)
		int				Peek(char *pDestBuf, int iSize); // size만큼 버퍼로부터 데이터 뽑아낸다. (데이터 유지)
		void			EraseData(int iSize);
		void			RelocateWrite(int iSize);

		const char*	    GetBufferPtr() const;
		const char*	    GetReadPtr() const; // 현재 front의 주소값 반환	
		const char*	    GetWritePtr() const; // 현재 rear의 주소값 반환		

		bool        IsEmpty() const;
		bool        IsFull() const;
		void        InitBuffer();	

		int         GetPacketError() const;

		//////////////////////// 연산자 오버로딩 /////////////////////////////
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
		LONG	  m_RefCnt; // 해당 직렬화 버퍼의 참조 카운트			
	public:
		// 패킷 풀
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
	// NetServer 패킷 : CPacket 상속 ( NetServer 전용)
	/////////////////////////////////////////////////////
	
	constexpr auto NET_HEADER_SIZE = 5;

	class CNetPacket : public CPacket
	{
	public:

		//////////////////////////////////////////////////
		// public static 함수
		//////////////////////////////////////////////////

		// 서버 구동시 패킷 풀 생성
		static bool           CreatePacketPool(int NumOfPacketChunk);
		static void           DeletePacketPool();

		// 서버 구동시 패킷 코드 및 XOR 키 생성
		static void           SetPacketCode(BYTE Code, BYTE FixedKey);

		// 외부에서 직렬화 버퍼의 포인터를 동적할당받기 위해서 사용 
		// 어디서나 호출할 수 있게 static으로 선언
		static CNetPacket*    Alloc();

		// 동적할당한 직렬화 버퍼의 사용이 끝났으면 Free호출
		// Free 호출이후 참조카운트 반환, 할당해제 되었다면 0반환
		// 어디서나 호출할 수 있게 static으로 선언
		static LONG           Free(CNetPacket *pPacket);

		//////////////////////////////////////////////////
		// public 함수
		//////////////////////////////////////////////////

		// 생성자와 소멸자
		CNetPacket(BYTE HeaderSize = sizeof(NET_HEADER));
		virtual ~CNetPacket() = default;

		// 버퍼 초기화
		void        InitBuffer();

	public:

		//////////////////////////////////////////////////
		// private 함수
		//////////////////////////////////////////////////

		// 패킷 송신 시, 암호화 처리 후 Net Server 계층의 헤더 삽입
		void Encode();

		// 패킷 수신 시, 복호화 처리 
		bool Decode(NET_HEADER *pHeader);

	private:

		// 인코딩 여부 플래그
		bool      m_IsEncoded = false;

		// 1byte 패킷 코드
		static    BYTE      PacketCode;

		// 1byte 고정 XOR
		static    BYTE      PacketKey;		

		// 패킷 풀 ( 모니터링 목적으로 퍼블릭으로 품 )
	public:
		static    CTlsMemoryPool<CNetPacket> *PacketPool;	

		friend class CNetServer;
		friend class CNetClient;
		friend class CMMOSession;
		friend class CMMOServer;
	};

}

