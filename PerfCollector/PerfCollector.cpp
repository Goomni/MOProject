#include "PerfCollector.h"
#include "CrashDump\CrashDump.h"
#include <psapi.h>
#include <strsafe.h>
#include <PdhMsg.h>
#pragma once

namespace GGM
{
	GGM::CPerfCollector::CPerfCollector()
	{
		// ���� �غ�
		PDH_STATUS status;
		if (status = PdhOpenQuery(NULL, 0, &m_hQuery))
			CCrashDump::ForceCrash();

		// �����͸� ������ ���μ��� �̸� ���
		DWORD ret;
		ret = GetModuleBaseName(GetCurrentProcess(), NULL, m_ProcessName, MAX_PATH);

		TCHAR *pBuf = m_ProcessName + ret - 4;
		*pBuf = 0;

		if (ret == 0)
			CCrashDump::ForceCrash();
	}
	
	GGM::CPerfCollector::~CPerfCollector()
	{
		// ���� ���� �� ���ҽ� ����
		if (m_hQuery)
			PdhCloseQuery(m_hQuery);

		// ������ ����ü ����
		if (m_CounterUmap.size() > 0)
		{
			auto iter_cur = m_CounterUmap.begin();
			auto iter_end = m_CounterUmap.end();

			while (iter_cur != iter_end)
			{
				PerfData *pData = iter_cur->second;			
				
				if (pData->IsArray == true && pData->pItems != nullptr)
				{
					free(pData->pItems);
				}

				delete pData;

				++iter_cur;
			}
		}
	}
	
	bool GGM::CPerfCollector::AddCounter(const WCHAR * CounterPath, int DataType, DWORD Format, bool IsArray)
	{
		// ������ ���� ������ �����Ҵ� �޴´�.
		PerfData *pData = new PerfData;

		if (pData == nullptr)
			return false;

		// ���� ���� ī���� ��� ���� 
		TCHAR CounterPathBuf[4096];
		if (_tcscmp(CounterPath, PROCESS_PRIVATE_BYTES) == 0)
		{					
			// ���μ��� �̸��� �ʿ��� ��� ���μ��� �̸� ���� �־���
			StringCchPrintf(CounterPathBuf, 4096, CounterPath, m_ProcessName);
		}
		else
		{		
			StringCchPrintf(CounterPathBuf, 4096, CounterPath);
		}

		// ���� ���� ī������ ������ Ÿ�� ���� 
		// �����Ӱ� ���� �� �ִ�. 
		// UMAP�� Ű���ȴ�.
		pData->DataType = DataType;

		// �ѹ��� ������ �������� �����͸� ������������ ���� ����
		pData->IsArray = IsArray;		

		// ī���� �߰� 		
		PDH_STATUS status;
		status = PdhAddCounter(m_hQuery, CounterPathBuf, 0, &pData->hCounter);
		if (status != ERROR_SUCCESS)
		{
			delete pData;
			return false;		
		}

		// ���ʷ� �ѹ� ���� �����ش�.
		status = PdhCollectQueryData(m_hQuery);
		if (status != ERROR_SUCCESS)
		{
			delete pData;
			return false;
		}

		// �������� �����͸� �����ؾ� �ϸ� ������ �׸��� ������ �迭 ���̸� �̸� ����
		if (IsArray == true)
		{
			status = PdhGetFormattedCounterArray(
				pData->hCounter, // ���� ���� ī���� 
				Format, // ���� �������� ���� 
				&pData->BufferSize, // ���� ������, �ƿ����� ������ �����Ҵ��ؾ� �� ���ۻ���� �˷���
				&pData->ItemCount,  // �׸� ����, �ƿ����� ����� ������ �׸���� �˷���
				pData->pItems
			);

			if (status != PDH_MORE_DATA)
			{
				delete pData;
				return false;
			}

			// ���� ���� ũ�⸸ŭ �����Ҵ� 
			pData->pItems = (PDH_FMT_COUNTERVALUE_ITEM*)malloc(pData->BufferSize);

			if (pData->pItems == nullptr)
			{
				delete pData;
				return false;
			}
		}

		// ������ ���� 
		auto pair_insert = m_CounterUmap.insert({ DataType, pData });

		if (pair_insert.second == false)
		{
			delete pData;
			return false;
		}
		
		return true;
	}
	
	bool GGM::CPerfCollector::CollectQueryData()
	{
		// ���� ������.
		PDH_STATUS status;
		status = PdhCollectQueryData(m_hQuery);
		if (status != ERROR_SUCCESS)
			return false;

		return true;
	}
	
	bool GGM::CPerfCollector::GetFormattedData(int DataType, PDH_FMT_COUNTERVALUE * pCounterValue, DWORD Format)
	{
		// ����� ����ü �˻� 
		auto iter_find = m_CounterUmap.find(DataType);

		if (iter_find == m_CounterUmap.end())
			return false;

		PerfData *pData = iter_find->second;

		// �迭 �����Ͷ�� GetFormattedDataArray�� ���� �� 
		if (pData->IsArray == true)
			return false;

		// ���� ������ ��´�.
		PDH_STATUS status;
		status = PdhGetFormattedCounterValue(pData->hCounter, Format, NULL, pCounterValue);

		if (status != ERROR_SUCCESS)
			return false;

		return true;
	}
	
	bool GGM::CPerfCollector::GetFormattedDataArray(int DataType, PDH_FMT_COUNTERVALUE_ITEM **pItems, DWORD * pItemCount, DWORD Format)
	{
		// ����� ����ü �˻� 
		auto iter_find = m_CounterUmap.find(DataType);

		if (iter_find == m_CounterUmap.end())
			return false;

		PerfData *pData = iter_find->second;

		// �迭 �����Ͱ� �ƴ϶�� GetFormattedData�� ���� �� 
		if (pData->IsArray == false)
			return false;

		// ���� ������ ��´�.
		PDH_STATUS status;
		status = PdhGetFormattedCounterArray(pData->hCounter, Format, &pData->BufferSize, &pData->ItemCount, pData->pItems);

		if (status != ERROR_SUCCESS)
			return false;

		*pItems = pData->pItems;
		*pItemCount = pData->ItemCount;

		return true;
	}

}
