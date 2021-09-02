#include "RingBuffer.h"
#include <tchar.h>

namespace GGM
{

	CRingBuffer::CRingBuffer() : m_BufferCapacity(DEFAULT_BUFFER_MEMSIZE - 1), m_BufferMemSize(DEFAULT_BUFFER_MEMSIZE)
	{
		// 동기화 객체 초기화	
		m_pRingBuffer = new char[DEFAULT_BUFFER_MEMSIZE];

		InitializeSRWLock(&m_SRW);
	}

	CRingBuffer::CRingBuffer(int iBufferSize) : m_BufferCapacity(iBufferSize - 1), m_BufferMemSize(iBufferSize)
	{
		// 링버퍼 생성자 : 매개변수 사이즈만큼 버퍼를 동적할당한다.
		m_pRingBuffer = new char[iBufferSize];
	}

	CRingBuffer::~CRingBuffer()
	{
		// 버퍼 파괴
		delete[]m_pRingBuffer;
	}

	bool CRingBuffer::ResizeBuffer(int iBufferSize)
	{
		// 현재 버퍼를 파괴하고 새로운 사이즈로 버퍼 생성
		delete[]m_pRingBuffer;

		m_pRingBuffer = new char[iBufferSize];

		m_BufferCapacity = iBufferSize - 1;
		m_BufferMemSize = iBufferSize;

		if (m_pRingBuffer == nullptr)
			return false;

		return true;
	}

	int CRingBuffer::GetBufferCapacity() const
	{
		return m_BufferCapacity;
	}

	int CRingBuffer::GetBufferMemSize() const
	{
		return m_BufferMemSize;
	}

	int CRingBuffer::GetCurrentUsage() const
	{
		// Bytes of Front To Rear 
		// 현재 버퍼를 얼마나 사용중인가? 
		int iRear = m_Rear;
		int iFront = m_Front;

		if (iRear == iFront)
			return 0;
		else if (iRear > iFront)
			return iRear - iFront;
		else
			return m_BufferMemSize - iFront + iRear;
	}

	int CRingBuffer::GetCurrentSpare() const
	{
		// Bytes of Rear To Front
		// 현재 버퍼의 여유공간이 얼마나되는가?	
		int iFront = m_Front;
		int iRear = m_Rear;

		if (iRear < iFront)
			return iFront - iRear - 1;
		else
			return m_BufferCapacity - iRear + iFront;
	}

	int CRingBuffer::GetSizeReadableAtOnce() const
	{
		int iRear = m_Rear;
		int iFront = m_Front;

		if (iRear == iFront)
			return 0;
		else if (iRear > iFront)
			return iRear - iFront;
		else
			return m_BufferMemSize - iFront;
	}

	int CRingBuffer::GetSizeWritableAtOnce() const
	{
		int iFront = m_Front;
		int iRear = m_Rear;

		if (iFront > iRear)
			return iFront - iRear - 1;
		else if (iFront == 0)
			return m_BufferCapacity - iRear;
		else
			return m_BufferMemSize - iRear;
	}

