#pragma once
#include <Windows.h>
#include <new>
#include <process.h>
#include "CrashDump\CrashDump.h"
#include "Logger\Logger.h"

namespace GGM
{
	/////////////////////////////////////////////////////////////////////////////////////////////
	// 스택 기반 프리리스트 구조의 템플릿 메모리 풀 
	// - 각 인스턴스마다 노드 할당을 위한 독자적인 힙을 가짐. 메모리 관리의 용이성 및 파편화 방지 목적 
	// - 락프리 알고리즘(Treiber's Stack Algorithm)을 적용하여 멀티 스레드 환경에서 안전하게 사용가능 	
	/////////////////////////////////////////////////////////////////////////////////////////////	

	// 외부에서 노드 반납시 내부적으로 확인할 식별 코드 	
	constexpr auto MEMORY_END_CODE = 0x2f304f50;

	template <typename DATA>
	class CMemoryPool
	{
	public:

		///////////////////////////////////////////////////////////////////////////////////
		// 생성자 1
		// 인자  : 없음
		// 반환  : 없음
		// 기능  : 힙 초기 커밋 0, 동기화 기능 ON, PlacementNew false로 인스턴스 생성		
		///////////////////////////////////////////////////////////////////////////////////
		CMemoryPool();

		///////////////////////////////////////////////////////////////////////////////////
		// 생성자 2 
		// 인자1 : int NumOfMemBlock ( 힙 생성 시 바로 커밋할 데이터 블락의 수 )
		// 인자2 : bool bHeapSerialize ( OS 내부적으로 사용되는 힙 동기화 기능 ON/OFF에 대한 플래그 )
		// 인자3 : bool bPlacementNew ( 생성자 / 소멸자 호출 시점 결정할 플래그 )
		// 반환  : 없음
		// 기능  : 인자로 전달받은 정보를 바탕으로 힙 생성 및 메모리 할당				
		///////////////////////////////////////////////////////////////////////////////////
		CMemoryPool(int NumOfMemBlock, bool bHeapSerialize = true, bool bPlacementNew = false);

		///////////////////////////////////////////////////////////////////////////////////
		// 소멸자
		// 인자 : 없음				
		// 반환 : 없음
		// 기능 : 필요시 각 데이터 블록에 대한 개별 소멸자 호출, 전용 힙 파괴를 통한 메모리 해제
		///////////////////////////////////////////////////////////////////////////////////
		virtual ~CMemoryPool();

		///////////////////////////////////////////////////////////////////////////////////////////
		// Alloc
		// 인자 : 없음				
		// 반환 : DATA*
		// 기능 : 노드를 하나 뽑아 데이터 포인터 반환 		
		///////////////////////////////////////////////////////////////////////////////////////////
		DATA* Alloc(); 

		///////////////////////////////////////////////////////////////////////////////////////////
		// Free
		// 인자 : DATA*				
		// 반환 : bool (성공시 true, 반환 실패(메모리 코드 오류)시 false)
		// 기능 : 반환된 포인터를 스택에 삽입	
		///////////////////////////////////////////////////////////////////////////////////////////	
		bool  Free(DATA *pData); 

		///////////////////////////////////////////////////////////////////////////////////////////
		// GetTotalMemBlock
		// 인자 : 없음				
		// 반환 : int 
		// 기능 : 현재 힙에 실제로 커밋된 데이터 블록의 개수를 반환		
		///////////////////////////////////////////////////////////////////////////////////////////
		ULONGLONG   GetTotalMemBlock() { return m_TotalMemBlockCount; }

		///////////////////////////////////////////////////////////////////////////////////////////
		// GetMemBlokcInUse
		// 인자 : 없음				
		// 반환 : int 
		// 기능 : 현재 외부에서 사용중인 데이터 블록의 개수를 반환 		
		///////////////////////////////////////////////////////////////////////////////////////////
		ULONGLONG   GetMemBlokcInUse(void) { return m_MemBlockInUseCount; }

