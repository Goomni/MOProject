#pragma once
#include <tchar.h>
#include <Windows.h>

namespace GGM
{
	constexpr auto OUTPUT_TYPE = 3; // �α� �ƿ�ǲ ��� ����� ��;
	constexpr auto LOG_LEN = 4096;	// �α� ���ڿ��� ����;

	enum class OUTMODE
	{
		// �α� ��� ����� �����Ѵ�.
		CONSOLE,
		FILE,
		BOTH
	};

	enum class LEVEL
	{
		// ���� �α� ������ ���� ��µǴ� �α��� ������ �޶�����.
		DBG = 0,
		WARN = 1,
		ERR = 2,
		SYS = 3
	};

	class CLogger
	{
	protected:
		// �̱��� �ν��Ͻ��� ����Ű�� ������
		static CLogger* pInstance;
		static SRWLOCK  GetInstLock;

	public:

		// �ܺο��� �α� Ŭ������ ����ü �����͸� ���� �������� ����� �������̽� 
		static CLogger* GetInstance();

		// �α׸� ������ ������ �����Ѵ�.
		bool SetLogDirectory(const TCHAR* dir);

		// ���� �α׷����� �����ϰų� Ȯ���ϴ� Getter / Setter		
		LEVEL SetDefaultLogLevel(LEVEL LogLevel); // ���طα׷����� ���� �����Ѵ�. ��ȯ���� ���� �α׷���
		LEVEL GetDefaultLogLevel() const; // ���طα׷����� ���� �����Ѵ�. ��ȯ���� ���� �α׷���

		// �ܺο��� �α� ����� �ַ� ����ϰ� �� �������̽�, ���ڷ� �ش� �α��� �α׷���, ��¸��, �α׳����� ���޹޴´�.
		bool Log(const TCHAR* type, LEVEL LogLevel, OUTMODE OutPutMode, const TCHAR* pArg, ...);
		bool LogHex(const TCHAR* type, LEVEL LogLevel, OUTMODE OutPutMode, const TCHAR* Log, BYTE* pByte, int Len);

	protected:
		// ���� �α׷���
		LEVEL m_LogLevel;

		// �α� ������ ����� �������
		TCHAR m_LogPath[MAX_PATH];

		// ��Ƽ ������ ȯ�濡�� ���Ͽ� ���� �α� ��ȣ
		ULONGLONG m_LogNum = 0;

		// ��Ƽ ������ ȯ�濡�� ���� �����尡 ���ÿ� ������� �Ϸ��� �� �� �����Ƿ� SRWLock���� ���� �ɾ��ش�.
		SRWLOCK m_lock;

		// �α� �ν��Ͻ��� ������ �� �׻� ����� ������ �����ϰ� ���� �ϵ��� ����Ʈ �����ڸ� ����		
		CLogger() = delete;
		virtual ~CLogger() = default;

		// �α� Ŭ������ �̱������� �����ڿ� �Ҹ��ڴ� �ܺ� ���� �Ұ�
		// �����ڸ� ���ؼ� ����Ʈ �α� ������ ���ʷ� �����Ѵ�. ���Ŀ� Ȯ�� �� ���� ����		
		explicit		CLogger(LEVEL DefaultLogLevel);
		static void     Destroy();

		// �α� ��� 
		bool      LogOutput(OUTMODE, const TCHAR*, LEVEL, TCHAR*);	
	};

}

