#pragma once
#include <Windows.h>
#include <new>
#include <process.h>
#include "CrashDump\CrashDump.h"
#include "Logger\Logger.h"

namespace GGM
{
	/////////////////////////////////////////////////////////////////////////////////////////////
	// ���� ��� ��������Ʈ ������ ���ø� �޸� Ǯ 
	// - �� �ν��Ͻ����� ��� �Ҵ��� ���� �������� ���� ����. �޸� ������ ���̼� �� ����ȭ ���� ���� 
	// - ������ �˰���(Treiber's Stack Algorithm)�� �����Ͽ� ��Ƽ ������ ȯ�濡�� �����ϰ� ��밡�� 	
	/////////////////////////////////////////////////////////////////////////////////////////////	

	// �ܺο��� ��� �ݳ��� ���������� Ȯ���� �ĺ� �ڵ� 	
	constexpr auto MEMORY_END_CODE = 0x2f304f50;

	template <typename DATA>
	class CMemoryPool
	{
	public:

		///////////////////////////////////////////////////////////////////////////////////
		// ������ 1
		// ����  : ����
		// ��ȯ  : ����
		// ���  : �� �ʱ� Ŀ�� 0, ����ȭ ��� ON, PlacementNew false�� �ν��Ͻ� ����		
		///////////////////////////////////////////////////////////////////////////////////
		CMemoryPool();

		///////////////////////////////////////////////////////////////////////////////////
		// ������ 2 
		// ����1 : int NumOfMemBlock ( �� ���� �� �ٷ� Ŀ���� ������ ����� �� )
		// ����2 : bool bHeapSerialize ( OS ���������� ���Ǵ� �� ����ȭ ��� ON/OFF�� ���� �÷��� )
		// ����3 : bool bPlacementNew ( ������ / �Ҹ��� ȣ�� ���� ������ �÷��� )
		// ��ȯ  : ����
		// ���  : ���ڷ� ���޹��� ������ �������� �� ���� �� �޸� �Ҵ�				
		///////////////////////////////////////////////////////////////////////////////////
		CMemoryPool(int NumOfMemBlock, bool bHeapSerialize = true, bool bPlacementNew = false);

		///////////////////////////////////////////////////////////////////////////////////
		// �Ҹ���
		// ���� : ����				
		// ��ȯ : ����
		// ��� : �ʿ�� �� ������ ��Ͽ� ���� ���� �Ҹ��� ȣ��, ���� �� �ı��� ���� �޸� ����
		///////////////////////////////////////////////////////////////////////////////////
		virtual ~CMemoryPool();

		///////////////////////////////////////////////////////////////////////////////////////////
		// Alloc
		// ���� : ����				
		// ��ȯ : DATA*
		// ��� : ��带 �ϳ� �̾� ������ ������ ��ȯ 		
		///////////////////////////////////////////////////////////////////////////////////////////
		DATA* Alloc(); 

		///////////////////////////////////////////////////////////////////////////////////////////
		// Free
		// ���� : DATA*				
		// ��ȯ : bool (������ true, ��ȯ ����(�޸� �ڵ� ����)�� false)
		// ��� : ��ȯ�� �����͸� ���ÿ� ����	
		///////////////////////////////////////////////////////////////////////////////////////////	
		bool  Free(DATA *pData); 

		///////////////////////////////////////////////////////////////////////////////////////////
		// GetTotalMemBlock
		// ���� : ����				
		// ��ȯ : int 
		// ��� : ���� ���� ������ Ŀ�Ե� ������ ����� ������ ��ȯ		
		///////////////////////////////////////////////////////////////////////////////////////////
		ULONGLONG   GetTotalMemBlock() { return m_TotalMemBlockCount; }

		///////////////////////////////////////////////////////////////////////////////////////////
		// GetMemBlokcInUse
		// ���� : ����				
		// ��ȯ : int 
		// ��� : ���� �ܺο��� ������� ������ ����� ������ ��ȯ 		
		///////////////////////////////////////////////////////////////////////////////////////////
		ULONGLONG   GetMemBlokcInUse(void) { return m_MemBlockInUseCount; }

