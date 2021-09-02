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

	// 하나의 함수 수행결과가 저장될 프로파일링 데이터의 단위
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

	// 스레드마다 TLS에 할당될 프로파일링 데이터의 묶음
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
		
		// 스레드별로 할당해줄 PROFILE_DATA 구조체 묶음
		PROFILE_CHUNK   m_pProfileDataArr[NUM_OF_THREADS];

		// 스레드별로 데이터 할당시 참조할 인덱스
		LONG           m_AllocIdx = -1;

		// 스레드별로 데이터가 할당될 TLS INDEX
		DWORD          m_TlsIndex;				

	public:
		static COUNT   llFrequency;

	private:
		stProfileData*      AddData(const TCHAR* szFunc, PROFILE_CHUNK * pChunk); // 배열에 데이터 추가 	 
		void		        WriteResult(); // 프로파일링 파일 쓰기( 소멸자에서 호출된다.) 
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

