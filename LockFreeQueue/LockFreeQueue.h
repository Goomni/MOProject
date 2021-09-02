#pragma once
#include <Windows.h>
#include "MemoryPool\MemoryPool.h"
#include "CrashDump\CrashDump.h"

namespace GGM
{	
	/////////////////////////////////////////////////////////////////////////////////////////////////////////
	// 락 프리 큐! ( Lock-free queue as a singly-linkedlist with Head and Tail pointers)
	// - 구현시 반드시 기억해야 할 사항
	//  1. 큐에 노드가 하나도 존재하지 않는 경우는 없다. 무슨일이 있어도 더미 노드 하나는 존재한다.
	//  2. 누군가 인큐에 성공했다면 노드는 항상 큐에 제대로 연결되어 있어야 한다. 중간에 노드간 연결이 끊어지는 경우는 절대 없어야 한다.
	//  ( Tail은 옮겨지지 않았을 수도 있다. Enqueue를 수행중인 스레드가 옮기지 못했다면 다른 스레드가 옮겨줄 수도 있다. )
	//  3. 인큐는 항상 Tail을 기준으로 연산한다.
	//  4. 디큐는 항상 Head를 기준으로 연산한다.
	//  5. Head는 항상 리스트의 첫번째 노드를 가리킨다. ( 더미 노드 )
	//  6. Tail은 항상 리스트 내부의 노드를 가리킨다. 
	/////////////////////////////////////////////////////////////////////////////////////////////////////////
	template <typename DATA>
	class CLockFreeQueue
	{
	public:		

		// 기본 생성자는 사용하지 않는다.
		CLockFreeQueue() = default;

		// 생성자의 인자로 미리 생성해 둘 노드의 개수를 전달 받는다.
		CLockFreeQueue(int NumOfMemBlock, ULONG MaxSize, bool bIsPlacementNew = false);		

		virtual ~CLockFreeQueue();

		void InitLockFreeQueue(int NumOfMemBlock, ULONG MaxSize, bool bIsPlacementNew = false);
		
		void ClearLockFreeQueue();

		bool Enqueue(DATA data);		

		bool Dequeue(DATA *pOut);			

		bool Peek(DATA *pOut, ULONG NodePos);		

		ULONG size() const;

	protected:

		struct Node
		{
			Node *pNext;
			DATA data;
		};

		// Head와 Tail 전용 구조체
		struct NODE_PTR
		{
			Node     *pNode = nullptr;
			LONG64   Count = 0;
		};

	protected:

		__declspec(align(16)) NODE_PTR             m_Head; // 항상 리스트의 첫번째 노드를 가리킨다.
		__declspec(align(16)) NODE_PTR             m_Tail; // 항상 리스트 내부의 노드를 가리킨다.		
							  LONG                 m_Size = 0; // 현재 큐의 사이즈 (노드 개수)		                  
							  LONG                 m_MaxSize = 0;
							  CMemoryPool<Node>    *m_pNodePool; // 노드를 할당해 줄 메모리 풀         
	
	};
	template<typename DATA>
	inline CLockFreeQueue<DATA>::CLockFreeQueue(int NumOfMemBlock, ULONG MaxSize, bool bIsPlacementNew)
	{		
		InitLockFreeQueue(NumOfMemBlock, MaxSize, bIsPlacementNew);		
	}
	template<typename DATA>
	inline CLockFreeQueue<DATA>::~CLockFreeQueue()
	{
		// 메모리 풀 정리
		delete m_pNodePool;
	}
	template<typename DATA>
	inline void CLockFreeQueue<DATA>::InitLockFreeQueue(int NumOfMemBlock, ULONG MaxSize, bool bIsPlacementNew)
	{
		// 큐의 최대 사이즈 설정
		m_MaxSize = MaxSize;

		// 메모리 풀 생성
		m_pNodePool = new CMemoryPool<Node>(NumOfMemBlock, true);

		// 더미 노드 생성			
		Node* pDummyNode = m_pNodePool->Alloc();
		pDummyNode->pNext = nullptr;

		// Head와 Tail이 더미노드를 가리키게 한다.
		m_Head.pNode = m_Tail.pNode = pDummyNode;
	}

	template<typename DATA>
	inline void CLockFreeQueue<DATA>::ClearLockFreeQueue()
	{
		// 락프리 큐를 새로 생성하지 않고 초기상태로 돌릴때 사용
		// 현재 큐에 있는 노드를 전부 메모리풀에 반환한다.
		Node* pFreeNode = m_Head.pNode->pNext;
		while (pFreeNode != nullptr)
		{
			m_pNodePool->Free(pFreeNode);
			pFreeNode = pFreeNode->pNext;
		}

		// 헤드, 테일을 초기화한다.
		m_Head.Count = 0;
		m_Tail.Count = 0;
		m_Head.pNode->pNext = nullptr;
		m_Size = 0;
	}

