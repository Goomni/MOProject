#include <Windows.h>
#include <signal.h>
#include <stdio.h>
#include <crtdbg.h>
#include <DbgHelp.h>
#include <tchar.h>
#include "CrashDump\CrashDump.h"
#pragma comment(lib, "Dbghelp.lib")

namespace GGM
{
	CCrashDump *CCrashDump::m_pInstance = nullptr;

	CCrashDump::CCrashDump()
	{

		////////////////////////////////////////////////////////////////////////
		// 생성자에서는 각종 예외 핸들러들을 내가 커스터마이징한 핸들러로 변경한다.
		////////////////////////////////////////////////////////////////////////

		// C 런타임 라이브러리 함수를 호출할 때 예외가 발생하면 덤프를 남길 수 있도록 핸들러를 셋팅한다.
		_set_invalid_parameter_handler(MyInvalidParameterHandler);

		// 디버그 모드일 때 CRT 에서 발생하는 오류보고를 끈다.
		_CrtSetReportMode(_CRT_WARN, 0);
		_CrtSetReportMode(_CRT_ASSERT, 0);
		_CrtSetReportMode(_CRT_ERROR, 0);
		_CrtSetReportHook(MyCRTReportHook);

		// 순수 가상 함수 에러 핸들러를 우리쪽으로 돌린다.
		_set_purecall_handler(myPureCallHandler);

		// abort 함수를 호출했을 때 발생하는 기능들을 끔.
		_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);

		// 운영체제가 인터럽트 시그날을 보냈을 때 호출될 함수를 지정함
		signal(SIGABRT, signalHandler);
		signal(SIGFPE, signalHandler);
		signal(SIGILL, signalHandler);
		signal(SIGINT, signalHandler);
		signal(SIGSEGV, signalHandler);
		signal(SIGTERM, signalHandler);

		// 위의 모든 경우를 핸들링하는 함수들은 내부적으로 ForceCrash함수를 호출
		// ForceCrash 함수가 호출되면 아래의 예외 처리 함수가 호출됨
		// 이 함수는 덤프를 남긴다.
		SetUnhandledExceptionFilter(MyUnhandledExceptionFilter);

	}

	CCrashDump* CCrashDump::GetInstance()
	{
		// 멀티스레드 환경에서 싱글턴 인스턴스가 생성되지 않은 상태라면 여러 스레드가 한번에 new를 하게될 위험이 있다.
		// 따라서 인스턴스가 생성되지 않은 상황에서는 최초로 pInstance == nullptr을 확인한 스레드만 인스턴스를 생성하게 한다.
		// 여러 스레드가 생성되기 전에 메인 스레드에서 GetInstance()를 한번 호출해주면 문제가 없지만, 그래도 혹시 모르니 안전장치를 둔다.		

		static LONG IsCreated = FALSE;

		if (m_pInstance == nullptr)
		{
			// 여러 스레드가 동시에 pInstance의 상태를 nullptr로 확인하고 이 안으로 들어왔다면
			// 누군가 객체를 생성하러 들어갔는지 확인
			if (InterlockedExchange(&IsCreated, TRUE) == TRUE)
			{
				// 루프돌면서 인스턴스가 생성될때까지 기다림
				while (m_pInstance == nullptr)
				{
					Sleep(0);
				}

				// 생성된 객체를 얻어간다.
				return m_pInstance;
			}

			m_pInstance = new CCrashDump();

			if (m_pInstance != nullptr)
				return m_pInstance;
			else
				return nullptr;
		}

		return m_pInstance;
	}

	void CCrashDump::ForceCrash()
	{
		int *CrashNull = nullptr;
		*CrashNull = 0;
	}

	LONG WINAPI CCrashDump::MyUnhandledExceptionFilter(__in PEXCEPTION_POINTERS pExceptionPointer)
	{
		// 덤프를 저장할 때 저장한 날짜와 시간을 파일이름에 표시한다.
		SYSTEMTIME now;
		GetLocalTime(&now);

		// 덤프 파일 이름 
		TCHAR DumpFileName[MAX_PATH];

		_stprintf_s(DumpFileName, _T("DUMP_%d_%02d_%02d_%02d%02d%02d_THREAD%d.dmp"),
			now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, GetCurrentThreadId());

		// 파일 오픈
		HANDLE hFile = CreateFile(
			DumpFileName,
			GENERIC_WRITE,
			FILE_SHARE_WRITE,
			nullptr,
			CREATE_ALWAYS,
			FILE_ATTRIBUTE_NORMAL,
			NULL
		);

		if (hFile == INVALID_HANDLE_VALUE)
		{
			_tprintf_s(_T("CreateFile Failed For Dump [ ERROR : %d ]\n"), GetLastError());
			return EXCEPTION_EXECUTE_HANDLER;
		}

		_tprintf_s(_T("DUMP FILE OPEN NOW SAVING....\n"));

		// 파일 생성

		_MINIDUMP_EXCEPTION_INFORMATION dumpInfo;

		dumpInfo.ThreadId = GetCurrentThreadId();
		dumpInfo.ExceptionPointers = pExceptionPointer;
		dumpInfo.ClientPointers = true;

		bool fOK = MiniDumpWriteDump(
			GetCurrentProcess(),
			GetCurrentProcessId(),
			hFile,
			MiniDumpWithFullMemory,
			&dumpInfo,
			NULL,
			NULL
		);


		if (!fOK)
		{
			_tprintf_s(_T("MiniDumpWriteDump Failed For Dump [ ERROR : %d ]\n"), GetLastError());
			return EXCEPTION_EXECUTE_HANDLER;
		}

		CloseHandle(hFile);

		_tprintf_s(_T("DUMP FILE SAVED COMPLETION\n"));

		return EXCEPTION_EXECUTE_HANDLER;
	}

	void CCrashDump::MyInvalidParameterHandler(wchar_t const* const expression,
		wchar_t const* const function_name,
		wchar_t const* const file_name,
		unsigned int   const line_number,
		uintptr_t      const reserved)
	{
		ForceCrash();
	}		

	int CCrashDump::MyCRTReportHook(int reportType, char *message, int *returnValue)
	{
		ForceCrash();
		return true;
	}	

		// 최상위 부모 클래스의 소멸자에서 파괴된 자식에 의해 정의된 메서드를 호출할 때 발생하는 에러를 잡기 위한 메서드
	void CCrashDump::myPureCallHandler()
	{
		ForceCrash();
	}

	// 시그널 
	void CCrashDump::signalHandler(int Error)
	{
		ForceCrash();
	}
}