		///////////////////////////////////////////////////////////////////////////////////////////
		// ClearMemoryPool
		// ���� : ����				
		// ��ȯ : ���� 
		// ��� : �� ��忡 ���Ե� �����Ϳ� ���� �Ҹ��� ȣ��, �� �ı��� ���� �޸� ����
		///////////////////////////////////////////////////////////////////////////////////////////
		void  ClearMemoryPool();

	protected:		

		// ���� ���� ����� �Ǵ� ���� ��� ��������Ʈ�� ���
		struct Node
		{
			Node*  pNext = nullptr;
			DATA   Data; // ���ø� ���ڷ� ���޹��� �ڷ�������, �ܺο��� Alloc ȣ��� �ش� �������� �����͸� ��ȯ����
			int    EndCode = MEMORY_END_CODE;				
		};				

		// ������ �˰����� ���� TOP ����ü 
		struct TOP
		{
			Node*		 pTop = nullptr; // ���� TOP�� ������
			LONG64       Count = 0; // ABA �̽��� �ذ��ϱ� ���� �߰� �ĺ���
		};	

	protected:	 	

		// ����Ʈ ��� ������ ž 
		__declspec(align(16)) TOP  m_Top; // interlockedCompareAndExchange128�� ����ϱ� ���ؼ� 16����Ʈ ���� ���־�� ��
		HANDLE		          m_hHeap = nullptr; // �޸� Ǯ �������� ������ ���� �ڵ�	
		ULONGLONG	          m_TotalMemBlockCount; // ���� Ȯ���� �� �޸� ���� �� 
		ULONGLONG		      m_MemBlockInUseCount = 0; // ���� ����ڰ� ������� �޸� ���� ��		
		bool			      m_bPlacementNew; // ������ �� �Ҹ��� ȣ�� �ñ⸦ ������ �÷���					
    };

	template<typename DATA>
	inline CMemoryPool<DATA>::CMemoryPool()
	{
		m_TotalMemBlockCount = 0;

		// �޸� Ǯ ���� ���� �ϳ� �����Ѵ�.	
		// �����Ǵ� ���� ũ��� 1MB�̸� �������� Ŀ�� �� �ִ�. 		
		m_hHeap = HeapCreate(0, 0, 0);	

		// �⺻ ������ ������ PlacementNew == false;
		m_bPlacementNew = false;			
	}

	template<typename DATA>
	inline CMemoryPool<DATA>::CMemoryPool(int iNumOfMemBlock, bool bHeapSerialize, bool bPlacementNew)
	{	
		m_TotalMemBlockCount = iNumOfMemBlock;	
	
		// �޸� Ǯ ���� ���� �ϳ� �����Ѵ�.	
		// �����Ǵ� ���� ũ��� 1MB�̸� �������� Ŀ�� �� �ִ�. 
		// �Լ��� ���ڷ� ������ block�� ũ�⸸ŭ �ʱ� Commit ���·� �д�.			
		if(bHeapSerialize == true)
			m_hHeap = HeapCreate(0, iNumOfMemBlock * sizeof(Node), 0);
		else
			m_hHeap = HeapCreate(HEAP_NO_SERIALIZE, iNumOfMemBlock * sizeof(Node), 0);			
	
		// ����ڰ� �������� �޸� Commit�� ��û�ߴٸ� �޸𸮸� �Ҵ��Ѵ�.
		if (iNumOfMemBlock > 0)
		{
			// ���� ��� ����
			Node* pNewNode = (Node*)HeapAlloc(m_hHeap, 0, sizeof(Node));			
	
			// Placement New�� ������� �ʴ´ٸ� ���� 1ȸ ������ ȣ���� �־�� �Ѵ�!
			if (bPlacementNew == false)
			{
				new (pNewNode) Node;
			}
	
			pNewNode->pNext = nullptr;
			m_Top.pTop = pNewNode;
		
			for (int i = 0; i < iNumOfMemBlock - 1; i++)
			{
				Node* pNewNode = (Node*)HeapAlloc(m_hHeap, 0, sizeof(Node));				
	
				// Placement New�� ������� �ʴ´ٸ� ���� 1ȸ ������ ȣ���� �־�� �Ѵ�!
				if (bPlacementNew == false)
				{
					new (pNewNode) Node;
				}			
	
				// ����Ʈ ���ÿ� PUSH!
				pNewNode->pNext = m_Top.pTop;
				m_Top.pTop = pNewNode;
			}
		}		
	}	

