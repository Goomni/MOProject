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
		// �����ڿ����� ���� ���� �ڵ鷯���� ���� Ŀ���͸���¡�� �ڵ鷯�� �����Ѵ�.
		////////////////////////////////////////////////////////////////////////

		// C ��Ÿ�� ���̺귯�� �Լ��� ȣ���� �� ���ܰ� �߻��ϸ� ������ ���� �� �ֵ��� �ڵ鷯�� �����Ѵ�.
		_set_invalid_parameter_handler(MyInvalidParameterHandler);

		// ����� ����� �� CRT ���� �߻��ϴ� �������� ����.
		_CrtSetReportMode(_CRT_WARN, 0);
		_CrtSetReportMode(_CRT_ASSERT, 0);
		_CrtSetReportMode(_CRT_ERROR, 0);
		_CrtSetReportHook(MyCRTReportHook);

		// ���� ���� �Լ� ���� �ڵ鷯�� �츮������ ������.
		_set_purecall_handler(myPureCallHandler);

		// abort �Լ��� ȣ������ �� �߻��ϴ� ��ɵ��� ��.
		_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);

		// �ü���� ���ͷ�Ʈ �ñ׳��� ������ �� ȣ��� �Լ��� ������
		signal(SIGABRT, signalHandler);
		signal(SIGFPE, signalHandler);
		signal(SIGILL, signalHandler);
		signal(SIGINT, signalHandler);
		signal(SIGSEGV, signalHandler);
		signal(SIGTERM, signalHandler);

		// ���� ��� ��츦 �ڵ鸵�ϴ� �Լ����� ���������� ForceCrash�Լ��� ȣ��
		// ForceCrash �Լ��� ȣ��Ǹ� �Ʒ��� ���� ó�� �Լ��� ȣ���
		// �� �Լ��� ������ �����.
		SetUnhandledExceptionFilter(MyUnhandledExceptionFilter);

	}

	CCrashDump* CCrashDump::GetInstance()
	{
		// ��Ƽ������ ȯ�濡�� �̱��� �ν��Ͻ��� �������� ���� ���¶�� ���� �����尡 �ѹ��� new�� �ϰԵ� ������ �ִ�.
		// ���� �ν��Ͻ��� �������� ���� ��Ȳ������ ���ʷ� pInstance == nullptr�� Ȯ���� �����常 �ν��Ͻ��� �����ϰ� �Ѵ�.
		// ���� �����尡 �����Ǳ� ���� ���� �����忡�� GetInstance()�� �ѹ� ȣ�����ָ� ������ ������, �׷��� Ȥ�� �𸣴� ������ġ�� �д�.		

		static LONG IsCreated = FALSE;

		if (m_pInstance == nullptr)
		{
			// ���� �����尡 ���ÿ� pInstance�� ���¸� nullptr�� Ȯ���ϰ� �� ������ ���Դٸ�
			// ������ ��ü�� �����Ϸ� ������ Ȯ��
			if (InterlockedExchange(&IsCreated, TRUE) == TRUE)
			{
				// �������鼭 �ν��Ͻ��� �����ɶ����� ��ٸ�
				while (m_pInstance == nullptr)
				{
					Sleep(0);
				}

				// ������ ��ü�� ����.
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
		// ������ ������ �� ������ ��¥�� �ð��� �����̸��� ǥ���Ѵ�.
		SYSTEMTIME now;
		GetLocalTime(&now);

		// ���� ���� �̸� 
		TCHAR DumpFileName[MAX_PATH];

		_stprintf_s(DumpFileName, _T("DUMP_%d_%02d_%02d_%02d%02d%02d_THREAD%d.dmp"),
			now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, GetCurrentThreadId());

		// ���� ����
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

		// ���� ����

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

		// �ֻ��� �θ� Ŭ������ �Ҹ��ڿ��� �ı��� �ڽĿ� ���� ���ǵ� �޼��带 ȣ���� �� �߻��ϴ� ������ ��� ���� �޼���
	void CCrashDump::myPureCallHandler()
	{
		ForceCrash();
	}

	// �ñ׳� 
	void CCrashDump::signalHandler(int Error)
	{
		ForceCrash();
	}
}

