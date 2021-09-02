#pragma once
#include <Windows.h>
#include "MemoryPool\MemoryPool.h"

namespace GGM
{
	template <typename DATA>
	class CLockFreeStack
	{
	public:

		CLockFreeStack() = delete; // 기본 생성자는 사용하지 않는다.

		CLockFreeStack(int NumOfMemBlock, bool bPlacementNew = false);		

		virtual ~CLockFreeStack();		

		void Push(DATA data);		

		bool Pop(DATA *pDataOut);		

		LONG size();		

		bool empty();		

	protected:

		// 리스트 기반의 스택 노드 
		struct Node
		{
			Node *pNext = nullptr;
			DATA data;
		};

		// LockFree 알고리즘 구현을 위한 탑 
		// [스택 탑 포인터 + 유니크 식별자]로 이루어진 128비트 구조체
		// InterlockedCompareExchange128에 사용하기 위함
		struct TOP
		{
			Node   *pTop;			
			LONG64  Count = 0; // ABA 이슈를 막기위한 TOP 유니크 식별자
		};	

	protected:

		__declspec(align(16)) TOP                  m_Top; // 스택의 탑				                      
							  LONG                 m_size = 0; // 스택의 사이즈 (노드 카운트)		
							  HANDLE               m_hHeap; // 락프리 스택 노드 전용 힙 							
							  CMemoryPool<Node>   *m_pNodePool; // 노드를 할당해 줄 메모리 풀	  				  							  												
							
	
	};		

	template<typename DATA>
	inline CLockFreeStack<DATA>::CLockFreeStack(int NumOfMemBlock, bool bPlacementNew)
	{
		// 스택의 노드를 할당하기 위한 메모리 할당 풀을 동적할당하여 생성한다.
		// 생성자에 인자로 전달된 개수만큼 노드가 미리 생성된다.	
		m_pNodePool = new CMemoryPool<Node>(NumOfMemBlock, true);
	}

	template<typename DATA>
	inline CLockFreeStack<DATA>::~CLockFreeStack()
	{
		// 메모리 풀 정리	
		delete m_pNodePool;
	}

	template<typename DATA>
	inline void CLockFreeStack<DATA>::Push(DATA data)
	{
		// 노드를 할당받는다					
		Node *pPushNode = m_pNodePool->Alloc();

		// 데이터를 노드에 저장 
		pPushNode->data = data;

		Node *pSnapTop;
		TOP  *RealTop = &m_Top;

		// 락프리 알고리즘을 사용하여 Push에 성공할때까지 루프돈다.
		do
		{
			// 현재 스택의 탑을 로컬로 받아온다.
			// 락프리 스택을 사용할 때 Push에서는 ABA문제가 발생하지 않는다.
			// 카운트를 증가시키는 것은 Pop에서만 해주면 된다.				
			pSnapTop = RealTop->pTop;

			// 반환할 노드의 Next를 현재 스택의 탑으로 설정
			pPushNode->pNext = pSnapTop;		

			// CAS를 통해 원자적으로 Push를 시도한다.
		} while (InterlockedCompareExchange64((volatile LONG64*)RealTop, (LONG64)pPushNode, (LONG64)pSnapTop) != (LONG64)pSnapTop);

		InterlockedIncrement(&m_size);
	}

	template<typename DATA>
	inline bool CLockFreeStack<DATA>::Pop(DATA * pDataOut)
	{
		// 현재 스택에 아무것도 없으면 함수 호출 실패 
		// 다른 스레드에서 Push가 완벽하게 성공해야 사이즈가 증가하기 때문에 현재 사이즈가 0이면 데이터가 없는 것으로 간주		
		LONG size = InterlockedDecrement(&m_size);
		if (size < 0)
		{			
			InterlockedIncrement(&m_size);
			return false;
		}

		// 락프리 Pop을 수행하기 위해 필요한 변수들
		__declspec(align(16)) TOP PopNode;
		TOP		              *RealTop = &m_Top;

		do
		{
			// Pop연산을 수행할 현재 스택의 탑과 Pop연산을 수행한 카운트를 얻어온다.
			// ABA 문제를 해결하기 위해 Pop의 Count를 두어서 유니크하게 식별한다.			
			PopNode.Count = RealTop->Count;
			PopNode.pTop = RealTop->pTop;

			// 위에서 사이즈 확인했지만 한번 더 확인 
			if (PopNode.pTop == nullptr)
				return false;			

		} while (!InterlockedCompareExchange128((volatile LONG64*)RealTop, PopNode.Count + 1, (LONG64)(PopNode.pTop->pNext), (LONG64*)&PopNode));

		// 아웃변수에 데이터를 복사해주고 노드를 메모리 풀에 반환한다.
		*pDataOut = PopNode.pTop->data;

		// 노드를 할당받는다						
		m_pNodePool->Free(PopNode.pTop);		

		return true;
	}

	template<typename DATA>
	inline LONG CLockFreeStack<DATA>::size()
	{
		// 현재 스택의 사이즈 ( 노드 개수 반환 )
		return m_size;
	}

	template<typename DATA>
	inline bool CLockFreeStack<DATA>::empty()
	{
		// 현재 스택의 사이즈 ( 노드 개수 반환 )
		return m_size;
	}
}