	template<typename DATA>
	inline bool CLockFreeQueue<DATA>::Enqueue(DATA data)
	{
		// 현재 큐의 사이즈가 꽉 찼다면 더 이상 인큐 불가
		if (m_Size > m_MaxSize)
		{			
			return false;
		}

		// 새 노드를 할당받는다.				
		Node *pNewNode = m_pNodePool->Alloc();

		// 할당받은 노드에 데이터를 채운다.
		pNewNode->data = data;

		// 노드의 Next를 nullptr로 설정한다. 리스트의 끝은 항상 null이어야 한다.
		// 락프리큐 구현 코드 중 Next 포인터를 nullptr로 설정하는 부분은 이곳이 유일해야만 한다.		
		pNewNode->pNext = nullptr;

		// Enqueue에 성공할 때까지 루프돌며 시도한다.
		__declspec(align(16)) NODE_PTR LocalTail;
		NODE_PTR *RealTail = &m_Tail;
		Node *pLocalNext;

		while (true)
		{
			// Tail과 Next의 정보를 로컬로 얻어온다.				
			LocalTail.Count = RealTail->Count;
			LocalTail.pNode = RealTail->pNode;
			pLocalNext = LocalTail.pNode->pNext;

			// Tail의 Next가 NULL일 경우에만 인큐에 성공할 수 있다.
			if (pLocalNext == nullptr)
			{
				// Tail의 Next를 인큐할 노드로 변경! 
				// 연결에 성공했다면 루프 탈출
				if (InterlockedCompareExchange64((volatile LONG64*)&(RealTail->pNode->pNext), (LONG64)pNewNode, (LONG64)nullptr) == (LONG64)nullptr)
				{
					// Enqueue 완료 후 사이즈 증가						
					InterlockedIncrement(&m_Size);
					break;
				}
			}
			else
			{
				// Tail의 Next가 nullptr가 아니라면 누군가가 새 노드 연결은 완료했지만 Tail을 아직 옮기지 못한 경우이다.
				// 내가 옮길 수 있다면 옮기도록 시도해본다.
				InterlockedCompareExchange128(
					(volatile LONG64*)RealTail,
					LocalTail.Count + 1,
					(LONG64)pLocalNext,
					(LONG64*)&LocalTail
				);
			}

		}

		// 루프를 탈출했다는 것은 Enqueue를 성공적으로 완수했다는 것이다.
		// 이제 남은 것은 Tail을 옮겨주는 것이다.
		// 혹시나 실패해도 누군가가 옮겨 줄 것이라고 믿고 그냥 나간다.
		InterlockedCompareExchange128((volatile LONG64*)RealTail, LocalTail.Count + 1, (LONG64)pNewNode, (LONG64*)&LocalTail);

		return true;
	}