	template<typename DATA>
	inline CMemoryPool<DATA>::~CMemoryPool()
	{
		ClearMemoryPool();
	}	
	
	template<typename DATA>
	inline DATA * CMemoryPool<DATA>::Alloc()
	{
		TOP  *RealTop = &m_Top;
		__declspec(align(16)) TOP PopNode;
	
		// ���� Ǯ�� �Ҵ簡���� ��尡 �����Ѵٸ� Pop ���ش�.
		// ������ �˰����� ����Ͽ� Pop�� �����Ҷ����� ��������.				
		do
		{
			// Pop������ ������ ���� ������ ž�� Pop������ ������ ī��Ʈ�� ���´�.
			// ABA ������ �ذ��ϱ� ���� Pop�� Count�� �ξ ����ũ�ϰ� �ĺ��Ѵ�.				
			PopNode.Count = RealTop->Count;						
			PopNode.pTop  = RealTop->pTop;				

			if (PopNode.pTop == nullptr)
			{
				// ���� Ǯ�� �Ҵ�� �޸𸮰� ���ų� �Ҵ�� �޸𸮸� ��� ������̶�� ���� �Ҵ��Ѵ�.
				PopNode.pTop = (Node*)HeapAlloc(m_hHeap, 0, sizeof(Node));

				// Placement New�� ������� �ʴ´ٸ� ���� ���� ������ �ѹ� ȣ���� �־�� �Ѵ�.
				if (m_bPlacementNew == false)
					new(PopNode.pTop) Node;

				// �������� �Ҵ�� �� �޸� ����� ���� ������Ų��.				
				InterlockedIncrement(&m_TotalMemBlockCount);

				break;
			}			

		} while (!InterlockedCompareExchange128((volatile LONG64*)RealTop, PopNode.Count + 1, (LONG64)PopNode.pTop->pNext, (LONG64*)&PopNode));

		// Placement New�� ����Ѵٸ� �� ������ �����ڸ� ȣ���� �־�� �Ѵ�.
		if (m_bPlacementNew == true)
			new (PopNode.pTop) Node;

		// �� �Լ��� ���ؼ� ��ȯ�ؾ� �ϴ� ���� ����� �ּҰ� �ƴ϶� ��忡 ���Ե� ��ü�� �ּ��̴�.
		DATA* pRetObject = (DATA*)((char*)PopNode.pTop + sizeof(Node*));

		// ����ڰ� ������� �޸� ����� ���� ������Ų��.
		InterlockedIncrement(&m_MemBlockInUseCount);

		return pRetObject;
	}

