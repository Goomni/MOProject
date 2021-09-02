#pragma once
#include <Windows.h>
#include "MemoryPool\MemoryPool.h"
#include "CrashDump\CrashDump.h"

namespace GGM
{	
	/////////////////////////////////////////////////////////////////////////////////////////////////////////
	// �� ���� ť! ( Lock-free queue as a singly-linkedlist with Head and Tail pointers)
	// - ������ �ݵ�� ����ؾ� �� ����
	//  1. ť�� ��尡 �ϳ��� �������� �ʴ� ���� ����. �������� �־ ���� ��� �ϳ��� �����Ѵ�.
	//  2. ������ ��ť�� �����ߴٸ� ���� �׻� ť�� ����� ����Ǿ� �־�� �Ѵ�. �߰��� ��尣 ������ �������� ���� ���� ����� �Ѵ�.
	//  ( Tail�� �Ű����� �ʾ��� ���� �ִ�. Enqueue�� �������� �����尡 �ű��� ���ߴٸ� �ٸ� �����尡 �Ű��� ���� �ִ�. )
	//  3. ��ť�� �׻� Tail�� �������� �����Ѵ�.
	//  4. ��ť�� �׻� Head�� �������� �����Ѵ�.
	//  5. Head�� �׻� ����Ʈ�� ù��° ��带 ����Ų��. ( ���� ��� )
	//  6. Tail�� �׻� ����Ʈ ������ ��带 ����Ų��. 
	/////////////////////////////////////////////////////////////////////////////////////////////////////////
	template <typename DATA>
	class CLockFreeQueue
	{
	public:		

		// �⺻ �����ڴ� ������� �ʴ´�.
		CLockFreeQueue() = default;

		// �������� ���ڷ� �̸� ������ �� ����� ������ ���� �޴´�.
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

		// Head�� Tail ���� ����ü
		struct NODE_PTR
		{
			Node     *pNode = nullptr;
			LONG64   Count = 0;
		};

	protected:

		__declspec(align(16)) NODE_PTR             m_Head; // �׻� ����Ʈ�� ù��° ��带 ����Ų��.
		__declspec(align(16)) NODE_PTR             m_Tail; // �׻� ����Ʈ ������ ��带 ����Ų��.		
							  LONG                 m_Size = 0; // ���� ť�� ������ (��� ����)		                  
							  LONG                 m_MaxSize = 0;
							  CMemoryPool<Node>    *m_pNodePool; // ��带 �Ҵ��� �� �޸� Ǯ         
	
	};
	template<typename DATA>
	inline CLockFreeQueue<DATA>::CLockFreeQueue(int NumOfMemBlock, ULONG MaxSize, bool bIsPlacementNew)
	{		
		InitLockFreeQueue(NumOfMemBlock, MaxSize, bIsPlacementNew);		
	}
	template<typename DATA>
	inline CLockFreeQueue<DATA>::~CLockFreeQueue()
	{
		// �޸� Ǯ ����
		delete m_pNodePool;
	}
	template<typename DATA>
	inline void CLockFreeQueue<DATA>::InitLockFreeQueue(int NumOfMemBlock, ULONG MaxSize, bool bIsPlacementNew)
	{
		// ť�� �ִ� ������ ����
		m_MaxSize = MaxSize;

		// �޸� Ǯ ����
		m_pNodePool = new CMemoryPool<Node>(NumOfMemBlock, true);

		// ���� ��� ����			
		Node* pDummyNode = m_pNodePool->Alloc();
		pDummyNode->pNext = nullptr;

		// Head�� Tail�� ���̳�带 ����Ű�� �Ѵ�.
		m_Head.pNode = m_Tail.pNode = pDummyNode;
	}

	template<typename DATA>
	inline void CLockFreeQueue<DATA>::ClearLockFreeQueue()
	{
		// ������ ť�� ���� �������� �ʰ� �ʱ���·� ������ ���
		// ���� ť�� �ִ� ��带 ���� �޸�Ǯ�� ��ȯ�Ѵ�.
		Node* pFreeNode = m_Head.pNode->pNext;
		while (pFreeNode != nullptr)
		{
			m_pNodePool->Free(pFreeNode);
			pFreeNode = pFreeNode->pNext;
		}

		// ���, ������ �ʱ�ȭ�Ѵ�.
		m_Head.Count = 0;
		m_Tail.Count = 0;
		m_Head.pNode->pNext = nullptr;
		m_Size = 0;
	}