	int CRingBuffer::Enqueue(char *pData, int iSize)
	{
		int iCurrentSpare = GetCurrentSpare();

		if (iCurrentSpare == 0)
			return false;

		int iRear = m_Rear;

		// 현재 한번에 Enqueue 할 수 있는 크기 구한다.
		int iSizeAtOnce = GetSizeWritableAtOnce();

		if (iSize > iCurrentSpare)
			iSize = iCurrentSpare;

		if (iSizeAtOnce < iSize)
		{
			// 사용자가 요청한 데이터의 크기가 현재 한번에 Enqueue 할 수 있는 크기를 초과하면
			// 두번에 나누어 Enqueue 해야 한다.
			// 일단 한번에 복사할 수 있는 만큼 복사
			memcpy_s(&m_pRingBuffer[iRear], iCurrentSpare, pData, iSizeAtOnce);
			memcpy_s(m_pRingBuffer, (iCurrentSpare - iSizeAtOnce), (pData + iSizeAtOnce), (iSize - iSizeAtOnce));
		}
		else
		{
			if (iSize <= 16)
			{
				char *pDest = m_pRingBuffer + iRear;
				switch (iSize)
				{ //데이터의 크기가 작은 경우 memcpy 하지 않고, 바로 대입연산
				case 1:
					*pDest = *pData;
					break;
				case 2:
					*((short*)pDest) = *((short*)pData);
					break;
				case 3:
					*((short*)pDest) = *((short*)pData);
					*(pDest + 2) = *(pData + 2);
					break;
				case 4:
					*((int*)pDest) = *((int*)pData);
					break;
				case 5:
					*((int*)pDest) = *((int*)pData);
					*(pDest + 4) = *(pData + 4);
					break;
				case 6:
					*((int*)pDest) = *((int*)pData);
					*((short*)(pDest + 4)) = *((short*)(pData + 4));
					break;
				case 7:
					*((int*)pDest) = *((int*)pData);
					*((short*)(pDest + 4)) = *((short*)(pData + 4));
					*(pDest + 6) = *(pData + 6);
					break;
				case 8:
					*((double*)pDest) = *((double*)pData);
					break;
				case 9:
					*((double*)pDest) = *((double*)pData);
					*(pDest + 8) = *(pData + 8);
					break;
				case 10:
					*((double*)pDest) = *((double*)pData);
					*((short*)(pDest + 8)) = *((short*)(pData + 8));
					break;
				case 11:
					*((double*)pDest) = *((double*)pData);
					*((short*)(pDest + 8)) = *((short*)(pData + 8));
					*(pDest + 10) = *(pData + 10);
					break;
				case 12:
					*((double*)pDest) = *((double*)pData);
					*((int*)(pDest + 8)) = *((int*)(pData + 8));
					break;
				case 13:
					*((double*)pDest) = *((double*)pData);
					*((int*)(pDest + 8)) = *((int*)(pData + 8));
					*(pDest + 12) = *(pData + 12);
					break;
				case 14:
					*((double*)pDest) = *((double*)pData);
					*((int*)(pDest + 8)) = *((int*)(pData + 8));
					*((short*)(pDest + 12)) = *((short*)(pData + 12));
					break;
				case 15:
					*((double*)pDest) = *((double*)pData);
					*((int*)(pDest + 8)) = *((int*)(pData + 8));
					*((short*)(pDest + 12)) = *((short*)(pData + 12));
					*(pDest + 14) = *(pData + 14);
					break;
				case 16:
					*((double*)pDest) = *((double*)pData);
					*((double*)(pDest + 8)) = *((double*)(pData + 8));
					break;
				}
			}
			else
			{
				memcpy_s(&m_pRingBuffer[iRear], iCurrentSpare, pData, iSize);
			}
		}

		iRear += iSize;
		if (iRear > m_BufferCapacity)
			iRear -= m_BufferMemSize;

		m_Rear = iRear;

		return iSize;
	}

