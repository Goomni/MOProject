#pragma once
#include <Windows.h>

namespace GGM
{ 
	constexpr auto DEFAULT_BUFFER_MEMSIZE = 8192;

	class CRingBuffer
	{
	public:

		CRingBuffer();
		explicit	CRingBuffer(int iBufferSize); // 디폴트 사이즈가 아닌 다른 사이즈로 버퍼를 생성하고 싶을때 사용하는 생성자
		~CRingBuffer();

		bool		ResizeBuffer(int iBufferSize); // 이전 버퍼를 할당해제하고 새로운 사이즈로 버퍼 생성
		int			GetBufferCapacity() const; // 버퍼의 총 가용 공간을 얻는다. (동적할당한 메모리용량 -1)
		int			GetBufferMemSize() const; // 버퍼의 총 메모리크기를 얻는다. (동적할당한 메모리용량)
		int			GetCurrentUsage() const; // 현재 버퍼의 사용량을 얻는다.
		int			GetCurrentSpare() const; // 현재 버퍼에 남은 용량을 얻는다.	

		int			Enqueue(char *pData, int iSize); // size만큼 버퍼에 데이터 넣는다.
		int			Dequeue(char *pDestBuf, int iSize); // size만큼 버퍼로부터 데이터 뽑아낸다. (데이터 삭제)
		int         Peek(char *pDestBuf, int iSize) const; // size만큼 버퍼로부터 데이터 뽑아낸다. (데이터 유지)
		void		EraseData(int iSize);
		void        RelocateWrite(int iSize);

		char*	    GetBufferPtr() const;
		char*	    GetReadPtr() const; // 현재 front의 주소값 반환		
		char*	    GetWritePtr() const; // 현재 rear의 주소값 반환	
		int         GetSizeReadableAtOnce() const;
		int         GetSizeWritableAtOnce() const;

		bool        IsEmpty() const;
		bool        IsFull() const;
		void        ClearRingBuffer();

		// 외부에서 동기화 객체를 사용해 락을 걸기 위한 인터페이스		
		void        SRWExclusiveLock();
		void        SRWExclusiveUnLock();
		void        SRWSharedLock();
		void        SRWSharedUnLock();

	private:
	
		char				*m_pRingBuffer = nullptr;
		int					m_BufferCapacity; // 실제 버퍼의 가용 공간 (동적할당한 메모리용량 -1)
		int					m_BufferMemSize; // 버퍼의 메모리 할당 공간 (동적할당한 메모리용량)
		int					m_Front = 0;
		int					m_Rear = 0;			
		SRWLOCK             m_SRW; // 링버퍼 동기화 객체 SRW	
	
	private:	
		int         GetNextPos(int Index) const;
		
	};

}