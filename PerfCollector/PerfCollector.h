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
	// PDH�� Ȱ���Ͽ� �����ս��� ���õ� ���� ī���� ������ ������ Ŭ����
	////////////////////////////////////////////////////////////////////
	constexpr auto POOL_NON_PAGED_BYTES   = _T("\\Memory\\Pool Nonpaged Bytes");
	constexpr auto AVAILABLE_MBYTES		  = _T("\\Memory\\Available MBytes");
	constexpr auto NETWORK_BYTES_SENT     = _T("\\Network Interface(*)\\Bytes Sent/sec");
	constexpr auto NETWORK_BYTES_RECEIVED = _T("\\Network Interface(*)\\Bytes Received/sec");	
	constexpr auto PROCESS_PRIVATE_BYTES  = _T("\\Process(%s)\\Private Bytes");	

	struct PerfData
	{
		// �Ϲ������� ����ϴ� �ڷ��
		HCOUNTER                   hCounter = NULL;	
		int                        DataType;		

		// �ѹ��� ������ ������ �ν��Ͻ��� ���� ������ ��������[(*)�� ����Ҷ�] ����Ѵ�.
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

			// ������ ������ ī���͸� �߰� 
		    bool AddCounter(const WCHAR* CounterPath, int DataType, DWORD Format, bool IsArray = false);

			// �߰��� ī���Ϳ� ���� ���� ������ 
			bool CollectQueryData();

			// ���� ��� ������ ��� 
			bool GetFormattedData(int DataType, PDH_FMT_COUNTERVALUE *pCounterValue, DWORD Format);

			// ���� ��� ������ ��� (������ ������)
			bool GetFormattedDataArray(int DataType, PDH_FMT_COUNTERVALUE_ITEM **pItems, DWORD *pItemCount, DWORD Format);

	protected:		

		// ���� �ڵ�
		PDH_HQUERY m_hQuery = NULL;

		// ������ ī���͸� ���� UMAP
		std::unordered_map<int, PerfData*> m_CounterUmap;

		// �����͸� ������ ���μ����� �̸�
		TCHAR m_ProcessName[MAX_PATH];

	};
}