	template<typename DATA>
	inline bool CMemoryPool<DATA>::Free(DATA * pData)
	{
		// ��ȯ�� �޸��� �����ڵ带 Ȯ���Ͽ� ����ڰ� �ùٸ� �޸𸮸� ��ȯ�ߴ��� Ȯ���Ѵ�.
		// ����ڰ� �Ҵ����� ���� �޸𸮸� ��ȯ�ߴٸ� �Լ�ȣ���� �����Ѵ�.
		if (*((int*)(pData + 1)) != MEMORY_END_CODE)
			return false;

		// ����ڰ� �ùٸ� �޸𸮸� ��ȯ�ߴٸ� ����Ʈ ���ÿ� PUSH!
		// DATA�� �ּҿ��� �����͸�ŭ ������ �̵��ϸ� ����� �ּ�!!
		Node *pFreeNode = (Node*)((char*)pData - sizeof(Node*));
	
		TOP  *RealTop = &m_Top;
		Node* pSnapTop;

		// Alloc�ÿ� �����ڸ� ȣ���ߴٸ�, Free�ÿ� �Ҹ��ڸ� ȣ�����ش�.
		if (m_bPlacementNew == true)
			pFreeNode->Data.~DATA();

		// ��ȯ���� �޸𸮸� ���ÿ� Push�Ѵ�.
		// ������ �˰����� ����Ͽ� Push�� �����Ҷ����� ��������.
		do
		{
			// ���� ������ ž�� ���÷� �޾ƿ´�.
			// ������ ������ ����� �� Push������ ABA������ �߻����� �ʴ´�.
			// ī��Ʈ�� ������Ű�� ���� Pop������ ���ָ� �ȴ�.					
			pSnapTop = RealTop->pTop;

			// ��ȯ�� ����� Next�� ���� ������ ž���� ����									
			pFreeNode->pNext = pSnapTop;			

			// CAS�� ���� ���������� Push�� �õ��Ѵ�.			
		} while (InterlockedCompareExchange64((volatile LONG64*)RealTop, (LONG64)pFreeNode, (LONG64)pSnapTop) != (LONG64)pSnapTop);
		
		// ����ڰ� �Ҵ������� ��û�����Ƿ� ������� �޸� ����� ���� ���ҽ�Ų��.
		InterlockedDecrement(&m_MemBlockInUseCount);

		return true;
	}

	template<typename DATA>
	inline void CMemoryPool<DATA>::ClearMemoryPool()	 
	{
		Node* pList = m_Top.pTop;

		// PlacementNew�� ������� �ʾҴٸ� �������� 1ȸ �Ҹ��� ȣ���� �־���Ѵ�.
		if (m_bPlacementNew == false)
		{
			while (pList != nullptr)
			{
				// �޸� ���̺��� ��ȸ�ϸ鼭 ��� ����� �Ҹ��ڸ� ȣ������ش�.
				// new�� �Ҵ��� �޸� ������ �ƴϱ� ������ delete�� ����� �� ����, ���� �Ҹ��ڸ� ȣ���ؾ� �Ѵ�.
				Node *pDeleteNode = pList;
				pList = pList->pNext;
				pDeleteNode->Data.~DATA();
			}
		}

		// �޸� Ǯ ���ο��� ���� ���� ������ ��쿡�� ������ ���� ���� �ı��Ѵ�.		
		HeapDestroy(m_hHeap);			
	}

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////	
	

	////////////////////////////////////////////////////////////////////////////////////////////////
	// ���� ��� ��������Ʈ ������ ���ø� �޸� Ǯ + TLS Ȱ�� + Chunk ���� 
	// - ����� �޸� Ǯ�� TLS�� Ȱ���Ͽ� �޸� Ǯ�� ���Ͽ� ������ �� ������¸� �氨
	// - �����庰 TLS�� ������ �Ҵ�� Chunk ������ �Ҵ�	
	////////////////////////////////////////////////////////////////////////////////////////////////

	constexpr auto NODES_PER_CHUNK = 200; // Chunk�� ������ ����
	constexpr auto DEFATUL_MAX_CHUNK = 100; // �⺻���� �����Ǵ� Chunk�� ���� 

	template <typename T>
	class CTlsMemoryPool
	{		
	public:

