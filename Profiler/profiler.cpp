#include <Windows.h>
#include <cstring>
#include <cstdio>
#include <tchar.h>
#include "profiler.h"

using namespace std;

namespace GGM
{
	cProfiler profiler;

	COUNT cProfiler::llFrequency = 0;

	cProfiler::cProfiler()
	{
		// �������ϸ� �ð� ������ ���� Frequency�� ��´�.
		LARGE_INTEGER freq;
		QueryPerformanceFrequency(&freq);
		llFrequency = freq.QuadPart;

		// �����帶�� �������ϸ� ������ ���� �� �ֵ��� Tls������ �ϳ� �Ҵ�޴´�.
		m_TlsIndex = TlsAlloc();		

		// �����庰�� �������ϸ� �����͸� ������ �ڷᱸ���� �ϳ� �д�.		
		for (int i = 0; i < NUM_OF_THREADS; i++)
		{
			m_pProfileDataArr[i].pList = new std::unordered_map<std::wstring, stProfileData*>;
			m_pProfileDataArr[i].pList->reserve(MAX_PROFILE_DATA);
		}
	}

	stProfileData* cProfiler::AddData(const TCHAR* szFunc, PROFILE_CHUNK * pChunk)
	{		
		// �Լ� �̸��� Ű�� �˻��� �� �ֵ��� �ؽ� ���̺�� �������ϸ� �����͸� �����Ѵ�.
		stProfileData *pData = &(pChunk->Data[pChunk->DataIdx++]);
		pData->szFuncName = szFunc;
		pChunk->pList->insert({ wstring(szFunc), pData });
		return pData;
	}	

	void cProfiler::ProfileBegin(const TCHAR * szFunc)
	{
		// ���� �������� TLS���� �������ϸ� ������ ����Ʈ�� ����
		PROFILE_CHUNK *pChunk = (PROFILE_CHUNK*)TlsGetValue(m_TlsIndex);

		if (pChunk == nullptr)
		{
			// ���� �ش� �����忡�� �������ϸ� �����Ͱ� �Ҵ�Ǿ� ���� �ʴٸ� �Ҵ�����
			pChunk = &m_pProfileDataArr[InterlockedIncrement(&(m_AllocIdx))];

			// ���� ���Ͽ� ���� ���� �ش� ������ ����Ʈ�� ���εǴ� ������ ���̵� ���� 
			pChunk->ThreadID = GetCurrentThreadId();

			// TLS�� ����
			TlsSetValue(m_TlsIndex, (LPVOID)pChunk);
		}

		// �������ϸ� ������ ����Ʈ�� ������ ����
		std::unordered_map<wstring, stProfileData*> *pProfileList = pChunk->pList;

		// �Լ� �̸��� Ű�� ����� ������ ����ü�� �˻�			
		auto data_iter = pProfileList->find(wstring(szFunc));
		
		stProfileData *pData = nullptr;
		
		if (data_iter == pProfileList->end())
		{
			// ���� �ش��Լ��� �������ϸ� ������ ��Ͽ� ��ϵǾ� ���� �ʴٸ� ���
			pData = AddData(szFunc, pChunk);
		}
		else
		{
			// ������ �ش��Լ��� ���� Begin�� ȣ��� ���� �ִٸ� �����Ͱ� �̹� ��ϵǾ� ����
			pData = data_iter->second;
		}
			
		// ProfileEnd�� ȣ������ �ʰ� ProfileBegin�� �ٽõ��������� �׳� ������.
		if (pData->IsProfiling == true) 
			return;		
		
		pData->IsProfiling = true;

		// �̹��� ù ProfileBegin�̶�� �ʱ����� ����
		LARGE_INTEGER StartTime;
		QueryPerformanceCounter(&StartTime);
		pData->llStart = StartTime.QuadPart;

		return;
	}