	template<typename DATA>
	inline bool CLockFreeQueue<DATA>::Enqueue(DATA data)
	{
		// ���� ť�� ����� �� á�ٸ� �� �̻� ��ť �Ұ�
		if (m_Size > m_MaxSize)
		{			
			return false;
		}

		// �� ��带 �Ҵ�޴´�.				
		Node *pNewNode = m_pNodePool->Alloc();

		// �Ҵ���� ��忡 �����͸� ä���.
		pNewNode->data = data;

		// ����� Next�� nullptr�� �����Ѵ�. ����Ʈ�� ���� �׻� null�̾�� �Ѵ�.
		// ������ť ���� �ڵ� �� Next �����͸� nullptr�� �����ϴ� �κ��� �̰��� �����ؾ߸� �Ѵ�.		
		pNewNode->pNext = nullptr;

		// Enqueue�� ������ ������ �������� �õ��Ѵ�.
		__declspec(align(16)) NODE_PTR LocalTail;
		NODE_PTR *RealTail = &m_Tail;
		Node *pLocalNext;

		while (true)
		{
			// Tail�� Next�� ������ ���÷� ���´�.				
			LocalTail.Count = RealTail->Count;
			LocalTail.pNode = RealTail->pNode;
			pLocalNext = LocalTail.pNode->pNext;

			// Tail�� Next�� NULL�� ��쿡�� ��ť�� ������ �� �ִ�.
			if (pLocalNext == nullptr)
			{
				// Tail�� Next�� ��ť�� ���� ����! 
				// ���ῡ �����ߴٸ� ���� Ż��
				if (InterlockedCompareExchange64((volatile LONG64*)&(RealTail->pNode->pNext), (LONG64)pNewNode, (LONG64)nullptr) == (LONG64)nullptr)
				{
					// Enqueue �Ϸ� �� ������ ����						
					InterlockedIncrement(&m_Size);
					break;
				}
			}
			else
			{
				// Tail�� Next�� nullptr�� �ƴ϶�� �������� �� ��� ������ �Ϸ������� Tail�� ���� �ű��� ���� ����̴�.
				// ���� �ű� �� �ִٸ� �ű⵵�� �õ��غ���.
				InterlockedCompareExchange128(
					(volatile LONG64*)RealTail,
					LocalTail.Count + 1,
					(LONG64)pLocalNext,
					(LONG64*)&LocalTail
				);
			}

		}

		// ������ Ż���ߴٴ� ���� Enqueue�� ���������� �ϼ��ߴٴ� ���̴�.
		// ���� ���� ���� Tail�� �Ű��ִ� ���̴�.
		// Ȥ�ó� �����ص� �������� �Ű� �� ���̶�� �ϰ� �׳� ������.
		InterlockedCompareExchange128((volatile LONG64*)RealTail, LocalTail.Count + 1, (LONG64)pNewNode, (LONG64*)&LocalTail);

		return true;
	}