		//////////////////////////////////////////////////////
		// CTlsMemoryPool ��� �Լ�
		//////////////////////////////////////////////////////

		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// ������ 
		// ����1 : int NumOfChunk ( �ʱ⿡ �̸� ������ �� Chunk�� ������ �����Ѵ� )
		// ����2 : bool bHeapSerialzie ( OS ���������� ���Ǵ� �� ����ȭ ��� ON/OFF�� ���� �÷��� ) 
		// ����3 : bool bPlacementNew ( Chunk�� ������ / �Ҹ��� ȣ�� ���� ���� )		  
		// ��ȯ  : ����
		// ���  : �ʱ�ȭ
		// - ���� ���� Ŀ���� ũ��� ����1 NumfOfChunk�� ����.
		// - PlacementNew �÷��װ� true��� Alloc ȣ��� ������ Chunk �� ChunkNode�� �����ڰ� ȣ���		
		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		CTlsMemoryPool(int NumOfChunk = DEFATUL_MAX_CHUNK, bool bHeapSerialize = true, bool bPlacementNew = false);

		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// �Ҹ���
		// ����  : ����				
		// ��ȯ  : ����
		// ���  : ���ҽ� ����		
		// - �����Ҵ��� �޸� Ǯ�� ������
		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		virtual ~CTlsMemoryPool();

		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// Alloc
		// ����  : ����				
		// ��ȯ  : DATA*
		// ���  : DATA* �Ҵ�	
		// - Alloc�� ȣ���� �������� TLS�� �Ҵ�� Chunk�� ����� T�� �����͸� ��ȯ��
		// - ���� ���� �������� TLS�� ��밡���� �����Ͱ� �������� �ʴ´ٸ� TLS�� ���ο� Chunk�� �Ҵ���		
		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////	
		T* Alloc();

		///////////////////////////////////////////////////////////////////////////////////////////
		// Free
		// ���� : DATA*				
		// ��ȯ : bool (������ true, ��ȯ ����(�޸� �ڵ� ����)�� false)
		// ��� : Chunk�� Pool�� ��ȯ�ϱ� ���� �۾� ���� ( FreeCount ���� >> FreeCount�� ����ġ�� �̸� >> Chunk ������ Free )
		// - Chunk ���ο� Free ī��Ʈ�� �ΰ� Free�� ȣ��� ������ �ش� ī��Ʈ�� ������Ŵ
		// - �ش�ī��Ʈ�� �ִ��� �Ǹ� �� �̻� �ش� Chunk�� ����ϴ� ���� ���� ���̹Ƿ� �޸� Ǯ�� ��ȯ
		// - ���������� ��ȯ�� �� �� �޸� Ǯ�� ������ �̷������.
		///////////////////////////////////////////////////////////////////////////////////////////
		bool Free(T* pData);

		///////////////////////////////////////////////////////////////////////////////////////////
		// GetTotalNumOfChunk
		// ���� : ����				
		// ��ȯ : ULONGLONG 
		// ��� : ���� �����ϴ� Chunk�� �� ������ ��ȯ		
		///////////////////////////////////////////////////////////////////////////////////////////
		ULONGLONG GetTotalNumOfChunk() const { return m_pChunkPool->GetTotalMemBlock(); }

		///////////////////////////////////////////////////////////////////////////////////////////
		// GetNumOfChunkInUse
		// ���� : ����				
		// ��ȯ : ULONGLONG 
		// ��� : ���� �ܺο��� ������� Chunk�� ������ ��ȯ
		///////////////////////////////////////////////////////////////////////////////////////////
		ULONGLONG GetNumOfChunkInUse() const { return m_pChunkPool->GetMemBlokcInUse(); }

		///////////////////////////////////////////////////////////////////////////////////////////
		// GetNumOfChunkNodeInUse
		// ���� : ����				
		// ��ȯ : ULONGLONG 
		// ��� : ���� �ܺο��� ������� ChunkNode�� ������ ��ȯ		
		///////////////////////////////////////////////////////////////////////////////////////////
		ULONGLONG GetNumOfChunkNodeInUse();