		///////////////////////////////////////////////////////////////////////////////////////////
		// ClearMemoryPool
		// 인자 : 없음				
		// 반환 : 없음 
		// 기능 : 각 노드에 포함된 데이터에 대해 소멸자 호출, 힙 파괴를 통한 메모리 정리
		///////////////////////////////////////////////////////////////////////////////////////////
		void  ClearMemoryPool();

	protected:		

		// 실제 관리 대상이 되는 스택 기반 프리리스트의 노드
		struct Node
		{
			Node*  pNext = nullptr;
			DATA   Data; // 템플릿 인자로 전달받은 자료형으로, 외부에서 Alloc 호출시 해당 데이터의 포인터를 반환받음
			int    EndCode = MEMORY_END_CODE;				
		};				

		// 락프리 알고리즘을 위한 TOP 구조체 
		struct TOP
		{
			Node*		 pTop = nullptr; // 현재 TOP의 포인터
			LONG64       Count = 0; // ABA 이슈를 해결하기 위한 추가 식별자
		};	

	protected:	 	

		// 리스트 기반 스택의 탑 
		__declspec(align(16)) TOP  m_Top; // interlockedCompareAndExchange128에 사용하기 위해서 16바이트 정렬 해주어야 함
		HANDLE		          m_hHeap = nullptr; // 메모리 풀 전용으로 생성한 힙의 핸들	
		ULONGLONG	          m_TotalMemBlockCount; // 현재 확보된 총 메모리 블럭의 수 
		ULONGLONG		      m_MemBlockInUseCount = 0; // 현재 사용자가 사용중인 메모리 블럭의 수		
		bool			      m_bPlacementNew; // 생성자 및 소멸자 호출 시기를 결정할 플래그					
    };

	template<typename DATA>
	inline CMemoryPool<DATA>::CMemoryPool()
	{
		m_TotalMemBlockCount = 0;

		// 메모리 풀 전용 힙을 하나 생성한다.	
		// 생성되는 힙의 크기는 1MB이며 동적으로 커질 수 있다. 		
		m_hHeap = HeapCreate(0, 0, 0);	

		// 기본 생성자 에서는 PlacementNew == false;
		m_bPlacementNew = false;			
	}

