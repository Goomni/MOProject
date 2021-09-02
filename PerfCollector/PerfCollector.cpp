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
		// 쿼리 준비
		PDH_STATUS status;
		if (status = PdhOpenQuery(NULL, 0, &m_hQuery))
			CCrashDump::ForceCrash();

		// 데이터를 수집할 프로세스 이름 얻기
		DWORD ret;
		ret = GetModuleBaseName(GetCurrentProcess(), NULL, m_ProcessName, MAX_PATH);

		TCHAR *pBuf = m_ProcessName + ret - 4;
		*pBuf = 0;

		if (ret == 0)
			CCrashDump::ForceCrash();
	}
	
	GGM::CPerfCollector::~CPerfCollector()
	{
		// 쿼리 종료 및 리소스 정리
		if (m_hQuery)
			PdhCloseQuery(m_hQuery);

		// 데이터 구조체 정리
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
		// 쿼리를 날릴 데이터 동적할당 받는다.
		PerfData *pData = new PerfData;

		if (pData == nullptr)
			return false;

		// 쿼리 날릴 카운터 경로 저장 
		TCHAR CounterPathBuf[4096];
		if (_tcscmp(CounterPath, PROCESS_PRIVATE_BYTES) == 0)
		{					
			// 프로세스 이름이 필요할 경우 프로세스 이름 같이 넣어줌
			StringCchPrintf(CounterPathBuf, 4096, CounterPath, m_ProcessName);
		}
		else
		{		
			StringCchPrintf(CounterPathBuf, 4096, CounterPath);
		}

		// 쿼리 날릴 카운터의 데이터 타입 저장 
		// 자유롭게 정할 수 있다. 
		// UMAP의 키가된다.
		pData->DataType = DataType;

		// 한번의 쿼리로 복수개의 데이터를 얻을것인지의 여부 저장
		pData->IsArray = IsArray;		

		// 카운터 추가 		
		PDH_STATUS status;
		status = PdhAddCounter(m_hQuery, CounterPathBuf, 0, &pData->hCounter);
		if (status != ERROR_SUCCESS)
		{
			delete pData;
			return false;		
		}

		// 최초로 한번 쿼리 날려준다.
		status = PdhCollectQueryData(m_hQuery);
		if (status != ERROR_SUCCESS)
		{
			delete pData;
			return false;
		}

		// 복수개의 데이터를 저장해야 하면 아이템 항목의 개수와 배열 길이를 미리 구함
		if (IsArray == true)
		{
			status = PdhGetFormattedCounterArray(
				pData->hCounter, // 쿼리 날릴 카운터 
				Format, // 뽑을 데이터의 포맷 
				&pData->BufferSize, // 버퍼 사이즈, 아웃변수 지금은 동적할당해야 할 버퍼사이즈를 알려줌
				&pData->ItemCount,  // 항목 개수, 아웃변수 결과의 아이템 항목수를 알려줌
				pData->pItems
			);

			if (status != PDH_MORE_DATA)
			{
				delete pData;
				return false;
			}

			// 얻은 버퍼 크기만큼 동적할당 
			pData->pItems = (PDH_FMT_COUNTERVALUE_ITEM*)malloc(pData->BufferSize);

			if (pData->pItems == nullptr)
			{
				delete pData;
				return false;
			}
		}

		// 데이터 저장 
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
		// 쿼리 날린다.
		PDH_STATUS status;
		status = PdhCollectQueryData(m_hQuery);
		if (status != ERROR_SUCCESS)
			return false;

		return true;
	}
	
	bool GGM::CPerfCollector::GetFormattedData(int DataType, PDH_FMT_COUNTERVALUE * pCounterValue, DWORD Format)
	{
		// 저장된 구조체 검색 
		auto iter_find = m_CounterUmap.find(DataType);

		if (iter_find == m_CounterUmap.end())
			return false;

		PerfData *pData = iter_find->second;

		// 배열 데이터라면 GetFormattedDataArray로 얻어야 함 
		if (pData->IsArray == true)
			return false;

		// 포맷 데이터 얻는다.
		PDH_STATUS status;
		status = PdhGetFormattedCounterValue(pData->hCounter, Format, NULL, pCounterValue);

		if (status != ERROR_SUCCESS)
			return false;

		return true;
	}
	
	bool GGM::CPerfCollector::GetFormattedDataArray(int DataType, PDH_FMT_COUNTERVALUE_ITEM **pItems, DWORD * pItemCount, DWORD Format)
	{
		// 저장된 구조체 검색 
		auto iter_find = m_CounterUmap.find(DataType);

		if (iter_find == m_CounterUmap.end())
			return false;

		PerfData *pData = iter_find->second;

		// 배열 데이터가 아니라면 GetFormattedData로 얻어야 함 
		if (pData->IsArray == false)
			return false;

		// 포맷 데이터 얻는다.
		PDH_STATUS status;
		status = PdhGetFormattedCounterArray(pData->hCounter, Format, &pData->BufferSize, &pData->ItemCount, pData->pItems);

		if (status != ERROR_SUCCESS)
			return false;

		*pItems = pData->pItems;
		*pItemCount = pData->ItemCount;

		return true;
	}

}