	void cProfiler::ProfileEnd(const TCHAR * szFunc)
	{
		// �Լ� ȣ�� �� �� �ҿ�� �ð��� �ִ� �ּ� ���Ѵ�.
		LARGE_INTEGER EndTime;
		QueryPerformanceCounter(&EndTime);	

		// ���� �������� TLS���� �������ϸ� ������ ����Ʈ�� ����
		PROFILE_CHUNK *pChunk = (PROFILE_CHUNK*)TlsGetValue(m_TlsIndex);

		// ���� �������� TLS���� �������ϸ� ������ ����Ʈ�� ���ٸ� 
		// ���� �������ϸ��� ���۵��� �ʾ���
		if (pChunk == nullptr)
			return;

		// �������ϸ� ������ ����Ʈ�� ������ ����
		std::unordered_map<wstring, stProfileData*> *pProfileList = pChunk->pList;

		// �Լ� �̸��� Ű�� ����� ������ ����ü�� �˻�				
		auto data_iter = pProfileList->find(wstring(szFunc));

		if (data_iter == pProfileList->end())
		{
			// ���� �ش��Լ��� ������ ��Ͽ� ��ϵǾ� ���� �ʴٸ� End �Ұ�
			return;
		}

		stProfileData *pData = data_iter->second;		
	
		COUNT ElapsedCount = EndTime.QuadPart - pData->llStart;
		pData->ldTotal += ElapsedCount;
	
		if (ElapsedCount > pData->MaxCount)
			pData->MaxCount = ElapsedCount;

		if (ElapsedCount < pData->MinCount)
			pData->MinCount = ElapsedCount;

		// End �ѹ��� ȣ��Ƚ�� ++
		pData->iCalledCount++;

		// ���� �ٽ� Begin �� �� �ִ�.
		pData->IsProfiling = false;
	}

	void cProfiler::WriteResult()
	{
		// �ƹ��� ������ ������� �ʴٸ� �� �ʿ䰡 ����.
		if (m_pProfileDataArr->ThreadID == 0)
			return;

		FILE *fp;

		// �ð������� ���ִ� ���� ���ڴ�.
		SYSTEMTIME now;
		GetLocalTime(&now);

		// ���� �̸� ����
		TCHAR FileName[MAX_PATH];
		HRESULT result = _stprintf_s(FileName, MAX_PATH,
			_T("%4d%02d%02d_Profiling.txt"),			
			now.wYear,
			now.wMonth,
			now.wDay			
		);

		// �������ϸ� ������ ������ ���� ����
		_tfopen_s(&fp, FileName, _T("wt, ccs=UNICODE" ));		

		// �������Ϸ��� ����ϴ� ������ ������ŭ �ݺ��� ���� ���Ͽ� ����		
		PROFILE_CHUNK *pProfileDataArr = m_pProfileDataArr;

		// �ݺ��� ���� �� �����尡 ������ �������ϸ� ������ �ѹ��� ���Ͽ� ����.
		for(DWORD i=0;i< NUM_OF_THREADS;i++)
		{		
			std::unordered_map<wstring, stProfileData*> *pList = pProfileDataArr[i].pList;
			DWORD ThreadID = pProfileDataArr[i].ThreadID;

			auto iter_begin = pList->begin();
			auto iter_end = pList->end();

			if (pList->size() > 0)
			{
				_ftprintf_s(fp,
					_T("|| %5s || %20s || %22s || %22s || %22s || %20s ||\n")
					, _T("ID")
					, _T("NAME")
					, _T("AVERAGE")
					, _T("MIN")
					, _T("MAX")
					, _T("CALL")
				);

				_ftprintf_s(fp, _T("========================================================================================================================================\n"));
			}

			// �������ϸ� ������ ����Ʈ�� ��ȸ�ϸ� ���Ͽ� ����.
			while (iter_begin != iter_end)
			{	
				stProfileData *pData = iter_begin->second;					

				TIME AvrTime = ((TIME)(pData->ldTotal / (TIME)pData->iCalledCount * 1000000)) / (TIME)cProfiler::llFrequency;
				TIME MinTime = (TIME)(pData->MinCount * 1000000) / (TIME)cProfiler::llFrequency;
				TIME MaxTime = (TIME)(pData->MaxCount * 1000000) / (TIME)cProfiler::llFrequency;
				
				_ftprintf_s(fp, _T("|| %5d || %20s || %20.3Lf�� || %20.3Lf�� || %20.3Lf�� || %20d ||\n")
					, ThreadID
					, pData->szFuncName
					, AvrTime
					, MinTime
					, MaxTime
					, pData->iCalledCount
				);						

				++iter_begin;
			}			

			_ftprintf_s(fp, _T("\n\n"));

			delete pList;					
		}


		fclose(fp);
	}

	cProfiler::~cProfiler()
	{
		WriteResult();		
	}	

}