		////////////////////////////////////////////////////////////////////////////////////////////////////////
		// CTlsMemoryPool�� ���� ����� �Ǵ� Chunk 
		// - Chunk�� ���ø� ���ڷ� ���޵� �ڷ����� �������ν�, CTlsMemoryPool�� �����ϴ� ������� �Ҵ��� ������ �ȴ�.
		// - �ϳ��� Chunk�� ChunkNode�� �迭�� �����ȴ�.
		// - �ܺο��� Alloc ȣ���, �ϳ��� Chunk�� �ش� �������� TLS�� �Ҵ�ȴ�.
		// - ������� �ڽ��� �Ҵ���� Chunk�� ����ִ� �����͸� ��� �Ҹ��ϱ� �������� �ٽ� �޸� Ǯ�� �������� �ʴ´�.
		// - Chunk�� ��� �����Ͱ� �Ҹ�� ������ �ٷ� ���ο� Chunk�� TLS�� �Ҵ�ȴ�.
		// - Chunk�� �޸�Ǯ�� �ٽ� ��ȯ�Ǵ� ������ ������ ChunkNode�� Free�Ǵ� �����̴�.
		////////////////////////////////////////////////////////////////////////////////////////////////////////

	protected:

		template <typename U>
		class CMemChunk
		{			
		public:
			
			CMemChunk();			
			virtual ~CMemChunk();

		protected:

			////////////////////////////////////////////////////////////////////////////////////////////////////////////
			// Chunk�� �����ϴ� ������ ChunkNode
			// - ChunkNode�� ���� �������� ����� �ڱⰡ ���� Chunk�� �ּҸ� ������ �ִ�.
			// - ��� ��尡 Chunk�� ��ȯ�ϰ� ���� �𸣹Ƿ� ��� ���� �ڽ��� ���� Chunk�� �ּҸ� ����� �ʿ䰡 �ִ�.			
			// - Chunk�� ������ ��ȯ �� ��, �ش� �ּҸ� �������� ��ȯ�ȴ�.			
			////////////////////////////////////////////////////////////////////////////////////////////////////////////
			struct ChunkNode
			{
				U	           Data;								
				CMemChunk<U>  *pParent;
			};

		protected:

			//////////////////////////////////////////////////////
			// CMemChunk ��� ����
			//////////////////////////////////////////////////////

			// �ϳ��� Chunk�� �̸� ���ǵ� ������̸�ŭ�� ChunkNode �迭�� �����ȴ�.
			ChunkNode m_Chunk[NODES_PER_CHUNK];
			
			// Alloc�� �Ҵ��� �� ChunkNode�� �ε���			
			ULONG     m_ChunkNodeIdx = 0;

			// ���� Chunk�� FreeCount, NODES_PER_CHUNK���� �����ϸ� ���� Chunk�� ��ȯ�� �̷����
			ULONG     m_ChunkFreeCount = 0;		
	
			template<typename T>
			friend class CTlsMemoryPool;
		};

	protected:

		//////////////////////////////////////////////////////
		// CTlsMemoryPool ��� ����
		//////////////////////////////////////////////////////

		// Chunk�� �Ҵ�� TlsIndex, �����ڿ��� TlsAlloc�� ���� �Ҵ�޴´�.
		DWORD m_TlsIndex; 

		// Chunk���� ǰ���ִ� �޸� Ǯ 
		CMemoryPool<CMemChunk<T>> *m_pChunkPool = nullptr;				

		// �ܺο��� ������� ChunkNode�� ����
		ULONGLONG m_NumOfChunkNodeInUse = 0;			
	};	

	template<typename T>
	inline CTlsMemoryPool<T>::CTlsMemoryPool(int NumOfChunk, bool bHeapSerialize, bool bPlacementNew)
	{
		// �޸�Ǯ ����
		m_pChunkPool = new CMemoryPool<CMemChunk<T>>(NumOfChunk, bHeapSerialize, bPlacementNew);

		// TlsAlloc
		m_TlsIndex = TlsAlloc();

		// TlsAlloc�� �����ϸ� throw!
		if (m_TlsIndex == TLS_OUT_OF_INDEXES)
		{
			delete m_pChunkPool;
			CLogger::GetInstance()->Log(_T("TlsMemoryPool"), LEVEL::SYS, OUTMODE::FILE, _T("CTlsMemoryPool<T> Ctor, TLS_OUT_OF_INDEXES [Error Code : %d]"), GetLastError());
			CCrashDump::ForceCrash();
		}			
	}

