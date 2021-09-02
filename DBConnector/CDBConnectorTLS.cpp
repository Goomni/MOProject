#include "CDBConnectorTLS.h"
#include "LockFreeStack\LockFreeStack.h"
#include <strsafe.h>
#include "CrashDump\CrashDump.h"

namespace GGM
{
	GGM::CDBConnectorTLS::CDBConnectorTLS(const char * IP, const char * Username, const char * password, const char * DB, USHORT port)
	{
		mysql_library_init(0, nullptr, nullptr);

		// TlsIndex 확보
		m_TlsIndex = TlsAlloc();

		if (m_TlsIndex == TLS_OUT_OF_INDEXES)
		{
			CLogger::GetInstance()->Log(_T("DB_LOG"), LEVEL::SYS, OUTMODE::FILE, _T("TLS_OUT_OF_INDEXES [m_TlsIndex = TlsAlloc()]"));
			CCrashDump::ForceCrash();
		}

		// 연결정보 저장 
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

		// Tls에 할당해준 CDBConnector*를 저장할 스택
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

		// 할당해준 DBConnector 들을 할당해제해줌
		// DBConnector의 소멸자에서 연결 세션 정리를 하기 때문에 꼭 이 과정이 필요함
		while (m_pConnectorStack->size() > 0)
		{
			CDBConnector* pDBConnector;

			m_pConnectorStack->Pop(&pDBConnector);

			delete pDBConnector;
		}

		// 락프리 스택 정리
		delete m_pConnectorStack;
	}

	CDBConnector* GGM::CDBConnectorTLS::GetTlsDBConnector() 
	{
		// 이 함수를 호출한 스레드의 TLS에 할당된 DBConnector을 하나 꺼내준다.
		CDBConnector *pDBConnector = (CDBConnector*)TlsGetValue(m_TlsIndex);

		// 아직 할당된 것이 없다면 할당해서 반환
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

			// 나중에 정리하기 위해 보관해둠
			m_pConnectorStack->Push(pDBConnector);
		}

		return pDBConnector;
	}

}