	int CRingBuffer::Dequeue(char * pDestBuf, int iSize)
	{
		// 데이터가 없으면 0 반환한다.	
		if (m_Front == m_Rear)
			return 0;

		int iFront = m_Front;

		// 현재 버퍼의 사용공간을 구한다.
		int iCurrentUsage = GetCurrentUsage();
		int iSizeAtOnce = GetSizeReadableAtOnce();

		// 현재 버퍼의 데이터가 사용자가 요구하는 것보다 작으면 있는만큼만 복사한다.
		if (iSize > iCurrentUsage)
			iSize = iCurrentUsage;

		// 사용자가 요청한 데이터의 크기가 현재 한번에 Dequeue 할 수 있는 크기를 초과하면
		// 두번에 나누어 Dequeue 해야 한다.
		if (iSizeAtOnce < iSize)
		{
			// 일단 링버퍼의 끝까지 Dequeue할 수 있는 만큼 수행
			memcpy_s(pDestBuf, iSize, &m_pRingBuffer[iFront], iSizeAtOnce);
			memcpy_s((pDestBuf + iSizeAtOnce), iSize, m_pRingBuffer, (iSize - iSizeAtOnce));
		}
		else
		{
			if (iSize <= 16)
			{
				char *pSrc = m_pRingBuffer + iFront;
				switch (iSize)
				{//데이터의 크기가 작은 경우 memcpy 하지 않고, 바로 대입연산
				case 1:
					*pDestBuf = *pSrc;
					break;
				case 2:
					*((short*)pDestBuf) = *((short*)pSrc);
					break;
				case 3:
					*((short*)pDestBuf) = *((short*)pSrc);
					*(pDestBuf + 2) = *(pSrc + 2);
					break;
				case 4:
					*((int*)pDestBuf) = *((int*)pSrc);
					break;
				case 5:
					*((int*)pDestBuf) = *((int*)pSrc);
					*(pDestBuf + 4) = *(pSrc + 4);
					break;
				case 6:
					*((int*)pDestBuf) = *((int*)pSrc);
					*((short*)(pDestBuf + 4)) = *((short*)(pSrc + 4));
					break;
				case 7:
					*((int*)pDestBuf) = *((int*)pSrc);
					*((short*)(pDestBuf + 4)) = *((short*)(pSrc + 4));
					*(pDestBuf + 6) = *(pSrc + 6);
					break;
				case 8:
					*((double*)pDestBuf) = *((double*)pSrc);
					break;
				case 9:
					*((double*)pDestBuf) = *((double*)pSrc);
					*(pDestBuf + 8) = *(pSrc + 8);
					break;
				case 10:
					*((double*)pDestBuf) = *((double*)pSrc);
					*((short*)(pDestBuf + 8)) = *((short*)(pSrc + 8));
					break;
				case 11:
					*((double*)pDestBuf) = *((double*)pSrc);
					*((short*)(pDestBuf + 8)) = *((short*)(pSrc + 8));
					*(pDestBuf + 10) = *(pSrc + 10);
					break;
				case 12:
					*((double*)pDestBuf) = *((double*)pSrc);
					*((int*)(pDestBuf + 8)) = *((int*)(pSrc + 8));
					break;
				case 13:
					*((double*)pDestBuf) = *((double*)pSrc);
					*((int*)(pDestBuf + 8)) = *((int*)(pSrc + 8));
					*(pDestBuf + 12) = *(pSrc + 12);
					break;
				case 14:
					*((double*)pDestBuf) = *((double*)pSrc);
					*((int*)(pDestBuf + 8)) = *((int*)(pSrc + 8));
					*((short*)(pDestBuf + 12)) = *((short*)(pSrc + 12));
					break;
				case 15:
					*((double*)pDestBuf) = *((double*)pSrc);
					*((int*)(pDestBuf + 8)) = *((int*)(pSrc + 8));
					*((short*)(pDestBuf + 12)) = *((short*)(pSrc + 12));
					*(pDestBuf + 14) = *(pSrc + 14);
					break;
				case 16:
					*((double*)pDestBuf) = *((double*)pSrc);
					*((double*)(pDestBuf + 8)) = *((double*)(pSrc + 8));
					break;
				}
			}
			else
			{
				memcpy_s(pDestBuf, iSize, &m_pRingBuffer[iFront], iSize);
			}
		}

		// Front를 Dequeue한 바이트 수만큼 옮긴다.
		iFront += iSize;
		if (iFront > m_BufferCapacity)
			iFront -= m_BufferMemSize;

		m_Front = iFront;

		// Dequeue한 바이트수를 리턴한다.
		return iSize;
	}