	template<typename DATA>
	inline CMemoryPool<DATA>::CMemoryPool(int iNumOfMemBlock, bool bHeapSerialize, bool bPlacementNew)
	{	
		m_TotalMemBlockCount = iNumOfMemBlock;	
	
		// 메모리 풀 전용 힙을 하나 생성한다.	
		// 생성되는 힙의 크기는 1MB이며 동적으로 커질 수 있다. 
		// 함수에 인자로 전달한 block의 크기만큼 초기 Commit 상태로 둔다.			
		if(bHeapSerialize == true)
			m_hHeap = HeapCreate(0, iNumOfMemBlock * sizeof(Node), 0);
		else
			m_hHeap = HeapCreate(HEAP_NO_SERIALIZE, iNumOfMemBlock * sizeof(Node), 0);			
	
		// 사용자가 일정량의 메모리 Commit을 요청했다면 메모리를 할당한다.
		if (iNumOfMemBlock > 0)
		{
			// 최초 노드 생성
			Node* pNewNode = (Node*)HeapAlloc(m_hHeap, 0, sizeof(Node));			
	
			// Placement New를 사용하지 않는다면 최초 1회 생성자 호출해 주어야 한다!
			if (bPlacementNew == false)
			{
				new (pNewNode) Node;
			}
	
			pNewNode->pNext = nullptr;
			m_Top.pTop = pNewNode;
		
			for (int i = 0; i < iNumOfMemBlock - 1; i++)
			{
				Node* pNewNode = (Node*)HeapAlloc(m_hHeap, 0, sizeof(Node));				
	
				// Placement New를 사용하지 않는다면 최초 1회 생성자 호출해 주어야 한다!
				if (bPlacementNew == false)
				{
					new (pNewNode) Node;
				}			
	
				// 리스트 스택에 PUSH!
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
	
		// 현재 풀에 할당가능한 노드가 존재한다면 Pop 해준다.
		// 락프리 알고리즘을 사용하여 Pop에 성공할때까지 루프돈다.				
		do
		{
			// Pop연산을 수행할 현재 스택의 탑과 Pop연산을 수행한 카운트를 얻어온다.
			// ABA 문제를 해결하기 위해 Pop의 Count를 두어서 유니크하게 식별한다.				
			PopNode.Count = RealTop->Count;						
			PopNode.pTop  = RealTop->pTop;				

			if (PopNode.pTop == nullptr)
			{
				// 현재 풀에 할당된 메모리가 없거나 할당된 메모리를 모두 사용중이라면 새로 할당한다.
				PopNode.pTop = (Node*)HeapAlloc(m_hHeap, 0, sizeof(Node));

				// Placement New를 사용하지 않는다면 생성 직후 생성자 한번 호출해 주어야 한다.
				if (m_bPlacementNew == false)
					new(PopNode.pTop) Node;

				// 힙영역에 할당된 총 메모리 블록의 수를 증가시킨다.				
				InterlockedIncrement(&m_TotalMemBlockCount);

				break;
			}			

		} while (!InterlockedCompareExchange128((volatile LONG64*)RealTop, PopNode.Count + 1, (LONG64)PopNode.pTop->pNext, (LONG64*)&PopNode));

		// Placement New를 사용한다면 이 곳에서 생성자를 호출해 주어야 한다.
		if (m_bPlacementNew == true)
			new (PopNode.pTop) Node;

		// 이 함수를 통해서 반환해야 하는 것은 노드의 주소가 아니라 노드에 포함된 객체의 주소이다.
		DATA* pRetObject = (DATA*)((char*)PopNode.pTop + sizeof(Node*));

		// 사용자가 사용중인 메모리 블록의 수를 증가시킨다.
		InterlockedIncrement(&m_MemBlockInUseCount);

		return pRetObject;
	}

	template<typename DATA>
	inline bool CMemoryPool<DATA>::Free(DATA * pData)
	{
		// 반환된 메모리의 엔드코드를 확인하여 사용자가 올바른 메모리를 반환했는지 확인한다.
		// 사용자가 할당하지 않은 메모리를 반환했다면 함수호출은 실패한다.
		if (*((int*)(pData + 1)) != MEMORY_END_CODE)
			return false;

		// 사용자가 올바른 메모리를 반환했다면 리스트 스택에 PUSH!
		// DATA의 주소에서 포인터만큼 앞으로 이동하면 노드의 주소!!
		Node *pFreeNode = (Node*)((char*)pData - sizeof(Node*));
	
		TOP  *RealTop = &m_Top;
		Node* pSnapTop;

		// Alloc시에 생성자를 호출했다면, Free시에 소멸자를 호출해준다.
		if (m_bPlacementNew == true)
			pFreeNode->Data.~DATA();

		// 반환받은 메모리를 스택에 Push한다.
		// 락프리 알고리즘을 사용하여 Push에 성공할때까지 루프돈다.
		do
		{
			// 현재 스택의 탑을 로컬로 받아온다.
			// 락프리 스택을 사용할 때 Push에서는 ABA문제가 발생하지 않는다.
			// 카운트를 증가시키는 것은 Pop에서만 해주면 된다.					
			pSnapTop = RealTop->pTop;

			// 반환할 노드의 Next를 현재 스택의 탑으로 설정									
			pFreeNode->pNext = pSnapTop;			

			// CAS를 통해 원자적으로 Push를 시도한다.			
		} while (InterlockedCompareExchange64((volatile LONG64*)RealTop, (LONG64)pFreeNode, (LONG64)pSnapTop) != (LONG64)pSnapTop);
		
		// 사용자가 할당해제를 요청했으므로 사용중인 메모리 블록의 수를 감소시킨다.
		InterlockedDecrement(&m_MemBlockInUseCount);

		return true;
	}

	template<typename DATA>
	inline void CMemoryPool<DATA>::ClearMemoryPool()	 
	{
		Node* pList = m_Top.pTop;

		// PlacementNew를 사용하지 않았다면 마지막에 1회 소멸자 호출해 주어야한다.
		if (m_bPlacementNew == false)
		{
			while (pList != nullptr)
			{
				// 메모리 테이블을 순회하면서 모든 노드의 소멸자를 호출시켜준다.
				// new로 할당한 메모리 영역이 아니기 때문에 delete를 사용할 수 없고, 따로 소멸자를 호출해야 한다.
				Node *pDeleteNode = pList;
				pList = pList->pNext;
				pDeleteNode->Data.~DATA();
			}
		}

		// 메모리 풀 내부에서 힙을 직접 생성한 경우에는 생성한 동적 힙을 파괴한다.		
		HeapDestroy(m_hHeap);			
	}

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////	
	

	////////////////////////////////////////////////////////////////////////////////////////////////
	// 스택 기반 프리리스트 구조의 템플릿 메모리 풀 + TLS 활용 + Chunk 도입 
	// - 상기의 메모리 풀에 TLS를 활용하여 메모리 풀의 부하와 스레드 간 경쟁상태를 경감
	// - 스레드별 TLS에 데이터 할당시 Chunk 단위로 할당	
	////////////////////////////////////////////////////////////////////////////////////////////////

	constexpr auto NODES_PER_CHUNK = 200; // Chunk당 데이터 개수
	constexpr auto DEFATUL_MAX_CHUNK = 100; // 기본으로 생성되는 Chunk의 개수 

	template <typename T>
	class CTlsMemoryPool
	{		
	public:

		//////////////////////////////////////////////////////
		// CTlsMemoryPool 멤버 함수
		//////////////////////////////////////////////////////

		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// 생성자 
		// 인자1 : int NumOfChunk ( 초기에 미리 생성해 둘 Chunk의 개수를 설정한다 )
		// 인자2 : bool bHeapSerialzie ( OS 내부적으로 사용되는 힙 동기화 기능 ON/OFF에 대한 플래그 ) 
		// 인자3 : bool bPlacementNew ( Chunk의 생성자 / 소멸자 호출 시점 결정 )		  
		// 반환  : 없음
		// 기능  : 초기화
		// - 힙에 최초 커밋할 크기는 인자1 NumfOfChunk에 따름.
		// - PlacementNew 플래그가 true라면 Alloc 호출될 때마다 Chunk 와 ChunkNode의 생성자가 호출됨		
		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		CTlsMemoryPool(int NumOfChunk = DEFATUL_MAX_CHUNK, bool bHeapSerialize = true, bool bPlacementNew = false);

		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// 소멸자
		// 인자  : 없음				
		// 반환  : 없음
		// 기능  : 리소스 정리		
		// - 동적할당한 메모리 풀을 해제함
		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		virtual ~CTlsMemoryPool();

		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// Alloc
		// 인자  : 없음				
		// 반환  : DATA*
		// 기능  : DATA* 할당	
		// - Alloc을 호출한 스레드의 TLS에 할당된 Chunk에 저장된 T의 포인터를 반환함
		// - 만약 현재 스레드의 TLS에 사용가능한 데이터가 존재하지 않는다면 TLS에 새로운 Chunk를 할당함		
		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////	
		T* Alloc();

		///////////////////////////////////////////////////////////////////////////////////////////
		// Free
		// 인자 : DATA*				
		// 반환 : bool (성공시 true, 반환 실패(메모리 코드 오류)시 false)
		// 기능 : Chunk를 Pool에 반환하기 위한 작업 수행 ( FreeCount 증가 >> FreeCount가 기준치에 이름 >> Chunk 실제로 Free )
		// - Chunk 내부에 Free 카운트를 두고 Free가 호출될 때마다 해당 카운트를 증가시킴
		// - 해당카운트가 최댓값이 되면 더 이상 해당 Chunk를 사용하는 곳이 없는 것이므로 메모리 풀에 반환
		// - 최종적으로 반환이 될 때 메모리 풀에 접근이 이루어진다.
		///////////////////////////////////////////////////////////////////////////////////////////
		bool Free(T* pData);

		///////////////////////////////////////////////////////////////////////////////////////////
		// GetTotalNumOfChunk
		// 인자 : 없음				
		// 반환 : ULONGLONG 
		// 기능 : 현재 존재하는 Chunk의 총 개수를 반환		
		///////////////////////////////////////////////////////////////////////////////////////////
		ULONGLONG GetTotalNumOfChunk() const { return m_pChunkPool->GetTotalMemBlock(); }

		///////////////////////////////////////////////////////////////////////////////////////////
		// GetNumOfChunkInUse
		// 인자 : 없음				
		// 반환 : ULONGLONG 
		// 기능 : 현재 외부에서 사용중인 Chunk의 개수를 반환
		///////////////////////////////////////////////////////////////////////////////////////////
		ULONGLONG GetNumOfChunkInUse() const { return m_pChunkPool->GetMemBlokcInUse(); }

		///////////////////////////////////////////////////////////////////////////////////////////
		// GetNumOfChunkNodeInUse
		// 인자 : 없음				
		// 반환 : ULONGLONG 
		// 기능 : 현재 외부에서 사용중인 ChunkNode의 개수를 반환		
		///////////////////////////////////////////////////////////////////////////////////////////
		ULONGLONG GetNumOfChunkNodeInUse();

		////////////////////////////////////////////////////////////////////////////////////////////////////////
		// CTlsMemoryPool의 관리 대상이 되는 Chunk 
		// - Chunk는 템플릿 인자로 전달된 자료형의 묶음으로써, CTlsMemoryPool이 관리하는 대상이자 할당의 단위가 된다.
		// - 하나의 Chunk는 ChunkNode의 배열로 구성된다.
		// - 외부에서 Alloc 호출시, 하나의 Chunk가 해당 스레드의 TLS에 할당된다.
		// - 스레드는 자신이 할당받은 Chunk에 들어있는 데이터를 모두 소모하기 전까지는 다시 메모리 풀에 접근하지 않는다.
		// - Chunk의 모든 데이터가 소모된 순간에 바로 새로운 Chunk가 TLS에 할당된다.
		// - Chunk가 메모리풀로 다시 반환되는 시점은 마지막 ChunkNode가 Free되는 시점이다.
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
			// Chunk를 구성하는 단위인 ChunkNode
			// - ChunkNode는 실제 데이터의 내용과 자기가 속한 Chunk의 주소를 가지고 있다.
			// - 어느 노드가 Chunk를 반환하게 될지 모르므로 모든 노드는 자신이 속한 Chunk의 주소를 기억할 필요가 있다.			
			// - Chunk가 실제로 반환 될 때, 해당 주소를 기준으로 반환된다.			
			////////////////////////////////////////////////////////////////////////////////////////////////////////////
			struct ChunkNode
			{
				U	           Data;								
				CMemChunk<U>  *pParent;
			};

		protected:

			//////////////////////////////////////////////////////
			// CMemChunk 멤버 변수
			//////////////////////////////////////////////////////

			// 하나의 Chunk는 미리 정의된 상수길이만큼의 ChunkNode 배열로 구성된다.
			ChunkNode m_Chunk[NODES_PER_CHUNK];
			
			// Alloc시 할당해 줄 ChunkNode의 인덱스			
			ULONG     m_ChunkNodeIdx = 0;

			// 현재 Chunk의 FreeCount, NODES_PER_CHUNK값에 도달하면 실제 Chunk의 반환이 이루어짐
			ULONG     m_ChunkFreeCount = 0;		
	
			template<typename T>
			friend class CTlsMemoryPool;
		};

	protected:

		//////////////////////////////////////////////////////
		// CTlsMemoryPool 멤버 변수
		//////////////////////////////////////////////////////

		// Chunk가 할당될 TlsIndex, 생성자에서 TlsAlloc을 통해 할당받는다.
		DWORD m_TlsIndex; 

		// Chunk들을 품고있는 메모리 풀 
		CMemoryPool<CMemChunk<T>> *m_pChunkPool = nullptr;				

		// 외부에서 사용중인 ChunkNode의 개수
		ULONGLONG m_NumOfChunkNodeInUse = 0;			
	};	

	template<typename T>
	inline CTlsMemoryPool<T>::CTlsMemoryPool(int NumOfChunk, bool bHeapSerialize, bool bPlacementNew)
	{
		// 메모리풀 생성
		m_pChunkPool = new CMemoryPool<CMemChunk<T>>(NumOfChunk, bHeapSerialize, bPlacementNew);

		// TlsAlloc
		m_TlsIndex = TlsAlloc();

		// TlsAlloc에 실패하면 throw!
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
		// 메모리풀 할당 해제
		delete m_pChunkPool;

		// TlsIndex free
		TlsFree(m_TlsIndex);
	}

	template<typename T>
	inline T * CTlsMemoryPool<T>::Alloc()
	{	
		// 해당 스레드의 TLS 슬롯에서 Chunk 포인터 얻기
		DWORD TlsIndex = m_TlsIndex;
		CMemChunk<T> *pChunk = (CMemChunk<T>*)TlsGetValue(TlsIndex);

		// 널 체크
		if (pChunk == nullptr)
		{		
			// TLS에 0이 들어있다면 새로운 Chunk 할당해준다.
			pChunk = m_pChunkPool->Alloc();
			TlsSetValue(TlsIndex, (LPVOID)pChunk);
		}						

		// 실제 반환할 객체의 포인터를 얻어온다.
		ULONG &ChunkNodeIdx = pChunk->m_ChunkNodeIdx;
		T* pRet = &(pChunk->m_Chunk[ChunkNodeIdx++].Data);

		// Alloc을 받고나니 현재 TLS 할당된 Chunk의 데이터를 모두 소모했다면 하나 새로 꼽아준다.
		if (ChunkNodeIdx == NODES_PER_CHUNK)
		{
			// 메모리 풀에서 새로운 Chunk를 하나 할당받는다.
			CMemChunk<T>* pChunk = m_pChunkPool->Alloc();

			// 할당받은 새로운 Chunk를 해당 스레드의 TLS에 꼽아 넣는다.
			TlsSetValue(TlsIndex, (LPVOID)pChunk);
		}				

		// 디버깅용		
		InterlockedIncrement(&m_NumOfChunkNodeInUse);

		return pRet;
	}	
	
	template<typename T>
	inline bool CTlsMemoryPool<T>::Free(T* pData)
	{
		// 해당 데이터가 속한 부모 Chunk의 포인터 얻기				
		CMemChunk<T> *pChunk = ((typename CMemChunk<T>::ChunkNode*)pData)->pParent;	

		// 해당 Chunk의 free카운트를 증가시킴		
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
		// CMemChunk의 생성자에서는 내부에 지니고 있는 ChunkNode에 청크 부모의 포인터를 넣어준다.	
		for (int i = 0; i < NODES_PER_CHUNK; i++)
		{			
			m_Chunk[i].pParent = this;
		}
	}

	template<typename T>
	template<typename U>
	inline CTlsMemoryPool<T>::CMemChunk<U>::~CMemChunk()
	{
		// CMemChunk의 소멸자는 ChunkPool로 Chunk가 반환될때 호출이 결정된다.
		// ChunkNode에 포함된 데이터의 소멸자 또한 자동으로 호출된다.
	}	

	template<typename T>	
	inline ULONGLONG CTlsMemoryPool<T>::GetNumOfChunkNodeInUse()
	{
		return m_NumOfChunkNodeInUse;
	}	
}