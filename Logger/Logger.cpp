#include "Logger.h"
#include <stdarg.h>
#include <strsafe.h>
#include <time.h>
#include <cstdio>

using namespace GGM;

namespace GGM
{
	CLogger* GGM::CLogger::pInstance = nullptr;	

	CLogger::CLogger(LEVEL DefaultLogLevel) : m_LogLevel(DefaultLogLevel)
	{
		// 기본 로그 파일 저장 경로는 현재 디렉토리
		// 서버 초기화시 로그 경로는 항상 설정할 것이지만 혹시 까먹고 설정 하지 않을 경우 대비
		int Written = GetCurrentDirectory(MAX_PATH, m_LogPath);

		if (Written)
			m_LogPath[Written] = 0;

		// 멀티 스레드 환경에서 파일 출력의 동기화를 위한 SRWLock 초기화
		InitializeSRWLock(&m_lock);
	}

	void CLogger::Destroy()
	{
		// 힙영역에 생성된 싱글턴 인스턴스를 프로세스 종료시 할당해제
		delete pInstance;
	}

	CLogger * CLogger::GetInstance()
	{
		// 멀티스레드 환경에서 싱글턴 인스턴스가 생성되지 않은 상태라면 여러 스레드가 한번에 new를 하게될 위험이 있다.
		// 따라서 인스턴스가 생성되지 않은 상황에서는 최초로 pInstance == nullptr을 확인한 스레드만 인스턴스를 생성하게 한다.
		// 여러 스레드가 생성되기 전에 메인 스레드에서 GetInstance()를 한번 호출해주면 문제가 없지만, 그래도 혹시 모르니 안전장치를 둔다.		
		static LONG IsCreated = FALSE;

		if (pInstance == nullptr)
		{
			// 여러 스레드가 동시에 pInstance의 상태를 nullptr로 확인하고 이 안으로 들어왔다면
			// 누군가 객체를 생성하러 들어갔는지 확인
			if (InterlockedExchange(&IsCreated, TRUE) == TRUE)
			{
				// 루프돌면서 인스턴스가 생성될때까지 기다림
				while (pInstance == nullptr)
				{
					Sleep(0);
				}

				// 생성된 객체를 얻어간다.
				return pInstance;
			}

			pInstance = new CLogger(LEVEL::DBG);

			if (pInstance != nullptr)
			{	// 프로세스 종료시 이 클래스의 소멸자가 호출될 수 있도록 파괴함수 등록
				atexit(Destroy);
				return pInstance;
			}
			else
				return nullptr;
		}

		return pInstance;
	}

	bool CLogger::SetLogDirectory(const TCHAR * dir)
	{
		size_t dirLen = _tcslen(dir);

		if (dirLen > MAX_PATH)
			return false;

		// 로그를 저장할 폴더를 생성한다.	
		CreateDirectory(dir, nullptr);

		// 멤버에 폴더 이름을 복사한다.
		_tcscpy_s(m_LogPath, MAX_PATH, dir);

		m_LogPath[dirLen] = 0;

		return true;
	}

	LEVEL CLogger::SetDefaultLogLevel(LEVEL LogLevel)
	{
		// 디폴트 로그레벨을 바꿔준다.
		LEVEL RetLogLeve = m_LogLevel;

		m_LogLevel = LogLevel;

		// 이전 레벨을 반환해준다.
		return RetLogLeve;
	}

	LEVEL CLogger::GetDefaultLogLevel() const
	{
		// 외부에서 현재 디폴트 로그레벨 얻을 때 사용
		return m_LogLevel;
	}

	bool CLogger::Log(const TCHAR* type, LEVEL LogLevel, OUTMODE Mode, const TCHAR* pArg, ...)
	{
		// 기준로그 이상인 로그만 처리한다.
		if (m_LogLevel <= LogLevel)
		{
			// 가변인자의 시작포인터 설정
			va_list pVaList;
			va_start(pVaList, pArg);

			// 로그 내용 담을 버퍼 			
			TCHAR szLog[LOG_LEN];

			// 포맷 문자열 받은것을 로그 버퍼에 담는다.		
			StringCchVPrintf(szLog, LOG_LEN, pArg, pVaList);

			// 인자로 전달된 출력 모드에 따라 로그 출력 함수 호출한다.					
			if (!LogOutput(Mode, type, LogLevel, szLog))
				return false;

			//가변인자 포인터 사용 정리
			va_end(pVaList);
		}

		return true;
	}