	int CRingBuffer::Peek(char * pDestBuf, int iSize) const
	{
		// 데이터가 없으면 0 반환한다.	
		if (m_Front == m_Rear)
			return 0;

		int iFront = m_Front;

		// 현재 버퍼의 사용공간을 구한다.
		int iCurrentUsage = GetCurrentUsage();
		int iSizeAtOnce = GetSizeReadableAtOnce();

		// 현재 버퍼의 데이터가 사용자가 요구하는 것보다 작으면 있는만큼만 복사한다.
		if (iSize > iCurrentUsage)
			iSize = iCurrentUsage;

		// 데이터가 커서 한바퀴 돌아야 한다면 데이터를 잘라서 뽑는다.
		if (iSizeAtOnce < iSize)
		{
			// 일단 링버퍼의 끝까지 Dequeue할 수 있는 만큼 수행
			memcpy_s(pDestBuf, iSize, &m_pRingBuffer[iFront], iSizeAtOnce);
			memcpy_s((pDestBuf + iSizeAtOnce), iSize, m_pRingBuffer, (iSize - iSizeAtOnce));
		}
		else
		{
			if (iSize <= 16)
			{
				char *pSrc = m_pRingBuffer + iFront;
				switch (iSize)
				{//데이터의 크기가 작은 경우 memcpy 하지 않고, 바로 대입연산
				case 1:
					*pDestBuf = *pSrc;
					break;
				case 2:
					*((short*)pDestBuf) = *((short*)pSrc);
					break;
				case 3:
					*((short*)pDestBuf) = *((short*)pSrc);
					*(pDestBuf + 2) = *(pSrc + 2);
					break;
				case 4:
					*((int*)pDestBuf) = *((int*)pSrc);
					break;
				case 5:
					*((int*)pDestBuf) = *((int*)pSrc);
					*(pDestBuf + 4) = *(pSrc + 4);
					break;
				case 6:
					*((int*)pDestBuf) = *((int*)pSrc);
					*((short*)(pDestBuf + 4)) = *((short*)(pSrc + 4));
					break;
				case 7:
					*((int*)pDestBuf) = *((int*)pSrc);
					*((short*)(pDestBuf + 4)) = *((short*)(pSrc + 4));
					*(pDestBuf + 6) = *(pSrc + 6);
					break;
				case 8:
					*((double*)pDestBuf) = *((double*)pSrc);
					break;
				case 9:
					*((double*)pDestBuf) = *((double*)pSrc);
					*(pDestBuf + 8) = *(pSrc + 8);
					break;
				case 10:
					*((double*)pDestBuf) = *((double*)pSrc);
					*((short*)(pDestBuf + 8)) = *((short*)(pSrc + 8));
					break;
				case 11:
					*((double*)pDestBuf) = *((double*)pSrc);
					*((short*)(pDestBuf + 8)) = *((short*)(pSrc + 8));
					*(pDestBuf + 10) = *(pSrc + 10);
					break;
				case 12:
					*((double*)pDestBuf) = *((double*)pSrc);
					*((int*)(pDestBuf + 8)) = *((int*)(pSrc + 8));
					break;
				case 13:
					*((double*)pDestBuf) = *((double*)pSrc);
					*((int*)(pDestBuf + 8)) = *((int*)(pSrc + 8));
					*(pDestBuf + 12) = *(pSrc + 12);
					break;
				case 14:
					*((double*)pDestBuf) = *((double*)pSrc);
					*((int*)(pDestBuf + 8)) = *((int*)(pSrc + 8));
					*((short*)(pDestBuf + 12)) = *((short*)(pSrc + 12));
					break;
				case 15:
					*((double*)pDestBuf) = *((double*)pSrc);
					*((int*)(pDestBuf + 8)) = *((int*)(pSrc + 8));
					*((short*)(pDestBuf + 12)) = *((short*)(pSrc + 12));
					*(pDestBuf + 14) = *(pSrc + 14);
					break;
				case 16:
					*((double*)pDestBuf) = *((double*)pSrc);
					*((double*)(pDestBuf + 8)) = *((double*)(pSrc + 8));
					break;
				}
			}
			else
			{
				memcpy_s(pDestBuf, iSize, &m_pRingBuffer[iFront], iSize);
			}
		}

		// Peek한 바이트수를 리턴한다.
		return iSize;
	}

	void CRingBuffer::EraseData(int iSize)
	{
		int iFront = m_Front;
		iFront += iSize;

		if (iFront > m_BufferCapacity)
			iFront -= m_BufferMemSize;

		m_Front = iFront;
	}

	void CRingBuffer::RelocateWrite(int iSize)
	{
		int iRear = m_Rear;
		iRear += iSize;

		if (iRear > m_BufferCapacity)
			iRear -= m_BufferMemSize;

		m_Rear = iRear;
	}

	char* CRingBuffer::GetBufferPtr() const
	{
		return m_pRingBuffer;
	}

	char* CRingBuffer::GetReadPtr() const
	{
		return &m_pRingBuffer[m_Front];
	}

	char* CRingBuffer::GetWritePtr() const
	{
		return &m_pRingBuffer[m_Rear];
	}

	void CRingBuffer::ClearRingBuffer()
	{
		m_Front = 0;
		m_Rear = 0;
	}

	void CRingBuffer::SRWExclusiveLock()
	{
		AcquireSRWLockExclusive(&m_SRW);
	}

	void CRingBuffer::SRWExclusiveUnLock()
	{
		ReleaseSRWLockExclusive(&m_SRW);
	}

	void CRingBuffer::SRWSharedLock()
	{
		AcquireSRWLockShared(&m_SRW);
	}

	void CRingBuffer::SRWSharedUnLock()
	{
		ReleaseSRWLockShared(&m_SRW);
	}

	bool CRingBuffer::IsEmpty() const
	{
		// 버퍼가 비었는지 확인
		if (m_Front == m_Rear)
			return true;
		else
			return false;
	}

	bool CRingBuffer::IsFull() const
	{
		// 버퍼가 꽉 찼는지 확인
		if (GetNextPos(m_Rear) == m_Front)
			return true;
		else
			return false;
	}

	int CRingBuffer::GetNextPos(int Index) const
	{
		if (Index >= m_BufferCapacity)
			return 0;
		else
			return Index + 1;
	}

}
