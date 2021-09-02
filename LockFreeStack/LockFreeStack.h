#pragma once
#include <Windows.h>
#include "MemoryPool\MemoryPool.h"

namespace GGM
{
	template <typename DATA>
	class CLockFreeStack
	{
	public:

		CLockFreeStack() = delete; // �⺻ �����ڴ� ������� �ʴ´�.

		CLockFreeStack(int NumOfMemBlock, bool bPlacementNew = false);		

		virtual ~CLockFreeStack();		

		void Push(DATA data);		

		bool Pop(DATA *pDataOut);		

		LONG size();		

		bool empty();		

	protected:

		// ����Ʈ ����� ���� ��� 
		struct Node
		{
			Node *pNext = nullptr;
			DATA data;
		};

		// LockFree �˰��� ������ ���� ž 
		// [���� ž ������ + ����ũ �ĺ���]�� �̷���� 128��Ʈ ����ü
		// InterlockedCompareExchange128�� ����ϱ� ����
		struct TOP
		{
			Node   *pTop;			
			LONG64  Count = 0; // ABA �̽��� �������� TOP ����ũ �ĺ���
		};	

	protected:

		__declspec(align(16)) TOP                  m_Top; // ������ ž				                      
							  LONG                 m_size = 0; // ������ ������ (��� ī��Ʈ)		
							  HANDLE               m_hHeap; // ������ ���� ��� ���� �� 							
							  CMemoryPool<Node>   *m_pNodePool; // ��带 �Ҵ��� �� �޸� Ǯ	  				  							  												
							
	
	};		

	template<typename DATA>
	inline CLockFreeStack<DATA>::CLockFreeStack(int NumOfMemBlock, bool bPlacementNew)
	{
		// ������ ��带 �Ҵ��ϱ� ���� �޸� �Ҵ� Ǯ�� �����Ҵ��Ͽ� �����Ѵ�.
		// �����ڿ� ���ڷ� ���޵� ������ŭ ��尡 �̸� �����ȴ�.	
		m_pNodePool = new CMemoryPool<Node>(NumOfMemBlock, true);
	}

	template<typename DATA>
	inline CLockFreeStack<DATA>::~CLockFreeStack()
	{
		// �޸� Ǯ ����	
		delete m_pNodePool;
	}

	template<typename DATA>
	inline void CLockFreeStack<DATA>::Push(DATA data)
	{
		// ��带 �Ҵ�޴´�					
		Node *pPushNode = m_pNodePool->Alloc();

		// �����͸� ��忡 ���� 
		pPushNode->data = data;

		Node *pSnapTop;
		TOP  *RealTop = &m_Top;

		// ������ �˰����� ����Ͽ� Push�� �����Ҷ����� ��������.
		do
		{
			// ���� ������ ž�� ���÷� �޾ƿ´�.
			// ������ ������ ����� �� Push������ ABA������ �߻����� �ʴ´�.
			// ī��Ʈ�� ������Ű�� ���� Pop������ ���ָ� �ȴ�.				
			pSnapTop = RealTop->pTop;

			// ��ȯ�� ����� Next�� ���� ������ ž���� ����
			pPushNode->pNext = pSnapTop;		

			// CAS�� ���� ���������� Push�� �õ��Ѵ�.
		} while (InterlockedCompareExchange64((volatile LONG64*)RealTop, (LONG64)pPushNode, (LONG64)pSnapTop) != (LONG64)pSnapTop);

		InterlockedIncrement(&m_size);
	}

	template<typename DATA>
	inline bool CLockFreeStack<DATA>::Pop(DATA * pDataOut)
	{
		// ���� ���ÿ� �ƹ��͵� ������ �Լ� ȣ�� ���� 
		// �ٸ� �����忡�� Push�� �Ϻ��ϰ� �����ؾ� ����� �����ϱ� ������ ���� ����� 0�̸� �����Ͱ� ���� ������ ����		
		LONG size = InterlockedDecrement(&m_size);
		if (size < 0)
		{			
			InterlockedIncrement(&m_size);
			return false;
		}

		// ������ Pop�� �����ϱ� ���� �ʿ��� ������
		__declspec(align(16)) TOP PopNode;
		TOP		              *RealTop = &m_Top;

		do
		{
			// Pop������ ������ ���� ������ ž�� Pop������ ������ ī��Ʈ�� ���´�.
			// ABA ������ �ذ��ϱ� ���� Pop�� Count�� �ξ ����ũ�ϰ� �ĺ��Ѵ�.			
			PopNode.Count = RealTop->Count;
			PopNode.pTop = RealTop->pTop;

			// ������ ������ Ȯ�������� �ѹ� �� Ȯ�� 
			if (PopNode.pTop == nullptr)
				return false;			

		} while (!InterlockedCompareExchange128((volatile LONG64*)RealTop, PopNode.Count + 1, (LONG64)(PopNode.pTop->pNext), (LONG64*)&PopNode));

		// �ƿ������� �����͸� �������ְ� ��带 �޸� Ǯ�� ��ȯ�Ѵ�.
		*pDataOut = PopNode.pTop->data;

		// ��带 �Ҵ�޴´�						
		m_pNodePool->Free(PopNode.pTop);		

		return true;
	}

	template<typename DATA>
	inline LONG CLockFreeStack<DATA>::size()
	{
		// ���� ������ ������ ( ��� ���� ��ȯ )
		return m_size;
	}

	template<typename DATA>
	inline bool CLockFreeStack<DATA>::empty()
	{
		// ���� ������ ������ ( ��� ���� ��ȯ )
		return m_size;
	}
}

