#include "Packet.h"
#include "MemoryPool\MemoryPool.h"
#include <tchar.h>

namespace GGM
{	
	////////////////////////////////////////////////////////////////////////////////////////////
	// CPacket
	////////////////////////////////////////////////////////////////////////////////////////////
	// LAN SERVER ���� ��ŶǮ
	CTlsMemoryPool<CPacket> *CPacket::PacketPool = nullptr;	

	CPacket::CPacket(BYTE HeaderSize)
	{
		// ����ȭ ���� ������ : �Ű����� �����ŭ ���۸� �����Ҵ��Ѵ�.		
		m_Rear = HeaderSize;
		m_InitRear = HeaderSize;		
	}	

	bool CPacket::CreatePacketPool(int NumOfPacketChunk)
	{
		if(PacketPool == nullptr)
			PacketPool = new CTlsMemoryPool<CPacket>(NumOfPacketChunk);

		if (PacketPool == nullptr)
			return false;

		return true;
	}

	void CPacket::DeletePacketPool()
	{
		if(PacketPool != nullptr)
			delete PacketPool;

		PacketPool = nullptr;
	}

	CPacket * CPacket::Alloc()
	{
		/////////////////////////////////////////////////////
		// ����ȭ ���۸� �����Ҵ��ؼ� �����͸� ��ȯ�Ѵ�.
		// ������ üũ�ϱ� ���� ����ī��Ʈ�� �д�.
		// ���� ī��Ʈ�� 0�� �Ǹ� �Ҵ�����
		////////////////////////////////////////////////////				
		
		CPacket *pPacket = PacketPool->Alloc();		
		
		pPacket->m_RefCnt = 1;

		return pPacket;
	}

	LONG CPacket::Free(CPacket *pPacket)
	{
		/////////////////////////////////////////////////////
		// �����Ҵ��� ����ȭ ������ �����͸� �Ҵ��Ϸ��� �õ��Ѵ�.
		// ���� ī��Ʈ�� �ٿ���, 0�� �Ǵ� ��� �����Ѵ�.		
		/////////////////////////////////////////////////////

		// ������ ������ �ϵ��� ���� �Ǵ�.		
		// ���� ī��Ʈ ����
		LONG RefCnt = InterlockedDecrement((&pPacket->m_RefCnt));

		// ���� ī��Ʈ�� 0�̶�� ����
		if (RefCnt == 0)
		{			
			// ��Ŷ ���� �ʱ�ȭ �� ����
			pPacket->m_Rear = pPacket->m_InitRear;
			pPacket->m_Front = 0;
			PacketPool->Free(pPacket);			
		}		

		return RefCnt;
	}

	void CPacket::AddRefCnt()
	{
		InterlockedIncrement(&m_RefCnt);
	}

	void CPacket::AddRefCnt(LONG Addend)
	{
		// �ش� �����͸� �߰��� �����ϰ��� �� �� �������� ����ī��Ʈ�� �÷��ش�.				
		InterlockedAdd(&m_RefCnt, Addend);				
	}	

	int CPacket::GetBufferMemSize() const
	{
		return m_BufferMemSize;
	}

	int CPacket::GetCurrentUsage() const
	{
		return m_Rear - m_Front;
	}

	int CPacket::GetCurrentSpare() const
	{
		return m_BufferMemSize - m_Rear;
	}

