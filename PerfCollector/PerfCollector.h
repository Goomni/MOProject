#pragma once

#include <Windows.h>
#include <pdh.h>
#include <unordered_map>
#include <tchar.h>
#pragma comment(lib, "pdh.lib")

///////////////////////////////////////////////////////////////////////////////////////
//To collect performance data using the PDH functions, perform the following steps.
//1. Create a query
//2. Add counters to the query
//3. Collect the performance data
//4. Display the performance data
//5. Close the query
///////////////////////////////////////////////////////////////////////////////////////

namespace GGM
{
	////////////////////////////////////////////////////////////////////
	// PDH를 활용하여 퍼포먼스와 관련된 각종 카운터 정보를 얻어오는 클래스
	////////////////////////////////////////////////////////////////////
	constexpr auto POOL_NON_PAGED_BYTES   = _T("\\Memory\\Pool Nonpaged Bytes");
	constexpr auto AVAILABLE_MBYTES		  = _T("\\Memory\\Available MBytes");
	constexpr auto NETWORK_BYTES_SENT     = _T("\\Network Interface(*)\\Bytes Sent/sec");
	constexpr auto NETWORK_BYTES_RECEIVED = _T("\\Network Interface(*)\\Bytes Received/sec");	
	constexpr auto PROCESS_PRIVATE_BYTES  = _T("\\Process(%s)\\Private Bytes");	

	struct PerfData
	{
		// 일반적으로 사용하는 자료들
		HCOUNTER                   hCounter = NULL;	
		int                        DataType;		

		// 한번의 쿼리로 복수의 인스턴스에 대한 정보를 얻을때만[(*)을 사용할때] 사용한다.
		bool                       IsArray = false;		
		PDH_FMT_COUNTERVALUE_ITEM *pItems = nullptr;	
		DWORD                      BufferSize = 0;
		DWORD                      ItemCount = 0;
	};

	class CPerfCollector
	{
	public:

		         CPerfCollector();
		virtual ~CPerfCollector();

			// 정보를 얻고싶은 카운터를 추가 
		    bool AddCounter(const WCHAR* CounterPath, int DataType, DWORD Format, bool IsArray = false);

			// 추가한 카운터에 대해 쿼리 날리기 
			bool CollectQueryData();

			// 쿼리 결과 데이터 얻기 
			bool GetFormattedData(int DataType, PDH_FMT_COUNTERVALUE *pCounterValue, DWORD Format);

			// 쿼리 결과 데이터 얻기 (복수의 데이터)
			bool GetFormattedDataArray(int DataType, PDH_FMT_COUNTERVALUE_ITEM **pItems, DWORD *pItemCount, DWORD Format);

	protected:		

		// 쿼리 핸들
		PDH_HQUERY m_hQuery = NULL;

		// 복수의 카운터를 담을 UMAP
		std::unordered_map<int, PerfData*> m_CounterUmap;

		// 데이터를 수집할 프로세스의 이름
		TCHAR m_ProcessName[MAX_PATH];

	};
}

