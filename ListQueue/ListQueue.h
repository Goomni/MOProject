#ifndef __GGM_LIST_QUEUE_H__
#define __GGM_LIST_QUEUE_H__

#include "MemoryPool\MemoryPool.h"

namespace GGM
{
	// �̱� ������ �������� ����� ������ ���ø� ť 
	template <class Data>
	class CListQueue
	{
		// ����Ʈ ��� ť�� ���
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

		// ť�� ��带 �Ҵ��� �� �޸� Ǯ
		CMemoryPool<Node>  *m_pNodepool = nullptr;

		// ť�� ���� ������
		ULONGLONG           m_size = 0;

		// ť�� ���� ����
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
		//  int InitialNode [�޸� Ǯ�� �̸� �Ҵ��ص� ������Ʈ�� ����]
		//  bool IsSerialize [ Heap Alloc ����ȭ ���� ] 
		//  �����ڿ� ���޵� �ɼ��� �������� ť�� ��带 �Ҵ��� �� �޸� Ǯ ����
		m_pNodepool = new CMemoryPool<Node>(InitialNode, IsSerialize);

		if (m_pNodepool == nullptr)
			throw - 1;

		// ���� ��� ����
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
		//  int InitialNode [�޸� Ǯ�� �̸� �Ҵ��ص� ������Ʈ�� ����]
		//  bool IsSerialize [ Heap Alloc ����ȭ ���� ] 
		//  �����ڿ� ���޵� �ɼ��� �������� ť�� ��带 �Ҵ��� �� �޸� Ǯ ����
		m_pNodepool = new CMemoryPool<Node>(0, false);

		if (m_pNodepool == nullptr)
			throw - 1;

		// ���� ��� ����
		Node *pDummy = m_pNodepool->Alloc();
		pDummy->data = 0;
		pDummy->pNext = nullptr;
		m_pHead = m_pTail = pDummy;
	}

	template<class Data>
	inline void CListQueue<Data>::Enqueue(Data data)
	{
		// ���ο� ��带 Alloc		
		Node *pNewNode = m_pNodepool->Alloc();

		// ��忡 �����͸� ä��
		pNewNode->data = data;
		pNewNode->pNext = nullptr;

		// ���� Tail ���÷� ����
		Node *pLocalTail = m_pTail;

		// Tail�� �̸� ���� ���� �ű�
		m_pTail = pNewNode;		

		// ���� ��带 ť�� ���� 
		pLocalTail->pNext = pNewNode;

		// ť ������ ���� 
		InterlockedIncrement(&m_size);		
	}

	template<class Data>
	inline void CListQueue<Data>::Enqueue(Data data, bool NoInterlock)
	{
		// ���ο� ��带 Alloc		
		Node *pNewNode = m_pNodepool->Alloc();

		// ��忡 �����͸� ä��
		pNewNode->data = data;
		pNewNode->pNext = nullptr;

		// ���� Tail ���÷� ����
		Node *pLocalTail = m_pTail;

		// Tail�� �̸� ���� ���� �ű�
		m_pTail = pNewNode;

		// ���� ��带 ť�� ���� 
		pLocalTail->pNext = pNewNode;

		// ť ������ ���� 
		++m_size;
	}

	template<class Data>
	inline bool CListQueue<Data>::Dequeue(Data * pOut)
	{
		// ť�� ����ٸ� return false
		if (m_size == 0)
			return false;

		// ���÷� Head�� ����
		Node *pLocalHead = m_pHead;		

		// ���� Head�� �����̹Ƿ� ���� ����� �����͸� ��ȯ 
		*pOut = pLocalHead->pNext->data;

		// Head�� �̵� 
		m_pHead = pLocalHead->pNext;

		// ���� Head�� �޸� Ǯ�� ��ȯ
		m_pNodepool->Free(pLocalHead);

		InterlockedDecrement(&m_size);

		return true;
	}

	template<class Data>
	inline bool CListQueue<Data>::Dequeue(Data *pOut, bool NoInterlock)
	{
		// ť�� ����ٸ� return false
		if (m_size == 0)
			return false;

		// ���÷� Head�� ����
		Node *pLocalHead = m_pHead;

		// ���� Head�� �����̹Ƿ� ���� ����� �����͸� ��ȯ 
		*pOut = pLocalHead->pNext->data;

		// Head�� �̵� 
		m_pHead = pLocalHead->pNext;

		// ���� Head�� �޸� Ǯ�� ��ȯ
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
