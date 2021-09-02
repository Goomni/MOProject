#include "CDBConnector.h"
#include "CrashDump\CrashDump.h"
#include <strsafe.h>

GGM::CDBConnector::CDBConnector(const char * IP, const char * Username, const char * password, const char * DB, USHORT port)
{
	// MYSQL ����ü �ʱ�ȭ
	m_pSql = mysql_init(&m_Sql);

	// DB ����ü �ʱ�ȭ�� �����ϸ� �� �̻� ������ ������ �� ����.
	if (m_pSql == nullptr)
	{		
		CLogger::GetInstance()->Log(_T("DB_LOG"), LEVEL::SYS, OUTMODE::FILE, _T("mysql_init failed"));
		CCrashDump::ForceCrash();	
	}	

	// ���� �翬���� �õ��� �� ����ϱ� ���� ���� ������ ����� �����صд�.
	HRESULT result;

	result = StringCchCopyA(m_IP, sizeof(m_IP), IP);

	if(FAILED(result))
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

	// DB ����
	DBConnect();	
}

GGM::CDBConnector::~CDBConnector()
{
	DBDisconnect();
}

void GGM::CDBConnector::DBConnect()
{
	// DB ����
	if (mysql_real_connect(&m_Sql, m_IP, m_Username, m_Password, m_DBname, m_DBPort, nullptr, 0) == nullptr)
	{
		int DBErrno = mysql_errno(&m_Sql);

		// DB ������ �������� ��, �Ʒ��� ���� ������ �߻��Ѵٸ� ����Ƚ�� (5ȸ) �翬���� �õ��غ��� �ȵǸ� ���� ũ��������.
		if (DBErrno == CR_SOCKET_CREATE_ERROR
			|| DBErrno == CR_CONNECTION_ERROR
			|| DBErrno == CR_CONN_HOST_ERROR
			|| DBErrno == CR_SERVER_GONE_ERROR
			|| DBErrno == CR_SERVER_HANDSHAKE_ERR
			|| DBErrno == CR_SERVER_LOST
			|| DBErrno == CR_INVALID_CONN_HANDLE
			)
		{
			// �翬�� 5ȸ �õ� 
			for (int i = 0; i < 5; i++)
			{
				m_pSql = mysql_real_connect(&m_Sql, m_IP, m_Username, m_Password, m_DBname, m_DBPort, nullptr, 0);

				// ���� �����ϸ� �ݺ��� Ż��
				if (m_pSql != nullptr)
					break;
			}		
		}
		else
		{
			// ������ȣ�� ���ڿ� �� �α׷� �����.				
			const char *DBError = mysql_error(&m_Sql);
			TCHAR UTF16_DBError[256];

			// UTF8 ���� ���ڿ��� UTF16���� ��ȯ�Ͽ� �α׷� �����.
			MultiByteToWideChar(CP_UTF8, MB_PRECOMPOSED, DBError, -1, UTF16_DBError, 256);

			CLogger::GetInstance()->Log(_T("DB DB_LOG"), LEVEL::SYS, OUTMODE::FILE, _T("%s [Error Code] %d"), UTF16_DBError, mysql_errno(&m_Sql));

			// DB ������ ���� �ʴ´ٸ� ������ �� �̻� ������ �� ����.
			CCrashDump::ForceCrash();
		}

		// 5ȸ �翬��õ� �Ŀ��� ������ �� �� ���ٸ� �α׳���� ���� ũ����
		if (m_pSql == nullptr)
		{
			// ������ȣ�� ���ڿ� �� �α׷� �����.				
			const char *DBError = mysql_error(&m_Sql);
			TCHAR UTF16_DBError[256];

			// UTF8 ���� ���ڿ��� UTF16���� ��ȯ�Ͽ� �α׷� �����.
			MultiByteToWideChar(CP_UTF8, MB_PRECOMPOSED, DBError, -1, UTF16_DBError, 256);

			CLogger::GetInstance()->Log(_T("DB DB_LOG"), LEVEL::SYS, OUTMODE::FILE, _T("%s [Error Code] %d"), UTF16_DBError, mysql_errno(&m_Sql));

			// DB ������ ���� �ʴ´ٸ� ������ �� �̻� ������ �� ����.
			CCrashDump::ForceCrash();
		}
	}		
}
void GGM::CDBConnector::DBDisconnect()
{
	// ���� �������� �����ؾ� �� ������� �ִٸ� �����Ѵ�.
	if (m_pResult != nullptr)
		mysql_free_result(m_pResult);

	// DB���� �������� �� ���ҽ� ����
	mysql_close(&m_Sql);	
}

