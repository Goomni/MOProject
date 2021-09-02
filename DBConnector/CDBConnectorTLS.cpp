#include "CDBConnectorTLS.h"
#include "LockFreeStack\LockFreeStack.h"
#include <strsafe.h>
#include "CrashDump\CrashDump.h"

namespace GGM
{
	GGM::CDBConnectorTLS::CDBConnectorTLS(const char * IP, const char * Username, const char * password, const char * DB, USHORT port)
	{
		mysql_library_init(0, nullptr, nullptr);

		// TlsIndex Ȯ��
		m_TlsIndex = TlsAlloc();

		if (m_TlsIndex == TLS_OUT_OF_INDEXES)
		{
			CLogger::GetInstance()->Log(_T("DB_LOG"), LEVEL::SYS, OUTMODE::FILE, _T("TLS_OUT_OF_INDEXES [m_TlsIndex = TlsAlloc()]"));
			CCrashDump::ForceCrash();
		}

		// �������� ���� 
		HRESULT result;

		result = StringCchCopyA(m_IP, sizeof(m_IP), IP);

		if (FAILED(result))
		{
			CLogger::GetInstance()->Log(_T("DB_LOG"), LEVEL::SYS, OUTMODE::FILE, _T("StringCchCopyA failed [GetLastError : %d]"), GetLastError());
			CCrashDump::ForceCrash();
		}

		result = StringCchCopyA(m_Username, sizeof(m_Username), Username);

		if (FAILED(result))
		{
			CLogger::GetInstance()->Log(_T("DB_LOG"), LEVEL::SYS, OUTMODE::FILE, _T("StringCchCopyA failed [GetLastError : %d]"), GetLastError());
			CCrashDump::ForceCrash();
		}

		result = StringCchCopyA(m_Password, sizeof(m_Password), password);

		if (FAILED(result))
		{
			CLogger::GetInstance()->Log(_T("DB_LOG"), LEVEL::SYS, OUTMODE::FILE, _T("StringCchCopyA failed [GetLastError : %d]"), GetLastError());
			CCrashDump::ForceCrash();
		}

		result = StringCchCopyA(m_DBname, sizeof(m_DBname), DB);

		if (FAILED(result))
		{
			CLogger::GetInstance()->Log(_T("DB_LOG"), LEVEL::SYS, OUTMODE::FILE, _T("StringCchCopyA failed [GetLastError : %d]"), GetLastError());
			CCrashDump::ForceCrash();
		}

		m_DBPort = port;

		// Tls�� �Ҵ����� CDBConnector*�� ������ ����
		m_pConnectorStack = new CLockFreeStack<CDBConnector*>(0);

		if (m_pConnectorStack == nullptr)
		{
			CLogger::GetInstance()->Log(_T("DB_LOG"), LEVEL::SYS, OUTMODE::FILE, _T("MEM ALLOC FAILED [GetLastError : %d]"), GetLastError());
			CCrashDump::ForceCrash();
		}
	}

	GGM::CDBConnectorTLS::~CDBConnectorTLS()
	{
		TlsFree(m_TlsIndex);

		// �Ҵ����� DBConnector ���� �Ҵ���������
		// DBConnector�� �Ҹ��ڿ��� ���� ���� ������ �ϱ� ������ �� �� ������ �ʿ���
		while (m_pConnectorStack->size() > 0)
		{
			CDBConnector* pDBConnector;

			m_pConnectorStack->Pop(&pDBConnector);

			delete pDBConnector;
		}

		// ������ ���� ����
		delete m_pConnectorStack;
	}

	CDBConnector* GGM::CDBConnectorTLS::GetTlsDBConnector() 
	{
		// �� �Լ��� ȣ���� �������� TLS�� �Ҵ�� DBConnector�� �ϳ� �����ش�.
		CDBConnector *pDBConnector = (CDBConnector*)TlsGetValue(m_TlsIndex);

		// ���� �Ҵ�� ���� ���ٸ� �Ҵ��ؼ� ��ȯ
		if (pDBConnector == nullptr)
		{
			if (GetLastError() != ERROR_SUCCESS)
			{
				CLogger::GetInstance()->Log(_T("DB_LOG"), LEVEL::SYS, OUTMODE::FILE, _T("TlsGetValue FAILED [GetLastError : %d]"), GetLastError());
				CCrashDump::ForceCrash();
			}

			pDBConnector = new CDBConnector(m_IP, m_Username, m_Password, m_DBname, m_DBPort);

			if (pDBConnector == nullptr)
			{
				CLogger::GetInstance()->Log(_T("DB_LOG"), LEVEL::SYS, OUTMODE::FILE, _T("MEM ALLOC FAILED [GetLastError : %d]"), GetLastError());
				CCrashDump::ForceCrash();
			}

			if (TlsSetValue(m_TlsIndex, (LPVOID)pDBConnector) == 0)
			{
				CLogger::GetInstance()->Log(_T("DB_LOG"), LEVEL::SYS, OUTMODE::FILE, _T("TlsSetValue FAILED [GetLastError : %d]"), GetLastError());
				CCrashDump::ForceCrash();
			}

			// ���߿� �����ϱ� ���� �����ص�
			m_pConnectorStack->Push(pDBConnector);
		}

		return pDBConnector;
	}

}
