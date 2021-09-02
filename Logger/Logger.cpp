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
		// �⺻ �α� ���� ���� ��δ� ���� ���丮
		// ���� �ʱ�ȭ�� �α� ��δ� �׻� ������ �������� Ȥ�� ��԰� ���� ���� ���� ��� ���
		int Written = GetCurrentDirectory(MAX_PATH, m_LogPath);

		if (Written)
			m_LogPath[Written] = 0;

		// ��Ƽ ������ ȯ�濡�� ���� ����� ����ȭ�� ���� SRWLock �ʱ�ȭ
		InitializeSRWLock(&m_lock);
	}

	void CLogger::Destroy()
	{
		// �������� ������ �̱��� �ν��Ͻ��� ���μ��� ����� �Ҵ�����
		delete pInstance;
	}

	CLogger * CLogger::GetInstance()
	{
		// ��Ƽ������ ȯ�濡�� �̱��� �ν��Ͻ��� �������� ���� ���¶�� ���� �����尡 �ѹ��� new�� �ϰԵ� ������ �ִ�.
		// ���� �ν��Ͻ��� �������� ���� ��Ȳ������ ���ʷ� pInstance == nullptr�� Ȯ���� �����常 �ν��Ͻ��� �����ϰ� �Ѵ�.
		// ���� �����尡 �����Ǳ� ���� ���� �����忡�� GetInstance()�� �ѹ� ȣ�����ָ� ������ ������, �׷��� Ȥ�� �𸣴� ������ġ�� �д�.		
		static LONG IsCreated = FALSE;

		if (pInstance == nullptr)
		{
			// ���� �����尡 ���ÿ� pInstance�� ���¸� nullptr�� Ȯ���ϰ� �� ������ ���Դٸ�
			// ������ ��ü�� �����Ϸ� ������ Ȯ��
			if (InterlockedExchange(&IsCreated, TRUE) == TRUE)
			{
				// �������鼭 �ν��Ͻ��� �����ɶ����� ��ٸ�
				while (pInstance == nullptr)
				{
					Sleep(0);
				}

				// ������ ��ü�� ����.
				return pInstance;
			}

			pInstance = new CLogger(LEVEL::DBG);

			if (pInstance != nullptr)
			{	// ���μ��� ����� �� Ŭ������ �Ҹ��ڰ� ȣ��� �� �ֵ��� �ı��Լ� ���
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

		// �α׸� ������ ������ �����Ѵ�.	
		CreateDirectory(dir, nullptr);

		// ����� ���� �̸��� �����Ѵ�.
		_tcscpy_s(m_LogPath, MAX_PATH, dir);

		m_LogPath[dirLen] = 0;

		return true;
	}

	LEVEL CLogger::SetDefaultLogLevel(LEVEL LogLevel)
	{
		// ����Ʈ �α׷����� �ٲ��ش�.
		LEVEL RetLogLeve = m_LogLevel;

		m_LogLevel = LogLevel;

		// ���� ������ ��ȯ���ش�.
		return RetLogLeve;
	}

	LEVEL CLogger::GetDefaultLogLevel() const
	{
		// �ܺο��� ���� ����Ʈ �α׷��� ���� �� ���
		return m_LogLevel;
	}

	bool CLogger::Log(const TCHAR* type, LEVEL LogLevel, OUTMODE Mode, const TCHAR* pArg, ...)
	{
		// ���طα� �̻��� �α׸� ó���Ѵ�.
		if (m_LogLevel <= LogLevel)
		{
			// ���������� ���������� ����
			va_list pVaList;
			va_start(pVaList, pArg);

			// �α� ���� ���� ���� 			
			TCHAR szLog[LOG_LEN];

			// ���� ���ڿ� �������� �α� ���ۿ� ��´�.		
			StringCchVPrintf(szLog, LOG_LEN, pArg, pVaList);

			// ���ڷ� ���޵� ��� ��忡 ���� �α� ��� �Լ� ȣ���Ѵ�.					
			if (!LogOutput(Mode, type, LogLevel, szLog))
				return false;

			//�������� ������ ��� ����
			va_end(pVaList);
		}

		return true;
	}

	bool CLogger::LogHex(const TCHAR * type, LEVEL LogLevel, OUTMODE Mode, const TCHAR * Log, BYTE * pByte, int Len)
	{
		// ���طα� �̻��� �α׸� ó���Ѵ�.
		if (m_LogLevel <= LogLevel)
		{
			// �α� ���� ���� ���� 			
			TCHAR szLog[LOG_LEN];

			// Byte �迭�� ���Ե� ������ HEX�� �ٲ۴�.
			// ���˹��ڿ� �������� �ٷ� 16������ �ٲپ��ִ� ���̺귯�� �Լ����� ������带 ���ϱ� ����
			// ����Ʈ�迭�� ��� ������ ���� 16���������� ���ڿ��� �ٲ۴�.
			TCHAR hex_str[] = _T("0123456789ABCDEF"); // 16���� ��ū ���̺�
			TCHAR hex_out[2048];
			TCHAR *buf = hex_out;

			// �ݺ��� ���鼭 ����Ʈ �迭�� ���Ե� 1����Ʈ�� 16������ �����ؼ� ���ۿ� ����
			for (int i = 0; i < Len - 1; i++)
			{
				*buf++ = hex_str[(*pByte) >> 4 & 0xf];
				*buf++ = hex_str[(*pByte++) & 0xf];
				*buf++ = _T(':');
			}

			*buf++ = hex_str[(*pByte) >> 4 & 0xf];
			*buf++ = hex_str[(*pByte) & 0xf];
			*buf = 0;

			// 16���� ��ȯ ���ڿ��� �α� ������ �����Ͽ� ���ۿ� ��´�.		
			StringCchPrintf(szLog, LOG_LEN, _T("%s HEX[%s]"), Log, hex_out);

			// ��� ��忡 ���� �α� ���
			if (!LogOutput(Mode, type, LogLevel, szLog))
				return false;

		}

		return true;
	}

	bool CLogger::LogOutput(OUTMODE mode, const TCHAR* type, LEVEL level, TCHAR* LogMsg)
	{
		// ���� �ð� ���� ȹ��
		SYSTEMTIME now;
		GetLocalTime(&now);

		// �α� ���� ��Ʈ��
		const TCHAR *LogLevel[4] = { _T("DBG"), _T("WARN"), _T("ERR"), _T("SYS") };

		// ��Ƽ ������ ȯ�濡�� �α��� ������ �����ϱ� ���� �α� ���� ������ �����. 
		// 100% ��Ȯ���� ������ �αװ� ���� ���, �밭�� ���� �帧�� �ľ��ϱ� ���� �뵵 
		ULONGLONG LogNum = InterlockedIncrement(&m_LogNum);

		// ���� �̸� ����
		TCHAR FileName[MAX_PATH];
		HRESULT result = StringCchPrintf(FileName, MAX_PATH,
			_T("%s/%4d%02d%02d_%s.txt"),
			m_LogPath,
			now.wYear,
			now.wMonth,
			now.wDay,
			type
		);

		// ���� ���̿� ���� ���� ��ΰ� �ʹ� ��ٸ� �׳� ���� ���͸��� �α� ���� �����Ѵ�.
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

		// �α� Ÿ��, ��� �ð� ����, ���� �α׳����� ���ۿ� ��´�.	
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

		// �ܼ� ����ؾ� �Ѵٸ� ��� 
		if (mode == OUTMODE::BOTH)
			_tprintf_s(szLog);

		// ���� ���̿� ���� �αװ� �ʹ� ��ٸ� �ش� ������ �α׷� �����.
		// �� ������ �α׷� ����� ������ �α��� ���̰� �ʹ� ��ٴ� ���� �˷��־� �����ϱ� �����̴�.
		if (result == STRSAFE_E_INSUFFICIENT_BUFFER)
		{
			// �ʹ� ��ٰ� �ǴܵǴ� �α��� ������ �Բ� �־� TOO LONG �α׸� ���Ͽ� �����.
			LogMsg[MAX_PATH] = 0;
			LogOutput(OUTMODE::BOTH, _T("TOO LONG"), level, LogMsg);
			return false;
		}

		// ��Ƽ������ ȯ�濡�� ���� �����尡 �ϳ��� ���Ͽ� ���ÿ� �� ���ɼ��� �ֱ� ������ �� �ɾ��ش�.
		AcquireSRWLockExclusive(&m_lock);

		// ���� ������
		FILE *pFile = nullptr;

		_tfopen_s(&pFile, FileName, _T("rt+, ccs=UNICODE"));

		// ������ ������ �Ǿ� ���� �ʴٸ� �����Ѵ�.
		if (pFile == nullptr)
		{
			_tfopen_s(&pFile, FileName, _T("wt, ccs=UNICODE"));

			if (pFile == nullptr)
				return false;
		}

		// ������ ���� �α׸� �̾� ���δ�.
		fseek(pFile, 0, SEEK_END);

		// �α����Ͽ� �α׸� ����.
		fwrite(szLog, 1, _tcslen(szLog) * sizeof(TCHAR), pFile);

		// ���� ��� �۾��� �������Ƿ� ������ �ݾ��ش�.
		fclose(pFile);

		ReleaseSRWLockExclusive(&m_lock);

		return true;
	}

}