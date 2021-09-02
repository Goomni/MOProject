#ifndef __GGM_LIST_QUEUE_H__
#define __GGM_LIST_QUEUE_H__

#include "MemoryPool\MemoryPool.h"

namespace GGM
{
	// 싱글 스레드 전용으로 사용할 가벼운 템플릿 큐 
	template <class Data>
	class CListQueue
	{
		// 리스트 기반 큐의 노드
		struct Node
		{
			Data data;
			Node *pNext;
		};

	public:

		CListQueue();		
		
		CListQueue(int InitialNode, bool IsSerialize);

		virtual ~CListQueue();

		void    InitQueue();

		void    Enqueue(Data data);
		void    Enqueue(Data data, bool NoInterlock);

		bool    Dequeue(Data* pOut);
		bool    Dequeue(Data* pOut, bool NoInterlock);

		ULONGLONG  size() const;

		void    clear();

	private:

		// 큐의 노드를 할당해 줄 메모리 풀
		CMemoryPool<Node>  *m_pNodepool = nullptr;

		// 큐의 현재 사이즈
		ULONGLONG           m_size = 0;

		// 큐의 헤드와 테일
		Node               *m_pHead = nullptr;
		Node               *m_pTail = nullptr;

	};
	
	template<class Data>
	inline CListQueue<Data>::CListQueue()
	{
		InitQueue();
	}

	template<class Data>
	inline CListQueue<Data>::CListQueue(int InitialNode, bool IsSerialize)
	{
		//  int InitialNode [메모리 풀에 미리 할당해둘 오브젝트의 개수]
		//  bool IsSerialize [ Heap Alloc 동기화 여부 ] 
		//  생성자에 전달된 옵션을 바탕으로 큐의 노드를 할당해 줄 메모리 풀 생성
		m_pNodepool = new CMemoryPool<Node>(InitialNode, IsSerialize);

		if (m_pNodepool == nullptr)
			throw - 1;

		// 더미 노드 생성
		Node *pDummy = m_pNodepool->Alloc();
		pDummy->data = 0;
		pDummy->pNext = nullptr;
		m_pHead = m_pTail = pDummy;
	}

	template<class Data>
	inline CListQueue<Data>::~CListQueue()
	{
		Node *pNode = m_pHead;

		while (pNode != nullptr)
		{
			Node *pFreeNode = pNode;
			pNode = pNode->pNext;
			m_pNodepool->Free(pFreeNode);
		}

		delete m_pNodepool;
	}

	template<class Data>
	inline void CListQueue<Data>::InitQueue()
	{
		//  int InitialNode [메모리 풀에 미리 할당해둘 오브젝트의 개수]
		//  bool IsSerialize [ Heap Alloc 동기화 여부 ] 
		//  생성자에 전달된 옵션을 바탕으로 큐의 노드를 할당해 줄 메모리 풀 생성
		m_pNodepool = new CMemoryPool<Node>(0, false);

		if (m_pNodepool == nullptr)
			throw - 1;

		// 더미 노드 생성
		Node *pDummy = m_pNodepool->Alloc();
		pDummy->data = 0;
		pDummy->pNext = nullptr;
		m_pHead = m_pTail = pDummy;
	}

	template<class Data>
	inline void CListQueue<Data>::Enqueue(Data data)
	{
		// 새로운 노드를 Alloc		
		Node *pNewNode = m_pNodepool->Alloc();

		// 노드에 데이터를 채움
		pNewNode->data = data;
		pNewNode->pNext = nullptr;

		// 현재 Tail 로컬로 받음
		Node *pLocalTail = m_pTail;

		// Tail을 미리 다음 노드로 옮김
		m_pTail = pNewNode;		

		// 삽입 노드를 큐에 연결 
		pLocalTail->pNext = pNewNode;

		// 큐 사이즈 증가 
		InterlockedIncrement(&m_size);		
	}

	template<class Data>
	inline void CListQueue<Data>::Enqueue(Data data, bool NoInterlock)
	{
		// 새로운 노드를 Alloc		
		Node *pNewNode = m_pNodepool->Alloc();

		// 노드에 데이터를 채움
		pNewNode->data = data;
		pNewNode->pNext = nullptr;

		// 현재 Tail 로컬로 받음
		Node *pLocalTail = m_pTail;

		// Tail을 미리 다음 노드로 옮김
		m_pTail = pNewNode;

		// 삽입 노드를 큐에 연결 
		pLocalTail->pNext = pNewNode;

		// 큐 사이즈 증가 
		++m_size;
	}

	template<class Data>
	inline bool CListQueue<Data>::Dequeue(Data * pOut)
	{
		// 큐가 비었다면 return false
		if (m_size == 0)
			return false;

		// 로컬로 Head를 받음
		Node *pLocalHead = m_pHead;		

		// 현재 Head는 더미이므로 다음 노드의 데이터를 반환 
		*pOut = pLocalHead->pNext->data;

		// Head를 이동 
		m_pHead = pLocalHead->pNext;

		// 이전 Head를 메모리 풀에 반환
		m_pNodepool->Free(pLocalHead);

		InterlockedDecrement(&m_size);

		return true;
	}

	template<class Data>
	inline bool CListQueue<Data>::Dequeue(Data *pOut, bool NoInterlock)
	{
		// 큐가 비었다면 return false
		if (m_size == 0)
			return false;

		// 로컬로 Head를 받음
		Node *pLocalHead = m_pHead;

		// 현재 Head는 더미이므로 다음 노드의 데이터를 반환 
		*pOut = pLocalHead->pNext->data;

		// Head를 이동 
		m_pHead = pLocalHead->pNext;

		// 이전 Head를 메모리 풀에 반환
		m_pNodepool->Free(pLocalHead);

		--m_size;

		return true;
	}

	template<class Data>
	inline ULONGLONG CListQueue<Data>::size() const
	{
		return m_size;
	}

	template<class Data>
	inline void CListQueue<Data>::clear()
	{
		Node *pNode = m_pHead->pNext;
		while (pNode != nullptr)
		{
			Node *pFreeNode = pNode;
			pNode = pNode->pNext;
			m_pNodepool->Free(pFreeNode);			
		}

		m_pTail = m_pHead;
		m_pHead->pNext = nullptr;
		m_size = 0;
				
	}

}



#endif
