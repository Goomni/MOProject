#pragma once

namespace GGM
{	
	///////////////////////////////////////////////////////////////////////////////////
	// 덤프 생성 클래스 CCrashDump
	// 개발과정 및 라이브 서비스시에 특정 포인트에서 덤프를 남겨서 문제해결을 위한 근거로 사용
	// 전역적으로 하나만 존재하도록 싱글턴 패턴으로 생성
	///////////////////////////////////////////////////////////////////////////////////
	class CCrashDump
	{
	private:

		static CCrashDump *m_pInstance; // 동적할당을 통한 싱글턴 인스턴스 생성을 위한 포인터		

	public:

		static CCrashDump * GetInstance();

		// 강제로 크래쉬를 일으킨다.
		static void ForceCrash();

		// 처리되지 않은 예외에 대하여 이 함수가 호출된다.
		// 이 함수 내부에서는 덤프를 생성하고 EXCEPTION_EXECUTE_HANDLER를 반환하여 예외가 정상적으로 핸들링되었음을 시스템에 알려주고 프로세스를 종료한다.
		static LONG WINAPI MyUnhandledExceptionFilter(__in PEXCEPTION_POINTERS pExceptionPointer);

		// C 런타임 라이브러리에서 발생하는 예외인 INVALID PARAMETER HANDLER를 처리하기 위한 메서드
		static void MyInvalidParameterHandler(wchar_t const* const expression,
			wchar_t const* const function_name,
			wchar_t const* const file_name,
			unsigned int   const line_number,
			uintptr_t      const reserved);

		// 
		static int MyCRTReportHook(int reportType, char *message, int *returnValue);

		// 최상위 부모 클래스의 소멸자에서 파괴된 자식에 의해 정의된 메서드를 호출할 때 발생하는 에러를 잡기 위한 메서드
		static void myPureCallHandler();

		// 시그널 
		static void signalHandler(int Error);

	private:
		// 싱글턴 클래스이므로 생성자와 소멸자는 Private
		CCrashDump();
		virtual ~CCrashDump() = default;	

	};	

	
}