	template<typename DATA>
	inline bool CLockFreeQueue<DATA>::Dequeue(DATA * pOut)
	{
		// ť�� �����Ͱ� �ϳ��� ������ ��ť ����
		// ���� �ְ� �ִ� ���ϼ��� ������ ������ ������ �Ϸ����� ���ߴٸ� ������ 0���� �����Ѵ�.
		// Dequeue�ϱ� ���� �̸� ������ �ϳ� �������ش�.
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

		// Dequeue�� ������ ������ �������� �õ��Ѵ�.
		while (true)
		{
			// ���� Head�� Tail�� ������ ���÷� �����ؿ´�.
			LocalHead.Count = RealHead->Count;
			LocalHead.pNode = RealHead->pNode;

			LocalTail.Count = RealTail->Count;
			LocalTail.pNode = RealTail->pNode;

			// ���� Head�� Next�� ���÷� ����
			pLocalNext = LocalHead.pNode->pNext;

			// ���� Head�� Tail�� ������ ��
			// Head�� Tail�� ������ �� �ִ� ���
			// 1. ���� ť�� ���� ��带 �����ϰ� �ƹ��� �����Ͱ� ���°��
			// 2. ���� ť�� ���ο� ��尡 ����Ǿ����� Tail�� ���� �Ű����� ���� ���
			if (LocalHead.pNode == LocalTail.pNode)
			{
				// 1���� ��� ��ť�� �����Ͱ� ���� ���̹Ƿ� ��ť ����					
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

				// 2���� ��� ��ť�� �����Ͱ� �ִ� ���� �ٸ� �����尡 ��� ��ť�� ������ ���� Tail�� �ű��� ���� ���̴�.
				// �׷��� ��쿡�� ���⼭ Tail�� �ű���� �õ��غ���.
				// ���� ���⼭ Tail�� �ű��� �ʰ�, �ٸ� ��� �����嵵 Tail�� �ű��� ���� ���¿��� Dequeue�� �����ع����� Head�� Tail���� �ռ��� ��Ȳ�� �߻��� �� �ִ�.
				// Head�� �׻� ����Ʈ�� ù��° ��带 �����Ѿ� �ϹǷ� ���� ���� ��Ȳ�� �߻��ؼ��� �ȵȴ�.
				InterlockedCompareExchange128(
					(volatile LONG64*)RealTail,
					LocalTail.Count + 1,
					(LONG64)(pLocalNext),
					(LONG64*)&LocalTail
				);
			}
			else
			{
				// Head�� �׻� ���̳�带 ����Ű�Ƿ�, ������ ��ť�ؾ��� �����ʹ� Next�� �������̴�.
				*pOut = pLocalNext->data;

				// Head�� �̵� �õ�
				// Head�� �ٸ� �����忡 ���ؼ� ��ȭ���� �ʾҴٸ� �ݺ��� Ż��, ��ȭ�Ǿ��ٸ� ���� ���� �ö󰡼� ��õ�
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

		// �� ������ �Դٴ� ���� ť�� �����Ͱ� �����߰�, �ش� �����͸� ���������� �ܺη� ���������� �ǹ�
		// ���� ���� ���̳�带 �������ش�. 
		// ���� ���̳�� == ���÷� �޾Ƶ� Head					
		m_pNodePool->Free(LocalHead.pNode);

		// ��ť ������
		return true;
	}

	

	template<typename DATA>
	inline bool CLockFreeQueue<DATA>::Peek(DATA * pOut, ULONG NodePos)
	{
		// NodePos : ���� Head�� ������� ��ġ ex) 5�� ���޵Ǹ� Head�κ��� pNext�� 5ȸ �̵�
		// Peek �Լ��� ������ ���������� �ʴ�. ũ�������� �ʰԸ� ������.			
		// ����� �� �����ؼ� ����ؾ� �� �Լ��̴�.
		// ť�� �����Ͱ� �ϳ��� ������ Peek ����
		// ���� �ְ� �ִ� ���ϼ��� ������ ������ ������ �Ϸ����� ���ߴٸ� ������ 0���� �����Ѵ�.			
		if (NodePos == 0 || NodePos > m_Size || m_Size <= 0)
			return false;

		// ���� Head�� Tail�� ������ ����� �޾ƿ´�.
		__declspec(align(16)) NODE_PTR LocalTail;
		__declspec(align(16)) NODE_PTR LocalHead;

		// Peek�� ������ ���������� �ʱ� ������ ���� ������ ������� ���ڷ� ���޵� ��ġ�� ��带 ��ȯ���ش�.
		LocalHead.pNode = m_Head.pNode;
		LocalHead.Count = m_Head.Count;
		LocalTail.pNode = m_Tail.pNode;
		LocalTail.Count = m_Tail.Count;

		if (LocalHead.pNode == LocalTail.pNode)
		{
			if (LocalHead.pNode->pNext == nullptr)
			{
				// ���� ���� ������ ���� ���� ��� �ִµ� �ش� Next�� Null�̶�� �ϴ� �� ������ �����δ� Peek�� ��尡 ���� ���̴�.					
				// ������ ����� üũ�ϰ� ��������, ������ �ٸ� �����尡 ��ť�� ��ť�� �����Ͽ� ����� ���� �� �ֱ� ������ �ѹ� �� üũ
				if (m_Size <= 0)
					return false;
			}
		}

		// NodePos ��ŭ Next�� �� �̵��� ���� �ش� ��带 ��ȯ�Ѵ�.			
		Node *pOutNode = LocalHead.pNode;
		for (ULONG i = 0; i < NodePos; i++)
		{
			pOutNode = pOutNode->pNext;

			// �ѹ� �� �� üũ 
			if (pOutNode == nullptr)
				return false;
		}

		// NodePos�� ��� �����͸� ��ȯ
		*pOut = pOutNode->data;

		return true;
	}

	template<typename DATA>
	inline ULONG CLockFreeQueue<DATA>::size() const
	{
		return m_Size;
	}

}