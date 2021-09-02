#pragma once
#include <unordered_map>
#include <string>
#include <tchar.h>

constexpr auto NUM_OF_THREADS = 128;
constexpr auto MAX_PROFILE_DATA = 256;
constexpr auto MIN_INIT = 0x7fffffffffffffff;

namespace GGM
{
	typedef long long   COUNT;
	typedef long double TIME;	

	// �ϳ��� �Լ� �������� ����� �������ϸ� �������� ����
	struct stProfileData
	{
		const   TCHAR*	szFuncName;
		int		iCalledCount = 0;
		bool	IsProfiling = false;
		COUNT	llStart = 0;
		COUNT	ldTotal = 0;
		COUNT   MaxCount = 0;
		COUNT	MinCount = MIN_INIT;		
	};

	// �����帶�� TLS�� �Ҵ�� �������ϸ� �������� ����
	struct PROFILE_CHUNK
	{
		std::unordered_map<std::wstring, stProfileData*> *pList;		
		stProfileData Data[MAX_PROFILE_DATA];
		DWORD DataIdx = 0;
		DWORD ThreadID = 0;
	};

	class cProfiler
	{
	private:		
		
		// �����庰�� �Ҵ����� PROFILE_DATA ����ü ����
		PROFILE_CHUNK   m_pProfileDataArr[NUM_OF_THREADS];

		// �����庰�� ������ �Ҵ�� ������ �ε���
		LONG           m_AllocIdx = -1;

		// �����庰�� �����Ͱ� �Ҵ�� TLS INDEX
		DWORD          m_TlsIndex;				

	public:
		static COUNT   llFrequency;

	private:
		stProfileData*      AddData(const TCHAR* szFunc, PROFILE_CHUNK * pChunk); // �迭�� ������ �߰� 	 
		void		        WriteResult(); // �������ϸ� ���� ����( �Ҹ��ڿ��� ȣ��ȴ�.) 
	public:

	                  	    cProfiler();
		virtual             ~cProfiler();			
		void		        ProfileBegin(const TCHAR * szFunc);
		void		        ProfileEnd(const TCHAR * szFunc);	
		
	};

	extern cProfiler profiler;

#ifdef PROFILE_ENABLE
#define BEGIN(X) profiler.ProfileBegin(X)
#define END(X) profiler.ProfileEnd(X)
#else
#define BEGIN(X) 
#define END(X)
#endif

}