	int CPacket::Enqueue(char * pData, int iSize)
	{
		int iCurrentSpare = m_BufferMemSize;

		// ����
		if (iSize > iCurrentSpare)
		{
			m_Error = GGM_ERROR_SERIAL_BUFFER_OVERFLOW;		
			return GGM_PACKET_ERROR;
		}

		if (iSize <= 16)
		{
			char *pDest = m_pSerialBuffer + m_Rear;
			switch (iSize)
			{ 
			//�������� ũ�Ⱑ ����ũ�� ������ ��� memcpy ���� �ʰ�, �ٷ� ���Կ���
			case 1:
				*pDest = *pData;
				break;
			case 2:
				*((short*)pDest) = *((short*)pData);
				break;
			case 3:
				*((short*)pDest) = *((short*)pData);				
				*(pDest +2) = *(pData+2);
				break;
			case 4:
				*((int*)pDest) = *((int*)pData);
				break;
			case 5:
				*((int*)pDest) = *((int*)pData);			
				*(pDest+4) = *(pData+4);
				break;
			case 6:
				*((int*)pDest) = *((int*)pData);				
				*((short*)(pDest+4)) = *((short*)(pData+4));
				break;
			case 7:
				*((int*)pDest) = *((int*)pData);				
				*((short*)(pDest+4)) = *((short*)(pData+4));				
				*(pDest+6) = *(pData+6);
				break;
			case 8:
				*((double*)pDest) = *((double*)pData);		
				break;
			case 9:
				*((double*)pDest) = *((double*)pData);			
				*(pDest+8) = *(pData+8);
				break;
			case 10:
				*((double*)pDest) = *((double*)pData);			
				*((short*)(pDest+8)) = *((short*)(pData+8));
				break;
			case 11:
				*((double*)pDest) = *((double*)pData);			
				*((short*)(pDest+8)) = *((short*)(pData+8));			
				*(pDest+10) = *(pData+10);
				break;
			case 12:
				*((double*)pDest) = *((double*)pData);				
				*((int*)(pDest+8)) = *((int*)(pData+8));
				break;
			case 13:
				*((double*)pDest) = *((double*)pData);				
				*((int*)(pDest+8)) = *((int*)(pData+8));			
				*(pDest+12) = *(pData+12);
				break;
			case 14:
				*((double*)pDest) = *((double*)pData);					
				*((int*)(pDest + 8)) = *((int*)(pData +8));				
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
				*((double*)(pDest+8)) = *((double*)(pData+8));
				break;
			}
		}
		else
		{
			memcpy_s(&m_pSerialBuffer[m_Rear], iCurrentSpare, pData, iSize);
		}			

		m_Error = 0;
		m_Rear += iSize;

		return iSize;
	}

	void CPacket::EnqueueHeader(char *pHeader)
	{
		// ����ȭ ������ �� �տ� ����� �Ⱦ� �ִ´�.
		*((LAN_HEADER*)m_pSerialBuffer) = *((LAN_HEADER*)pHeader);
	}