bool GGM::CDBConnector::SendQuery(bool ResultKeep, const char * StringFormat, ...)
{
	// ���� ���ڿ� ���� ����
	// UTF - 8
	char  QueryString[MAX_QUERY_STRING_LEN]; 

	// UTF - 16
	TCHAR QueryString_UTF16[MAX_QUERY_STRING_LEN];

	// �α�� ���� ���ڿ� ����
	TCHAR UTF16_DBError[256];

	// ���� ���ڷ� ���� ���� ���ڿ��� �۽��� �� �ֵ��� ����
	va_list pValist;
	va_start(pValist, StringFormat);
	HRESULT result = StringCchVPrintfA(QueryString, MAX_QUERY_STRING_LEN, StringFormat, pValist);

	// ��û�� �������ڿ��� �ʹ� �� �߷ȴٸ� ���� ���δ�.
	// �߸� ������ ���� ���� ����.
	// �߸��� ��� ��û�� �ִٸ� ������ ������ �� ����.
	if (FAILED(result))
	{
		// ���� ������ �� Ȯ���� �� �ֵ��� ���� UTF 16���ε� ��ȯ�صα� 
		MultiByteToWideChar(CP_UTF8, MB_PRECOMPOSED, QueryString, -1, QueryString_UTF16, MAX_QUERY_STRING_LEN);
		CCrashDump::ForceCrash();
	}

	// ���� �۽�
	while(mysql_query(&m_Sql, QueryString))
	{
		// ������ ���ȴµ� �Ʒ� ���� �ڵ�� ���� ���� ������ ���� ���̶�� ����Ƚ�� �翬���� �õ��غ��� �ȵǸ� ũ����
		int DBErrno = mysql_errno(&m_Sql);
		
		if (DBErrno == CR_SOCKET_CREATE_ERROR
			|| DBErrno == CR_CONNECTION_ERROR
			|| DBErrno == CR_CONN_HOST_ERROR
			|| DBErrno == CR_SERVER_GONE_ERROR
			|| DBErrno == CR_SERVER_HANDSHAKE_ERR
			|| DBErrno == CR_SERVER_LOST
			|| DBErrno == CR_INVALID_CONN_HANDLE
			)
		{
			// �翬�� 5ȸ �õ� 
			for (int i = 0; i < 5; i++)
			{
				m_pSql = mysql_real_connect(&m_Sql, m_IP, m_Username, m_Password, m_DBname, m_DBPort, nullptr, 0);

				// ���� �����ϸ� �ݺ��� Ż��
				if (m_pSql != nullptr)
					break;
			}

			// �翬�� �����ߴٸ� �ٽ� ���� �۽Žõ�
			if (m_pSql != nullptr)
				continue;
		}

		// ���� �۽� �����ϸ� ���´� ������ ��� �α׷� �����.
		// ��Ȯ�� Ȯ���� ���� �۽��� ������ UTF 16���� ��ȯ�� ���´�.	
		MultiByteToWideChar(CP_UTF8, MB_PRECOMPOSED, QueryString, -1, QueryString_UTF16, MAX_QUERY_STRING_LEN);

		// ������ȣ�� ���ڿ� �� �α׷� �����.				
		const char *DBError = mysql_error(&m_Sql);		

		// UTF8 ���ڿ��� UTF16���� ��ȯ�Ͽ� �α׷� �����.
		MultiByteToWideChar(CP_UTF8, MB_PRECOMPOSED, DBError, -1, UTF16_DBError, 256);

		// �������ڿ�, �����ڵ�, ������ �� ������ �α׿� �����.
		CLogger::GetInstance()->Log(_T("DB_LOG"), LEVEL::SYS, OUTMODE::FILE, _T("%s [Error Code] %d"), UTF16_DBError, DBErrno);
		CLogger::GetInstance()->Log(_T("DB_LOG"), LEVEL::SYS, OUTMODE::FILE, _T("[Error Query] %s"), QueryString_UTF16);

		// ���� �״´�.
		CCrashDump::ForceCrash();
	}

	// ������� �����ؾ��� ������� ����� �� �����Ѵ�.
	if (ResultKeep == true)
	{
		// ������ ���� ����� ���
		if (m_pResult != nullptr)
			mysql_free_result(m_pResult);

		m_pResult = mysql_store_result(&m_Sql);

		// ������� �������� �ߴµ� nullptr�̴�.
		if (m_pResult == nullptr)
		{
			// ������ ������ ������ �ƴϸ� return�� �����Ͱ� ������ ���� ������ Ȯ���Ѵ�.
			int FieldCount = mysql_field_count(&m_Sql);

			// m_pResult == nullptr �ε� FieldCount�� 0���� ũ�ٸ� �� ������ ��¥ ������ �� ���̴�.
			if(FieldCount > 0)
			{
				// ���� �۽� �����ϸ� ���´� ������ ��� �α׷� �����.
				// ��Ȯ�� Ȯ���� ���� �۽��� ������ UTF 16���� ��ȯ�� ���´�.	
				MultiByteToWideChar(CP_UTF8, MB_PRECOMPOSED, QueryString, -1, QueryString_UTF16, MAX_QUERY_STRING_LEN);

				// ������ȣ�� ���ڿ� �� �α׷� �����.				
				const char *DBError = mysql_error(&m_Sql);			

				// UTF8 ���ڿ��� UTF16���� ��ȯ�Ͽ� �α׷� �����.
				MultiByteToWideChar(CP_UTF8, MB_PRECOMPOSED, DBError, -1, UTF16_DBError, 256);

				// �������ڿ�, �����ڵ�, ������ �� ������ �α׿� �����.
				CLogger::GetInstance()->Log(_T("DB_LOG"), LEVEL::SYS, OUTMODE::FILE, _T("%s [Error Code] %d"), UTF16_DBError, mysql_errno(&m_Sql));
				CLogger::GetInstance()->Log(_T("DB_LOG"), LEVEL::SYS, OUTMODE::FILE, _T("[Error Query] %s"), QueryString_UTF16);

				// ���� �״´�.
				CCrashDump::ForceCrash();
			}
		}		
	}

	return true;
}

MYSQL_ROW GGM::CDBConnector::FetchRow()
{
	// ���� ������� ������ �ȵ�
	if (m_pResult == nullptr)
		return nullptr;	

	return mysql_fetch_row(m_pResult);
}

MYSQL_RES * GGM::CDBConnector::GetResult()
{
	return m_pResult;
}

void GGM::CDBConnector::FreeResult()
{
	// ����� �� ��������� �����Ѵ�.
	if(m_pResult != nullptr)
	{
		mysql_free_result(m_pResult);
		m_pResult = nullptr;
	}

}