	template<typename T>
	inline CTlsMemoryPool<T>::~CTlsMemoryPool()
	{
		// �޸�Ǯ �Ҵ� ����
		delete m_pChunkPool;

		// TlsIndex free
		TlsFree(m_TlsIndex);
	}

	template<typename T>
	inline T * CTlsMemoryPool<T>::Alloc()
	{	
		// �ش� �������� TLS ���Կ��� Chunk ������ ���
		DWORD TlsIndex = m_TlsIndex;
		CMemChunk<T> *pChunk = (CMemChunk<T>*)TlsGetValue(TlsIndex);

		// �� üũ
		if (pChunk == nullptr)
		{		
			// TLS�� 0�� ����ִٸ� ���ο� Chunk �Ҵ����ش�.
			pChunk = m_pChunkPool->Alloc();
			TlsSetValue(TlsIndex, (LPVOID)pChunk);
		}						

		// ���� ��ȯ�� ��ü�� �����͸� ���´�.
		ULONG &ChunkNodeIdx = pChunk->m_ChunkNodeIdx;
		T* pRet = &(pChunk->m_Chunk[ChunkNodeIdx++].Data);

		// Alloc�� �ް��� ���� TLS �Ҵ�� Chunk�� �����͸� ��� �Ҹ��ߴٸ� �ϳ� ���� �ž��ش�.
		if (ChunkNodeIdx == NODES_PER_CHUNK)
		{
			// �޸� Ǯ���� ���ο� Chunk�� �ϳ� �Ҵ�޴´�.
			CMemChunk<T>* pChunk = m_pChunkPool->Alloc();

			// �Ҵ���� ���ο� Chunk�� �ش� �������� TLS�� �ž� �ִ´�.
			TlsSetValue(TlsIndex, (LPVOID)pChunk);
		}				

		// ������		
		InterlockedIncrement(&m_NumOfChunkNodeInUse);

		return pRet;
	}	
	
	template<typename T>
	inline bool CTlsMemoryPool<T>::Free(T* pData)
	{
		// �ش� �����Ͱ� ���� �θ� Chunk�� ������ ���				
		CMemChunk<T> *pChunk = ((typename CMemChunk<T>::ChunkNode*)pData)->pParent;	

		// �ش� Chunk�� freeī��Ʈ�� ������Ŵ		
		if(NODES_PER_CHUNK == InterlockedIncrement(&(pChunk->m_ChunkFreeCount)))
		{			
			pChunk->m_ChunkFreeCount = 0;
			pChunk->m_ChunkNodeIdx = 0;	

			if (m_pChunkPool->Free(pChunk) == false)
				return false;
		}

		InterlockedDecrement(&m_NumOfChunkNodeInUse);

		return true;
	}

	template<typename T>
	template<typename U>
	inline CTlsMemoryPool<T>::CMemChunk<U>::CMemChunk()
	{		
		// CMemChunk�� �����ڿ����� ���ο� ���ϰ� �ִ� ChunkNode�� ûũ �θ��� �����͸� �־��ش�.	
		for (int i = 0; i < NODES_PER_CHUNK; i++)
		{			
			m_Chunk[i].pParent = this;
		}
	}

	template<typename T>
	template<typename U>
	inline CTlsMemoryPool<T>::CMemChunk<U>::~CMemChunk()
	{
		// CMemChunk�� �Ҹ��ڴ� ChunkPool�� Chunk�� ��ȯ�ɶ� ȣ���� �����ȴ�.
		// ChunkNode�� ���Ե� �������� �Ҹ��� ���� �ڵ����� ȣ��ȴ�.
	}	

	template<typename T>	
	inline ULONGLONG CTlsMemoryPool<T>::GetNumOfChunkNodeInUse()
	{
		return m_NumOfChunkNodeInUse;
	}	
}