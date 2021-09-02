#pragma once
#include <tchar.h>
#include <Windows.h>

namespace GGM
{
	constexpr auto OUTPUT_TYPE = 3; // 로그 아웃풋 모드 경우의 수;
	constexpr auto LOG_LEN = 4096;	// 로그 문자열의 길이;

	enum class OUTMODE
	{
		// 로그 출력 방식을 결정한다.
		CONSOLE,
		FILE,
		BOTH
	};

	enum class LEVEL
	{
		// 기준 로그 레벨에 따라 출력되는 로그의 수준이 달라진다.
		DBG = 0,
		WARN = 1,
		ERR = 2,
		SYS = 3
	};

	class CLogger
	{
	protected:
		// 싱글턴 인스턴스를 가리키는 포인터
		static CLogger* pInstance;
		static SRWLOCK  GetInstLock;

	public:

		// 외부에서 로그 클래스의 단일체 포인터를 얻을 목적으로 사용할 인터페이스 
		static CLogger* GetInstance();

		// 로그를 저장할 폴더를 설정한다.
		bool SetLogDirectory(const TCHAR* dir);

		// 현재 로그레벨을 변경하거나 확인하는 Getter / Setter		
		LEVEL SetDefaultLogLevel(LEVEL LogLevel); // 기준로그레벨을 새로 설정한다. 반환값은 이전 로그레벨
		LEVEL GetDefaultLogLevel() const; // 기준로그레벨을 새로 설정한다. 반환값은 이전 로그레벨

		// 외부에서 로그 저장시 주로 사용하게 될 인터페이스, 인자로 해당 로그의 로그레벨, 출력모드, 로그내용을 전달받는다.
		bool Log(const TCHAR* type, LEVEL LogLevel, OUTMODE OutPutMode, const TCHAR* pArg, ...);
		bool LogHex(const TCHAR* type, LEVEL LogLevel, OUTMODE OutPutMode, const TCHAR* Log, BYTE* pByte, int Len);

	protected:
		// 기준 로그레벨
		LEVEL m_LogLevel;

		// 로그 파일이 저장될 폴더경로
		TCHAR m_LogPath[MAX_PATH];

		// 멀티 스레드 환경에서 파일에 찍을 로그 번호
		ULONGLONG m_LogNum = 0;

		// 멀티 스레드 환경에서 여러 스레드가 동시에 입출력을 하려고 할 수 있으므로 SRWLock으로 락을 걸어준다.
		SRWLOCK m_lock;

		// 로그 인스턴스를 생성할 때 항상 디버그 레벨을 지정하게 강제 하도록 디폴트 생성자를 삭제		
		CLogger() = delete;
		virtual ~CLogger() = default;

		// 로그 클래스는 싱글턴으로 생성자와 소멸자는 외부 접근 불가
		// 생성자를 통해서 디폴트 로그 레벨을 최초로 설정한다. 추후에 확인 및 변경 가능		
		explicit		CLogger(LEVEL DefaultLogLevel);
		static void     Destroy();

		// 로그 출력 
		bool      LogOutput(OUTMODE, const TCHAR*, LEVEL, TCHAR*);	
	};

}