	template<typename DATA>
	inline bool CLockFreeQueue<DATA>::Dequeue(DATA * pOut)
	{
		// 큐에 데이터가 하나도 없으면 디큐 실패
		// 누가 넣고 있는 중일수도 있지만 아직은 삽입을 완료하지 못했다면 사이즈 0으로 간주한다.
		// Dequeue하기 전에 미리 사이즈 하나 차감해준다.
		LONG Size = InterlockedDecrement(&m_Size);
		if (Size < 0)
		{						
		   InterlockedIncrement(&m_Size);
		   return false;
		}				

		__declspec(align(16)) NODE_PTR LocalTail;
		__declspec(align(16)) NODE_PTR LocalHead;
		NODE_PTR *RealTail = &m_Tail;
		NODE_PTR *RealHead = &m_Head;
		Node *pLocalNext;

		// Dequeue에 성공할 때까지 루프돌며 시도한다.
		while (true)
		{
			// 현재 Head와 Tail의 정보를 로컬로 복사해온다.
			LocalHead.Count = RealHead->Count;
			LocalHead.pNode = RealHead->pNode;

			LocalTail.Count = RealTail->Count;
			LocalTail.pNode = RealTail->pNode;

			// 현재 Head의 Next를 로컬로 복사
			pLocalNext = LocalHead.pNode->pNext;

			// 로컬 Head와 Tail이 같은지 비교
			// Head와 Tail이 같아질 수 있는 경우
			// 1. 현재 큐에 더미 노드를 제외하고 아무런 데이터가 없는경우
			// 2. 현재 큐에 새로운 노드가 연결되었지만 Tail이 아직 옮겨지지 않은 경우
			if (LocalHead.pNode == LocalTail.pNode)
			{
				// 1번의 경우 디큐할 데이터가 없는 것이므로 디큐 실패					
				if (pLocalNext == nullptr)
				{
					if (m_Size > 0)
					{					
						continue;
					}

					if (RealHead->pNode == RealTail->pNode && RealHead->pNode->pNext == nullptr && RealTail->pNode->pNext == nullptr)
					{										
						return false;
					}
				}

				// 2번의 경우 디큐할 데이터가 있는 경우로 다른 스레드가 노드 인큐는 했지만 아직 Tail을 옮기지 않은 것이다.
				// 그러한 경우에는 여기서 Tail을 옮기려고 시도해본다.
				// 만약 여기서 Tail을 옮기지 않고, 다른 어느 스레드도 Tail을 옮기지 못한 상태에서 Dequeue가 성공해버리면 Head가 Tail보다 앞서는 상황이 발생할 수 있다.
				// Head는 항상 리스트의 첫번째 노드를 가리켜야 하므로 위와 같은 상황은 발생해서는 안된다.
				InterlockedCompareExchange128(
					(volatile LONG64*)RealTail,
					LocalTail.Count + 1,
					(LONG64)(pLocalNext),
					(LONG64*)&LocalTail
				);
			}
			else
			{
				// Head는 항상 더미노드를 가리키므로, 실제로 디큐해야할 데이터는 Next의 데이터이다.
				*pOut = pLocalNext->data;

				// Head를 이동 시도
				// Head가 다른 스레드에 의해서 변화되지 않았다면 반복문 탈출, 변화되었다면 루프 위로 올라가서 재시도
				if (InterlockedCompareExchange128(
					(volatile LONG64*)RealHead,
					LocalHead.Count + 1,
					(LONG64)(pLocalNext),
					(LONG64*)&LocalHead)
					)
				{
					break;
				}
			}
		}

		// 이 곳까지 왔다는 것은 큐에 데이터가 존재했고, 해당 데이터를 성공적으로 외부로 전달했음을 의미
		// 따라서 이전 더미노드를 제거해준다. 
		// 이전 더미노드 == 로컬로 받아둔 Head					
		m_pNodePool->Free(LocalHead.pNode);

		// 디큐 성공적
		return true;
	}

	

	template<typename DATA>
	inline bool CLockFreeQueue<DATA>::Peek(DATA * pOut, ULONG NodePos)
	{
		// NodePos : 현재 Head의 상대적인 위치 ex) 5가 전달되면 Head로부터 pNext로 5회 이동
		// Peek 함수는 스레드 세이프하지 않다. 크래쉬나지 않게만 만들자.			
		// 사용할 때 조심해서 사용해야 할 함수이다.
		// 큐에 데이터가 하나도 없으면 Peek 실패
		// 누가 넣고 있는 중일수도 있지만 아직은 삽입을 완료하지 못했다면 사이즈 0으로 간주한다.			
		if (NodePos == 0 || NodePos > m_Size || m_Size <= 0)
			return false;

		// 현재 Head와 Tail의 정보를 멤버로 받아온다.
		__declspec(align(16)) NODE_PTR LocalTail;
		__declspec(align(16)) NODE_PTR LocalHead;

		// Peek은 스레드 세이프하지 않기 떄문에 로컬 정보를 기반으로 인자로 전달된 위치의 노드를 반환해준다.
		LocalHead.pNode = m_Head.pNode;
		LocalHead.Count = m_Head.Count;
		LocalTail.pNode = m_Tail.pNode;
		LocalTail.Count = m_Tail.Count;

		if (LocalHead.pNode == LocalTail.pNode)
		{
			if (LocalHead.pNode->pNext == nullptr)
			{
				// 로컬 헤드와 테일이 같은 곳을 찌르고 있는데 해당 Next가 Null이라면 일단 그 순간의 정보로는 Peek할 노드가 없는 것이다.					
				// 위에서 사이즈를 체크하고 들어왔지만, 언제든 다른 스레드가 인큐와 디큐를 수행하여 사이즈가 변할 수 있기 때문에 한번 더 체크
				if (m_Size <= 0)
					return false;
			}
		}

		// NodePos 만큼 Next를 찔러 이동해 가며 해당 노드를 반환한다.			
		Node *pOutNode = LocalHead.pNode;
		for (ULONG i = 0; i < NodePos; i++)
		{
			pOutNode = pOutNode->pNext;

			// 한번 더 널 체크 
			if (pOutNode == nullptr)
				return false;
		}

		// NodePos의 노드 데이터를 반환
		*pOut = pOutNode->data;

		return true;
	}

	template<typename DATA>
	inline ULONG CLockFreeQueue<DATA>::size() const
	{
		return m_Size;
	}

}