#pragma once

namespace GGM
{	
	///////////////////////////////////////////////////////////////////////////////////
	// ���� ���� Ŭ���� CCrashDump
	// ���߰��� �� ���̺� ���񽺽ÿ� Ư�� ����Ʈ���� ������ ���ܼ� �����ذ��� ���� �ٰŷ� ���
	// ���������� �ϳ��� �����ϵ��� �̱��� �������� ����
	///////////////////////////////////////////////////////////////////////////////////
	class CCrashDump
	{
	private:

		static CCrashDump *m_pInstance; // �����Ҵ��� ���� �̱��� �ν��Ͻ� ������ ���� ������		

	public:

		static CCrashDump * GetInstance();

		// ������ ũ������ ����Ų��.
		static void ForceCrash();

		// ó������ ���� ���ܿ� ���Ͽ� �� �Լ��� ȣ��ȴ�.
		// �� �Լ� ���ο����� ������ �����ϰ� EXCEPTION_EXECUTE_HANDLER�� ��ȯ�Ͽ� ���ܰ� ���������� �ڵ鸵�Ǿ����� �ý��ۿ� �˷��ְ� ���μ����� �����Ѵ�.
		static LONG WINAPI MyUnhandledExceptionFilter(__in PEXCEPTION_POINTERS pExceptionPointer);

		// C ��Ÿ�� ���̺귯������ �߻��ϴ� ������ INVALID PARAMETER HANDLER�� ó���ϱ� ���� �޼���
		static void MyInvalidParameterHandler(wchar_t const* const expression,
			wchar_t const* const function_name,
			wchar_t const* const file_name,
			unsigned int   const line_number,
			uintptr_t      const reserved);

		// 
		static int MyCRTReportHook(int reportType, char *message, int *returnValue);

		// �ֻ��� �θ� Ŭ������ �Ҹ��ڿ��� �ı��� �ڽĿ� ���� ���ǵ� �޼��带 ȣ���� �� �߻��ϴ� ������ ��� ���� �޼���
		static void myPureCallHandler();

		// �ñ׳� 
		static void signalHandler(int Error);

	private:
		// �̱��� Ŭ�����̹Ƿ� �����ڿ� �Ҹ��ڴ� Private
		CCrashDump();
		virtual ~CCrashDump() = default;	

	};	

	
}