	int CPacket::Dequeue(char * pDestBuf, int iSize)
	{
		// ���� ������ �������� ���Ѵ�.
		int iCurrentUsage = m_Rear - m_Front;

		// ����
		if (iSize > iCurrentUsage)
		{
			m_Error = GGM_ERROR_SERIAL_PACKET_FRAGMENT;			
			return GGM_PACKET_ERROR;
		}

		if (iSize <= 16)
		{
			char *pSrc = m_pSerialBuffer + m_Front;			
			switch (iSize)
			{
			//�������� ũ�Ⱑ ����ũ�� ������ ��� memcpy ���� �ʰ�, �ٷ� ���Կ���
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
			memcpy_s(pDestBuf, iSize, &m_pSerialBuffer[m_Front], iSize);
		}		
		
		// Front�� Dequeue�� ����Ʈ ����ŭ �ű��.
		m_Error = 0;
		m_Front += iSize;

		// Dequeue�� ����Ʈ���� �����Ѵ�.
		return iSize;
	}

	int CPacket::Peek(char * pDestBuf, int iSize)
	{
		// ���� ������ �������� ���Ѵ�.
		int iCurrentUsage = m_Rear - m_Front;

		// ����
		if (iSize > iCurrentUsage)
		{
			m_Error = GGM_ERROR_SERIAL_PACKET_FRAGMENT;
			return GGM_PACKET_ERROR;
		}

		if (iSize <= 16)
		{
			char *pSrc = m_pSerialBuffer + m_Front;
			switch (iSize)
			{//�������� ũ�Ⱑ ���� ��� memcpy ���� �ʰ�, �ٷ� ���Կ���
					//�������� ũ�Ⱑ ����ũ�� ������ ��� memcpy ���� �ʰ�, �ٷ� ���Կ���
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
			memcpy_s(pDestBuf, iSize, &m_pSerialBuffer[m_Front], iSize);
		}		

		// Peek�� ����Ʈ���� �����Ѵ�.
		m_Error = 0;
		return iSize;
	}

	void CPacket::EraseData(int iSize)
	{
		m_Front += iSize;
	}

	void CPacket::RelocateWrite(int iSize)
	{
		m_Rear += iSize;
	}


	const char * CPacket::GetBufferPtr() const
	{		
		return m_pSerialBuffer;
	}

	const char * CPacket::GetReadPtr() const
	{
		return &m_pSerialBuffer[m_Front];
	}

	const char * CPacket::GetWritePtr() const
	{
		return &m_pSerialBuffer[m_Rear];
	}

	bool CPacket::IsEmpty() const
	{
		if (m_Rear == 0)
			return true;
		else
			return false;
	}

	bool CPacket::IsFull() const
	{
		if (m_Rear == m_BufferMemSize)
			return true;
		else
			return false;
	}

	void CPacket::InitBuffer()
	{
		m_Rear = m_InitRear;
		m_Front = 0;
	}
	int CPacket::GetPacketError() const
	{
		return m_Error;
	}


	////////////////////////////////////////////////////////////////////////////////////////////
	// CNetPacket
	////////////////////////////////////////////////////////////////////////////////////////////

	// NET SERVER ���� ��ŶǮ
	CTlsMemoryPool<CNetPacket> *CNetPacket::PacketPool = nullptr;
	BYTE                        CNetPacket::PacketCode;
	BYTE                        CNetPacket::PacketKey;
		
	bool CNetPacket::CreatePacketPool(int NumOfPacketChunk)
	{
		// ��Ŷ Ǯ �����Ҵ�
		if (PacketPool == nullptr)
			PacketPool = new CTlsMemoryPool<CNetPacket>(NumOfPacketChunk);

		if (PacketPool == nullptr)
			return false;

		return true;
	}

	void CNetPacket::DeletePacketPool()
	{
		if(PacketPool!= nullptr)
			delete PacketPool;

		PacketPool = nullptr;
	}

	void CNetPacket::SetPacketCode(BYTE Code, BYTE FixedKey)
	{
		// ��Ŷ �ڵ� �� XOR Ű ����
		PacketCode = Code;
		PacketKey  = FixedKey;
	}

	CNetPacket * CNetPacket::Alloc()
	{
		/////////////////////////////////////////////////////
		// ����ȭ ���۸� �����Ҵ��ؼ� �����͸� ��ȯ�Ѵ�.
		// ������ üũ�ϱ� ���� ����ī��Ʈ�� �д�.
		// ���� ī��Ʈ�� 0�� �Ǹ� �Ҵ�����
		////////////////////////////////////////////////////				

		CNetPacket *pPacket = PacketPool->Alloc();

		pPacket->m_RefCnt = 1;

		return pPacket;
	}

	LONG CNetPacket::Free(CNetPacket * pPacket)
	{
		/////////////////////////////////////////////////////
		// �����Ҵ��� ����ȭ ������ �����͸� �Ҵ��Ϸ��� �õ��Ѵ�.
		// ���� ī��Ʈ�� �ٿ���, 0�� �Ǵ� ��� �����Ѵ�.		
		/////////////////////////////////////////////////////

		// ������ ������ �ϵ��� ���� �Ǵ�.		
		// ���� ī��Ʈ ����
		LONG RefCnt = InterlockedDecrement((&pPacket->m_RefCnt));

		// ���� ī��Ʈ�� 0�̶�� ����
		if (RefCnt == 0)
		{
			// ��Ŷ ���� �ʱ�ȭ �� ����
			pPacket->m_Rear = pPacket->m_InitRear;
			pPacket->m_Front = 0;
			pPacket->m_IsEncoded = false;
			PacketPool->Free(pPacket);
		}

		return RefCnt;
	}

	CNetPacket::CNetPacket(BYTE HeaderSize) 
		: CPacket(HeaderSize)
	{		
			
	}

	void CNetPacket::InitBuffer()
	{
		m_Rear = m_InitRear;
		m_Front = 0;
		m_IsEncoded = false;
	}

	void CNetPacket::Encode()
	{
		/*��� �����ʹ� unsigned char �� ó�� �մϴ�.

		��ȣ ����Ű 1Byte - K(Ŭ�� - ���� �ֹ��� �����)
		���� ����Ű 1Byte - RK(Ŭ�� - ���� �ۼ��� ��Ŷ ����� ����)

		# ���� ������ ����Ʈ ����  D1 D2 D3 D4(��Ʈ�� ���� ���� ���� �ʽ��ϴ�. | �������� ���� ������� ���ſ�)
		---------------------
		| D1 | D2 | D3 | D4 |
		---------------------

		D1 ^ (RK + 1) = P1 | D2 ^ (P1 + RK + 2) = P2 | D3 ^ (P2 + RK + 3) = P3 | D4 ^ (P3 + RK + 4) = P4 |
		P1 ^ (K + 1) = E1 | P2 ^ (E1 + K + 2) = E2 | P3 ^ (E2 + K + 3) = E3 | P4 ^ (E3 + K + 4) = E4 |

		# ��ȣ ������ ����Ʈ ����  E1 E2 E3 E4
		---------------------
		| E1 | E2 | E3 | E4 |
		---------------------		
		*/

		// �̹� ���ڵ� �� ��Ŷ�̶�� ����
		if (m_IsEncoded == true)
			return;

		// NetServer ������ ���
		NET_HEADER Header;

		// ��� �⺻���� ����
		Header.Length = m_Rear - NET_HEADER_SIZE;
		Header.PacketCode = PacketCode;	

		//	1. Rand Code ����
		// RandCode�� �׳� ��Ŷ�� �����Ѵ�. 
		Header.RandKey = (BYTE)rand();		

		//	2. Payload �� checksum ���			
		char *pPacket = m_pSerialBuffer;
		char *pPayload = pPacket + NET_HEADER_SIZE;				
		
		// ���̷ε带 1����Ʈ�� ��� ���ؼ� üũ�� �ڵ� ���� 
		Header.CheckSum = 0;
		for (WORD i = 0; i < Header.Length; i++)
			Header.CheckSum += pPayload[i];		

		// 3. XOR �ڵ带 �̿��� ��ȣȭ
		// ��ȣȭ�� ��Ʈ��ũ ����κ��� ������ checksum + payload �κи� �ǽ��Ѵ�.

		// ---------------------------------------------------------------------------------------------
		// ��ȣȭ �⺻ ���� 
		// 1. ���� ��� D >> P
		// 2. ���� ����� ��ȣȭ�� ���� ���� ����� P�� �ӽ����� 
		// 3. ���� ��� P >> E		
		// �ݺ����� ���鼭 ��� ��Ͽ� ���� D>>P�� ���� �� �ٽ� �ݺ��� ���鼭 P>>E�� ������ ���� ������ 
		// �ݺ��� �ѹ��� ������ ���ؼ� �̹� ����� ��ȣȭ�ϴ� ���ÿ� ���� ����� ���� ������ �ٸ� ������ ���� 
		// ---------------------------------------------------------------------------------------------
		BYTE PrevBlockP;		
		Header.CheckSum ^= (Header.RandKey + 1);		
		PrevBlockP = Header.CheckSum;						
		Header.CheckSum ^= (CNetPacket::PacketKey + 1);		

		// üũ������ ������ �Ϸ�Ǿ����� ����� ��Ŷ�� �ִ´�.
		*((int*)pPacket) = *((int*)&Header);
		*(pPacket + 4) = Header.CheckSum;
		
		for (WORD i = 0; i < Header.Length; i++)
		{			
			pPayload[i] ^= (PrevBlockP + Header.RandKey + (i + 2));			
			PrevBlockP = pPayload[i];			
			pPayload[i] ^= (pPayload[i - 1] + CNetPacket::PacketKey + (i + 2));
		}			

		m_IsEncoded = true;
	}

	bool CNetPacket::Decode(NET_HEADER *pHeader)
	{
		/*��� �����ʹ� unsigned char �� ó�� �մϴ�.

		��ȣ ����Ű 1Byte - K(Ŭ�� - ���� �ֹ��� �����)
		���� ����Ű 1Byte - RK(Ŭ�� - ���� �ۼ��� ��Ŷ ����� ����)
		
		# ���� ������ ����Ʈ ����  D1 D2 D3 D4(��Ʈ�� ���� ���� ���� �ʽ��ϴ�. | �������� ���� ������� ���ſ�)
		---------------------
		| D1 | D2 | D3 | D4 |
		---------------------
		
		D1 ^ (RK + 1) = P1 | D2 ^ (P1 + RK + 2) = P2 | D3 ^ (P2 + RK + 3) = P3 | D4 ^ (P3 + RK + 4) = P4 |
		P1 ^ (K + 1) = E1 | P2 ^ (E1 + K + 2) = E2 | P3 ^ (E2 + K + 3) = E3 | P4 ^ (E3 + K + 4) = E4 |
		
		# ��ȣ ������ ����Ʈ ����  E1 E2 E3 E4
		---------------------
		| E1 | E2 | E3 | E4 |
		---------------------
		*/

		// ����� ���Ե� Ű�� ���� ������ ��ȣȭ
		BYTE RandKey = pHeader->RandKey;
		BYTE PrevBlockE;
		BYTE PrevBlockP;

		// ---------------------------------------------------------------------------------------------
		// ��ȣȭ �⺻ ���� 
		// 1. ���� ����� ��ȣȭ�� ���� E�� ����
		// 2. ���� ��� E >> P
		// 3. ���� ����� ��ȣȭ�� ���� P�� ���� 
		// 4. ���� ��� P >> D
		// �ݺ����� ���� ���鼭 ��� ��Ͽ� ���� E>>P�� ���� �� P>>D�� ������ ���� ������ 
		// �ݺ��� �ѹ��� ������ ���ؼ� �̹� ����� ��ȣȭ�ϴ� ���ÿ� ���� ����� ���� ������ �ٸ� ������ ���� 
		// ---------------------------------------------------------------------------------------------

		// Checksum ��ȣȭ		
		BYTE HeaderChecksum = pHeader->CheckSum;			
		PrevBlockE = HeaderChecksum;		
		HeaderChecksum ^= (CNetPacket::PacketKey + 1);		
		PrevBlockP = HeaderChecksum;	
		HeaderChecksum ^= (RandKey +1);

		// ���̷ε� ��ȣȭ
		WORD PayloadSize = pHeader->Length;
		char *pPacket = m_pSerialBuffer;

		for (WORD i = 0; i < PayloadSize; i++)
		{
			int Temp = PrevBlockE;	

			PrevBlockE = pPacket[i];			
			pPacket[i] ^= (Temp + CNetPacket::PacketKey + (i+2));	
			
			Temp = PrevBlockP;

			PrevBlockP = pPacket[i];
			pPacket[i] ^= (Temp + RandKey + (i + 2));
		}

		// ��Ŷ�� ������ Checksum �ڵ�� ��ȯ�Ͽ� ���� üũ���� ��ġ�ϴ��� Ȯ��
		BYTE PayloadChecksum = 0;
		for (WORD i = 0; i < PayloadSize; i++)
			PayloadChecksum += pPacket[i];

		// ����κ��� ��ȣȭ�� üũ���� ���̷ε忡�� ���س� üũ���� �ٸ��� 
		// ���������� ���� ��Ŷ���� ����	
		if (HeaderChecksum != PayloadChecksum)				
			return false;
		
		// ��ȣȭ �����߿� ������ �����ٸ� ���� ���� ����
		return true;
	}

}