	bool CLogger::LogHex(const TCHAR * type, LEVEL LogLevel, OUTMODE Mode, const TCHAR * Log, BYTE * pByte, int Len)
	{
		// 기준로그 이상인 로그만 처리한다.
		if (m_LogLevel <= LogLevel)
		{
			// 로그 내용 담을 버퍼 			
			TCHAR szLog[LOG_LEN];

			// Byte 배열에 포함된 내용을 HEX로 바꾼다.
			// 포맷문자열 형식으로 바로 16진수로 바꾸어주는 라이브러리 함수들의 오버헤드를 피하기 위해
			// 바이트배열에 담긴 내용을 직접 16진수형태의 문자열로 바꾼다.
			TCHAR hex_str[] = _T("0123456789ABCDEF"); // 16진수 토큰 테이블
			TCHAR hex_out[2048];
			TCHAR *buf = hex_out;

			// 반복문 돌면서 바이트 배열에 포함된 1바이트씩 16진수로 변경해서 버퍼에 담음
			for (int i = 0; i < Len - 1; i++)
			{
				*buf++ = hex_str[(*pByte) >> 4 & 0xf];
				*buf++ = hex_str[(*pByte++) & 0xf];
				*buf++ = _T(':');
			}

			*buf++ = hex_str[(*pByte) >> 4 & 0xf];
			*buf++ = hex_str[(*pByte) & 0xf];
			*buf = 0;

			// 16진수 변환 문자열과 로그 내용을 연결하여 버퍼에 담는다.		
			StringCchPrintf(szLog, LOG_LEN, _T("%s HEX[%s]"), Log, hex_out);

			// 출력 모드에 따라 로그 출력
			if (!LogOutput(Mode, type, LogLevel, szLog))
				return false;

		}

		return true;
	}

	bool CLogger::LogOutput(OUTMODE mode, const TCHAR* type, LEVEL level, TCHAR* LogMsg)
	{
		// 로컬 시간 정보 획득
		SYSTEMTIME now;
		GetLocalTime(&now);

		// 로그 레벨 스트링
		const TCHAR *LogLevel[4] = { _T("DBG"), _T("WARN"), _T("ERR"), _T("SYS") };

		// 멀티 스레드 환경에서 로그의 순서를 가늠하기 위해 로그 찍은 순서를 남긴다. 
		// 100% 정확하지 않지만 로그가 남을 당시, 대강의 로직 흐름을 파악하기 위한 용도 
		ULONGLONG LogNum = InterlockedIncrement(&m_LogNum);

		// 파일 이름 생성
		TCHAR FileName[MAX_PATH];
		HRESULT result = StringCchPrintf(FileName, MAX_PATH,
			_T("%s/%4d%02d%02d_%s.txt"),
			m_LogPath,
			now.wYear,
			now.wMonth,
			now.wDay,
			type
		);

		// 버퍼 길이에 비해 파일 경로가 너무 길다면 그냥 현재 디렉터리에 로그 파일 생성한다.
		if (result == STRSAFE_E_INSUFFICIENT_BUFFER)
		{
			StringCchPrintf(FileName, MAX_PATH,
				_T("%4d%02d%02d_%s.txt"),
				now.wYear,
				now.wMonth,
				now.wDay,
				type
			);
		}

		// 로그 타입, 기록 시간 정보, 실제 로그내용을 버퍼에 담는다.	
		TCHAR szLog[LOG_LEN];
		result = StringCchPrintf(szLog, LOG_LEN,
			_T("[%s][%4d-%02d-%02d %02d:%02d:%02d][%s][%08d] %s\n\n"),
			type,
			now.wYear,
			now.wMonth,
			now.wDay,
			now.wHour,
			now.wMinute,
			now.wSecond,
			LogLevel[(int)level],
			LogNum,
			LogMsg
		);

		// 콘솔 출력해야 한다면 출력 
		if (mode == OUTMODE::BOTH)
			_tprintf_s(szLog);

		// 버퍼 길이에 비해 로그가 너무 길다면 해당 사항을 로그로 남긴다.
		// 이 내용을 로그로 남기는 이유는 로그의 길이가 너무 길다는 것을 알려주어 조정하기 위함이다.
		if (result == STRSAFE_E_INSUFFICIENT_BUFFER)
		{
			// 너무 길다고 판단되는 로그의 내용을 함께 넣어 TOO LONG 로그를 파일에 남긴다.
			LogMsg[MAX_PATH] = 0;
			LogOutput(OUTMODE::BOTH, _T("TOO LONG"), level, LogMsg);
			return false;
		}

		// 멀티스레드 환경에서 여러 스레드가 하나의 파일에 동시에 쓸 가능성이 있기 때문에 락 걸어준다.
		AcquireSRWLockExclusive(&m_lock);

		// 파일 포인터
		FILE *pFile = nullptr;

		_tfopen_s(&pFile, FileName, _T("rt+, ccs=UNICODE"));

		// 파일이 생성이 되어 있지 않다면 생성한다.
		if (pFile == nullptr)
		{
			_tfopen_s(&pFile, FileName, _T("wt, ccs=UNICODE"));

			if (pFile == nullptr)
				return false;
		}

		// 파일의 끝에 로그를 이어 붙인다.
		fseek(pFile, 0, SEEK_END);

		// 로그파일에 로그를 쓴다.
		fwrite(szLog, 1, _tcslen(szLog) * sizeof(TCHAR), pFile);

		// 파일 출력 작업이 끝났으므로 파일을 닫아준다.
		fclose(pFile);

		ReleaseSRWLockExclusive(&m_lock);

		return true;
	}

}