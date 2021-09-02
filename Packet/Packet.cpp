#include "Packet.h"
#include "MemoryPool\MemoryPool.h"
#include <tchar.h>

namespace GGM
{	
	////////////////////////////////////////////////////////////////////////////////////////////
	// CPacket
	////////////////////////////////////////////////////////////////////////////////////////////
	// LAN SERVER 전용 패킷풀
	CTlsMemoryPool<CPacket> *CPacket::PacketPool = nullptr;	

	CPacket::CPacket(BYTE HeaderSize)
	{
		// 직렬화 버퍼 생성자 : 매개변수 사이즈만큼 버퍼를 동적할당한다.		
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
		// 직렬화 버퍼를 동적할당해서 포인터를 반환한다.
		// 누수를 체크하기 위해 참조카운트를 둔다.
		// 참조 카운트가 0이 되면 할당해제
		////////////////////////////////////////////////////				
		
		CPacket *pPacket = PacketPool->Alloc();		
		
		pPacket->m_RefCnt = 1;

		return pPacket;
	}

	LONG CPacket::Free(CPacket *pPacket)
	{
		/////////////////////////////////////////////////////
		// 동적할당한 직렬화 버퍼의 포인터를 할당하려고 시도한다.
		// 참조 카운트를 줄여서, 0이 되는 경우 삭제한다.		
		/////////////////////////////////////////////////////

		// 스레드 세이프 하도록 락을 건다.		
		// 참조 카운트 차감
		LONG RefCnt = InterlockedDecrement((&pPacket->m_RefCnt));

		// 참조 카운트가 0이라면 삭제
		if (RefCnt == 0)
		{			
			// 패킷 정보 초기화 후 프리
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
		// 해당 포인터를 추가로 참조하고자 할 때 수동으로 참조카운트를 늘려준다.				
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

		// 에러
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
			//데이터의 크기가 일정크기 이하인 경우 memcpy 하지 않고, 바로 대입연산
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
		// 직렬화 버퍼의 맨 앞에 헤더를 꽂아 넣는다.
		*((LAN_HEADER*)m_pSerialBuffer) = *((LAN_HEADER*)pHeader);
	}

	int CPacket::Dequeue(char * pDestBuf, int iSize)
	{
		// 현재 버퍼의 사용공간을 구한다.
		int iCurrentUsage = m_Rear - m_Front;

		// 에러
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
			//데이터의 크기가 일정크기 이하인 경우 memcpy 하지 않고, 바로 대입연산
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
		
		// Front를 Dequeue한 바이트 수만큼 옮긴다.
		m_Error = 0;
		m_Front += iSize;

		// Dequeue한 바이트수를 리턴한다.
		return iSize;
	}

	int CPacket::Peek(char * pDestBuf, int iSize)
	{
		// 현재 버퍼의 사용공간을 구한다.
		int iCurrentUsage = m_Rear - m_Front;

		// 에러
		if (iSize > iCurrentUsage)
		{
			m_Error = GGM_ERROR_SERIAL_PACKET_FRAGMENT;
			return GGM_PACKET_ERROR;
		}

		if (iSize <= 16)
		{
			char *pSrc = m_pSerialBuffer + m_Front;
			switch (iSize)
			{//데이터의 크기가 작은 경우 memcpy 하지 않고, 바로 대입연산
					//데이터의 크기가 일정크기 이하인 경우 memcpy 하지 않고, 바로 대입연산
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

		// Peek한 바이트수를 리턴한다.
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

	// NET SERVER 전용 패킷풀
	CTlsMemoryPool<CNetPacket> *CNetPacket::PacketPool = nullptr;
	BYTE                        CNetPacket::PacketCode;
	BYTE                        CNetPacket::PacketKey;
		
	bool CNetPacket::CreatePacketPool(int NumOfPacketChunk)
	{
		// 패킷 풀 동적할당
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
		// 패킷 코드 및 XOR 키 설정
		PacketCode = Code;
		PacketKey  = FixedKey;
	}

	CNetPacket * CNetPacket::Alloc()
	{
		/////////////////////////////////////////////////////
		// 직렬화 버퍼를 동적할당해서 포인터를 반환한다.
		// 누수를 체크하기 위해 참조카운트를 둔다.
		// 참조 카운트가 0이 되면 할당해제
		////////////////////////////////////////////////////				

		CNetPacket *pPacket = PacketPool->Alloc();

		pPacket->m_RefCnt = 1;

		return pPacket;
	}

	LONG CNetPacket::Free(CNetPacket * pPacket)
	{
		/////////////////////////////////////////////////////
		// 동적할당한 직렬화 버퍼의 포인터를 할당하려고 시도한다.
		// 참조 카운트를 줄여서, 0이 되는 경우 삭제한다.		
		/////////////////////////////////////////////////////

		// 스레드 세이프 하도록 락을 건다.		
		// 참조 카운트 차감
		LONG RefCnt = InterlockedDecrement((&pPacket->m_RefCnt));

		// 참조 카운트가 0이라면 삭제
		if (RefCnt == 0)
		{
			// 패킷 정보 초기화 후 프리
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
		/*모든 데이터는 unsigned char 로 처리 합니다.

		상호 고정키 1Byte - K(클라 - 서버 쌍방의 상수값)
		공개 랜덤키 1Byte - RK(클라 - 서버 송수신 패킷 헤더에 포함)

		# 원본 데이터 바이트 단위  D1 D2 D3 D4(폰트에 따라 줄이 맞지 않습니다. | 기준으로 줄을 맞춰놓고 보셔요)
		---------------------
		| D1 | D2 | D3 | D4 |
		---------------------

		D1 ^ (RK + 1) = P1 | D2 ^ (P1 + RK + 2) = P2 | D3 ^ (P2 + RK + 3) = P3 | D4 ^ (P3 + RK + 4) = P4 |
		P1 ^ (K + 1) = E1 | P2 ^ (E1 + K + 2) = E2 | P3 ^ (E2 + K + 3) = E3 | P4 ^ (E3 + K + 4) = E4 |

		# 암호 데이터 바이트 단위  E1 E2 E3 E4
		---------------------
		| E1 | E2 | E3 | E4 |
		---------------------		
		*/

		// 이미 인코딩 된 패킷이라면 리턴
		if (m_IsEncoded == true)
			return;

		// NetServer 계층의 헤더
		NET_HEADER Header;

		// 헤더 기본정보 셋팅
		Header.Length = m_Rear - NET_HEADER_SIZE;
		Header.PacketCode = PacketCode;	

		//	1. Rand Code 생성
		// RandCode는 그냥 패킷에 노출한다. 
		Header.RandKey = (BYTE)rand();		

		//	2. Payload 의 checksum 계산			
		char *pPacket = m_pSerialBuffer;
		char *pPayload = pPacket + NET_HEADER_SIZE;				
		
		// 페이로드를 1바이트씩 모두 더해서 체크섬 코드 생성 
		Header.CheckSum = 0;
		for (WORD i = 0; i < Header.Length; i++)
			Header.CheckSum += pPayload[i];		

		// 3. XOR 코드를 이용해 암호화
		// 암호화는 네트워크 헤더부분을 제외한 checksum + payload 부분만 실시한다.

		// ---------------------------------------------------------------------------------------------
		// 암호화 기본 로직 
		// 1. 현재 블록 D >> P
		// 2. 다음 블록의 암호화를 위해 현재 블록의 P를 임시저장 
		// 3. 현재 블록 P >> E		
		// 반복문을 돌면서 모든 블록에 대해 D>>P를 진행 후 다시 반복문 돌면서 P>>E를 진행할 수도 있으나 
		// 반복문 한번에 끝내기 위해서 이번 블록을 암호화하는 동시에 다음 블록을 위한 정보를 다른 변수에 저장 
		// ---------------------------------------------------------------------------------------------
		BYTE PrevBlockP;		
		Header.CheckSum ^= (Header.RandKey + 1);		
		PrevBlockP = Header.CheckSum;						
		Header.CheckSum ^= (CNetPacket::PacketKey + 1);		

		// 체크섬까지 생성이 완료되었으니 헤더를 패킷에 넣는다.
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
		/*모든 데이터는 unsigned char 로 처리 합니다.

		상호 고정키 1Byte - K(클라 - 서버 쌍방의 상수값)
		공개 랜덤키 1Byte - RK(클라 - 서버 송수신 패킷 헤더에 포함)
		
		# 원본 데이터 바이트 단위  D1 D2 D3 D4(폰트에 따라 줄이 맞지 않습니다. | 기준으로 줄을 맞춰놓고 보셔요)
		---------------------
		| D1 | D2 | D3 | D4 |
		---------------------
		
		D1 ^ (RK + 1) = P1 | D2 ^ (P1 + RK + 2) = P2 | D3 ^ (P2 + RK + 3) = P3 | D4 ^ (P3 + RK + 4) = P4 |
		P1 ^ (K + 1) = E1 | P2 ^ (E1 + K + 2) = E2 | P3 ^ (E2 + K + 3) = E3 | P4 ^ (E3 + K + 4) = E4 |
		
		# 암호 데이터 바이트 단위  E1 E2 E3 E4
		---------------------
		| E1 | E2 | E3 | E4 |
		---------------------
		*/

		// 헤더에 포함된 키를 통해 데이터 복호화
		BYTE RandKey = pHeader->RandKey;
		BYTE PrevBlockE;
		BYTE PrevBlockP;

		// ---------------------------------------------------------------------------------------------
		// 복호화 기본 로직 
		// 1. 다음 블록의 복호화를 위해 E를 저장
		// 2. 현재 블록 E >> P
		// 3. 다음 블록의 복호화를 위해 P를 저장 
		// 4. 현재 블록 P >> D
		// 반복문을 각각 돌면서 모든 블록에 대해 E>>P를 진행 후 P>>D를 진행할 수도 있으나 
		// 반복문 한번에 끝내기 위해서 이번 블록을 복호화하는 동시에 다음 블록을 위한 정보를 다른 변수에 저장 
		// ---------------------------------------------------------------------------------------------

		// Checksum 복호화		
		BYTE HeaderChecksum = pHeader->CheckSum;			
		PrevBlockE = HeaderChecksum;		
		HeaderChecksum ^= (CNetPacket::PacketKey + 1);		
		PrevBlockP = HeaderChecksum;	
		HeaderChecksum ^= (RandKey +1);

		// 페이로드 복호화
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

		// 패킷의 내용을 Checksum 코드로 변환하여 실제 체크섬과 일치하는지 확인
		BYTE PayloadChecksum = 0;
		for (WORD i = 0; i < PayloadSize; i++)
			PayloadChecksum += pPacket[i];

		// 헤더로부터 복호화된 체크섬과 페이로드에서 구해낸 체크섬이 다르면 
		// 정상적이지 않은 패킷으로 간주	
		if (HeaderChecksum != PayloadChecksum)				
			return false;
		
		// 복호화 과정중에 문제가 없었다면 다음 절차 진행
		return true;
	}

}

