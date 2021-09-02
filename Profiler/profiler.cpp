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
		// 프로파일링 시간 측정을 위해 Frequency를 얻는다.
		LARGE_INTEGER freq;
		QueryPerformanceFrequency(&freq);
		llFrequency = freq.QuadPart;

		// 스레드마다 프로파일링 정보를 담을 수 있도록 Tls슬롯을 하나 할당받는다.
		m_TlsIndex = TlsAlloc();		

		// 스레드별로 프로파일링 데이터를 관리할 자료구조를 하나 둔다.		
		for (int i = 0; i < NUM_OF_THREADS; i++)
		{
			m_pProfileDataArr[i].pList = new std::unordered_map<std::wstring, stProfileData*>;
			m_pProfileDataArr[i].pList->reserve(MAX_PROFILE_DATA);
		}
	}

	stProfileData* cProfiler::AddData(const TCHAR* szFunc, PROFILE_CHUNK * pChunk)
	{		
		// 함수 이름을 키로 검색할 수 있도록 해쉬 테이블로 프로파일링 데이터를 관리한다.
		stProfileData *pData = &(pChunk->Data[pChunk->DataIdx++]);
		pData->szFuncName = szFunc;
		pChunk->pList->insert({ wstring(szFunc), pData });
		return pData;
	}	

	void cProfiler::ProfileBegin(const TCHAR * szFunc)
	{
		// 현재 스레드의 TLS에서 프로파일링 데이터 리스트를 얻어옴
		PROFILE_CHUNK *pChunk = (PROFILE_CHUNK*)TlsGetValue(m_TlsIndex);

		if (pChunk == nullptr)
		{
			// 아직 해당 스레드에게 프로파일링 데이터가 할당되어 있지 않다면 할당해줌
			pChunk = &m_pProfileDataArr[InterlockedIncrement(&(m_AllocIdx))];

			// 추후 파일에 쓰기 위해 해당 데이터 리스트와 매핑되는 스레드 아이디 저장 
			pChunk->ThreadID = GetCurrentThreadId();

			// TLS에 저장
			TlsSetValue(m_TlsIndex, (LPVOID)pChunk);
		}

		// 프로파일링 데이터 리스트의 포인터 얻어옴
		std::unordered_map<wstring, stProfileData*> *pProfileList = pChunk->pList;

		// 함수 이름을 키로 사용해 데이터 구조체를 검색			
		auto data_iter = pProfileList->find(wstring(szFunc));
		
		stProfileData *pData = nullptr;
		
		if (data_iter == pProfileList->end())
		{
			// 아직 해당함수가 프로파일링 데이터 목록에 등록되어 있지 않다면 등록
			pData = AddData(szFunc, pChunk);
		}
		else
		{
			// 이전에 해당함수에 대해 Begin이 호출된 적이 있다면 데이터가 이미 등록되어 있음
			pData = data_iter->second;
		}
			
		// ProfileEnd를 호출하지 않고 ProfileBegin에 다시들어왔을때는 그냥 나간다.
		if (pData->IsProfiling == true) 
			return;		
		
		pData->IsProfiling = true;

		// 이번이 첫 ProfileBegin이라면 초기정보 셋팅
		LARGE_INTEGER StartTime;
		QueryPerformanceCounter(&StartTime);
		pData->llStart = StartTime.QuadPart;

		return;
	}

	void cProfiler::ProfileEnd(const TCHAR * szFunc)
	{
		// 함수 호출 시 총 소요된 시간과 최대 최소 구한다.
		LARGE_INTEGER EndTime;
		QueryPerformanceCounter(&EndTime);	

		// 현재 스레드의 TLS에서 프로파일링 데이터 리스트를 얻어옴
		PROFILE_CHUNK *pChunk = (PROFILE_CHUNK*)TlsGetValue(m_TlsIndex);

		// 현재 스레드의 TLS에서 프로파일링 데이터 리스트가 없다면 
		// 아직 프로파일링이 시작되지 않았음
		if (pChunk == nullptr)
			return;

		// 프로파일링 데이터 리스트의 포인터 얻어옴
		std::unordered_map<wstring, stProfileData*> *pProfileList = pChunk->pList;

		// 함수 이름을 키로 사용해 데이터 구조체를 검색				
		auto data_iter = pProfileList->find(wstring(szFunc));

		if (data_iter == pProfileList->end())
		{
			// 아직 해당함수가 데이터 목록에 등록되어 있지 않다면 End 불가
			return;
		}

		stProfileData *pData = data_iter->second;		
	
		COUNT ElapsedCount = EndTime.QuadPart - pData->llStart;
		pData->ldTotal += ElapsedCount;
	
		if (ElapsedCount > pData->MaxCount)
			pData->MaxCount = ElapsedCount;

		if (ElapsedCount < pData->MinCount)
			pData->MinCount = ElapsedCount;

		// End 한번에 호출횟수 ++
		pData->iCalledCount++;

		// 이제 다시 Begin 할 수 있다.
		pData->IsProfiling = false;
	}

	void cProfiler::WriteResult()
	{
		// 아무런 정보도 들어있지 않다면 쓸 필요가 없다.
		if (m_pProfileDataArr->ThreadID == 0)
			return;

		FILE *fp;

		// 시간정보를 써주는 것이 좋겠다.
		SYSTEMTIME now;
		GetLocalTime(&now);

		// 파일 이름 생성
		TCHAR FileName[MAX_PATH];
		HRESULT result = _stprintf_s(FileName, MAX_PATH,
			_T("%4d%02d%02d_Profiling.txt"),			
			now.wYear,
			now.wMonth,
			now.wDay			
		);

		// 프로파일링 내용을 저장할 파일 오픈
		_tfopen_s(&fp, FileName, _T("wt, ccs=UNICODE" ));		

		// 프로파일러를 사용하는 스레드 개수만큼 반복문 돌며 파일에 저장		
		PROFILE_CHUNK *pProfileDataArr = m_pProfileDataArr;

		// 반복문 돌며 각 스레드가 저장한 프로파일링 정보를 한번에 파일에 쓴다.
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

			// 프로파일링 데이터 리스트를 순회하며 파일에 쓴다.
			while (iter_begin != iter_end)
			{	
				stProfileData *pData = iter_begin->second;					

				TIME AvrTime = ((TIME)(pData->ldTotal / (TIME)pData->iCalledCount * 1000000)) / (TIME)cProfiler::llFrequency;
				TIME MinTime = (TIME)(pData->MinCount * 1000000) / (TIME)cProfiler::llFrequency;
				TIME MaxTime = (TIME)(pData->MaxCount * 1000000) / (TIME)cProfiler::llFrequency;
				
				_ftprintf_s(fp, _T("|| %5d || %20s || %20.3Lf㎲ || %20.3Lf㎲ || %20.3Lf㎲ || %20d ||\n")
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
