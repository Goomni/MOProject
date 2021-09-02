#pragma once
#include <Windows.h>

namespace GGM
{ 
	constexpr auto DEFAULT_BUFFER_MEMSIZE = 8192;

	class CRingBuffer
	{
	public:

		CRingBuffer();
		explicit	CRingBuffer(int iBufferSize); // ����Ʈ ����� �ƴ� �ٸ� ������� ���۸� �����ϰ� ������ ����ϴ� ������
		~CRingBuffer();

		bool		ResizeBuffer(int iBufferSize); // ���� ���۸� �Ҵ������ϰ� ���ο� ������� ���� ����
		int			GetBufferCapacity() const; // ������ �� ���� ������ ��´�. (�����Ҵ��� �޸𸮿뷮 -1)
		int			GetBufferMemSize() const; // ������ �� �޸�ũ�⸦ ��´�. (�����Ҵ��� �޸𸮿뷮)
		int			GetCurrentUsage() const; // ���� ������ ��뷮�� ��´�.
		int			GetCurrentSpare() const; // ���� ���ۿ� ���� �뷮�� ��´�.	

		int			Enqueue(char *pData, int iSize); // size��ŭ ���ۿ� ������ �ִ´�.
		int			Dequeue(char *pDestBuf, int iSize); // size��ŭ ���۷κ��� ������ �̾Ƴ���. (������ ����)
		int         Peek(char *pDestBuf, int iSize) const; // size��ŭ ���۷κ��� ������ �̾Ƴ���. (������ ����)
		void		EraseData(int iSize);
		void        RelocateWrite(int iSize);

		char*	    GetBufferPtr() const;
		char*	    GetReadPtr() const; // ���� front�� �ּҰ� ��ȯ		
		char*	    GetWritePtr() const; // ���� rear�� �ּҰ� ��ȯ	
		int         GetSizeReadableAtOnce() const;
		int         GetSizeWritableAtOnce() const;

		bool        IsEmpty() const;
		bool        IsFull() const;
		void        ClearRingBuffer();

		// �ܺο��� ����ȭ ��ü�� ����� ���� �ɱ� ���� �������̽�		
		void        SRWExclusiveLock();
		void        SRWExclusiveUnLock();
		void        SRWSharedLock();
		void        SRWSharedUnLock();

	private:
	
		char				*m_pRingBuffer = nullptr;
		int					m_BufferCapacity; // ���� ������ ���� ���� (�����Ҵ��� �޸𸮿뷮 -1)
		int					m_BufferMemSize; // ������ �޸� �Ҵ� ���� (�����Ҵ��� �޸𸮿뷮)
		int					m_Front = 0;
		int					m_Rear = 0;			
		SRWLOCK             m_SRW; // ������ ����ȭ ��ü SRW	
	
	private:	
		int         GetNextPos(int Index) const;
		
	};

}