#include "CDBConnector.h"
#include "CrashDump\CrashDump.h"
#include <strsafe.h>

GGM::CDBConnector::CDBConnector(const char * IP, const char * Username, const char * password, const char * DB, USHORT port)
{
	// MYSQL 구조체 초기화
	m_pSql = mysql_init(&m_Sql);

	// DB 구조체 초기화가 실패하면 더 이상 서버를 진행할 수 없다.
	if (m_pSql == nullptr)
	{		
		CLogger::GetInstance()->Log(_T("DB_LOG"), LEVEL::SYS, OUTMODE::FILE, _T("mysql_init failed"));
		CCrashDump::ForceCrash();	
	}	

	// 추후 재연결을 시도할 때 사용하기 위해 연결 정보를 멤버에 복사해둔다.
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

	// DB 연결
	DBConnect();	
}

GGM::CDBConnector::~CDBConnector()
{
	DBDisconnect();
}

void GGM::CDBConnector::DBConnect()
{
	// DB 연결
	if (mysql_real_connect(&m_Sql, m_IP, m_Username, m_Password, m_DBname, m_DBPort, nullptr, 0) == nullptr)
	{
		int DBErrno = mysql_errno(&m_Sql);

		// DB 연결이 실패했을 때, 아래와 같은 오류가 발생한다면 일정횟수 (5회) 재연결을 시도해보고 안되면 서버 크래쉬낸다.
		if (DBErrno == CR_SOCKET_CREATE_ERROR
			|| DBErrno == CR_CONNECTION_ERROR
			|| DBErrno == CR_CONN_HOST_ERROR
			|| DBErrno == CR_SERVER_GONE_ERROR
			|| DBErrno == CR_SERVER_HANDSHAKE_ERR
			|| DBErrno == CR_SERVER_LOST
			|| DBErrno == CR_INVALID_CONN_HANDLE
			)
		{
			// 재연결 5회 시도 
			for (int i = 0; i < 5; i++)
			{
				m_pSql = mysql_real_connect(&m_Sql, m_IP, m_Username, m_Password, m_DBname, m_DBPort, nullptr, 0);

				// 연결 성공하면 반복문 탈출
				if (m_pSql != nullptr)
					break;
			}		
		}
		else
		{
			// 에러번호와 문자열 얻어서 로그로 남긴다.				
			const char *DBError = mysql_error(&m_Sql);
			TCHAR UTF16_DBError[256];

			// UTF8 에러 문자열을 UTF16으로 변환하여 로그로 남긴다.
			MultiByteToWideChar(CP_UTF8, MB_PRECOMPOSED, DBError, -1, UTF16_DBError, 256);

			CLogger::GetInstance()->Log(_T("DB DB_LOG"), LEVEL::SYS, OUTMODE::FILE, _T("%s [Error Code] %d"), UTF16_DBError, mysql_errno(&m_Sql));

			// DB 연결이 되지 않는다면 서버를 더 이상 진행할 수 없다.
			CCrashDump::ForceCrash();
		}

		// 5회 재연결시도 후에도 연결을 할 수 없다면 로그남기고 서버 크래쉬
		if (m_pSql == nullptr)
		{
			// 에러번호와 문자열 얻어서 로그로 남긴다.				
			const char *DBError = mysql_error(&m_Sql);
			TCHAR UTF16_DBError[256];

			// UTF8 에러 문자열을 UTF16으로 변환하여 로그로 남긴다.
			MultiByteToWideChar(CP_UTF8, MB_PRECOMPOSED, DBError, -1, UTF16_DBError, 256);

			CLogger::GetInstance()->Log(_T("DB DB_LOG"), LEVEL::SYS, OUTMODE::FILE, _T("%s [Error Code] %d"), UTF16_DBError, mysql_errno(&m_Sql));

			// DB 연결이 되지 않는다면 서버를 더 이상 진행할 수 없다.
			CCrashDump::ForceCrash();
		}
	}		
}
void GGM::CDBConnector::DBDisconnect()
{
	// 연결 끊기전에 정리해야 할 결과셋이 있다면 정리한다.
	if (m_pResult != nullptr)
		mysql_free_result(m_pResult);

	// DB와의 연결종료 및 리소스 정리
	mysql_close(&m_Sql);	
}

bool GGM::CDBConnector::SendQuery(bool ResultKeep, const char * StringFormat, ...)
{
	// 쿼리 문자열 담을 버퍼
	// UTF - 8
	char  QueryString[MAX_QUERY_STRING_LEN]; 

	// UTF - 16
	TCHAR QueryString_UTF16[MAX_QUERY_STRING_LEN];

	// 로깅시 에러 문자열 버퍼
	TCHAR UTF16_DBError[256];

	// 가변 인자로 받은 쿼리 문자열을 송신할 수 있도록 복사
	va_list pValist;
	va_start(pValist, StringFormat);
	HRESULT result = StringCchVPrintfA(QueryString, MAX_QUERY_STRING_LEN, StringFormat, pValist);

	// 요청한 쿼리문자열이 너무 길어서 잘렸다면 서버 죽인다.
	// 잘린 쿼리를 보낼 수는 없다.
	// 잘못된 디비 요청이 있다면 서버를 진행할 수 없다.
	if (FAILED(result))
	{
		// 덤프 남았을 때 확인할 수 있도록 쿼리 UTF 16으로도 변환해두기 
		MultiByteToWideChar(CP_UTF8, MB_PRECOMPOSED, QueryString, -1, QueryString_UTF16, MAX_QUERY_STRING_LEN);
		CCrashDump::ForceCrash();
	}

	// 쿼리 송신
	while(mysql_query(&m_Sql, QueryString))
	{
		// 쿼리를 날렸는데 아래 오류 코드와 같은 연결 문제로 인한 것이라면 일정횟수 재연결을 시도해보고 안되면 크래쉬
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
			// 재연결 5회 시도 
			for (int i = 0; i < 5; i++)
			{
				m_pSql = mysql_real_connect(&m_Sql, m_IP, m_Username, m_Password, m_DBname, m_DBPort, nullptr, 0);

				// 연결 성공하면 반복문 탈출
				if (m_pSql != nullptr)
					break;
			}

			// 재연결 성공했다면 다시 쿼리 송신시도
			if (m_pSql != nullptr)
				continue;
		}

		// 쿼리 송신 실패하면 보냈던 쿼리를 모두 로그로 남긴다.
		// 정확한 확인을 위해 송신한 쿼리를 UTF 16으로 변환해 놓는다.	
		MultiByteToWideChar(CP_UTF8, MB_PRECOMPOSED, QueryString, -1, QueryString_UTF16, MAX_QUERY_STRING_LEN);

		// 에러번호와 문자열 얻어서 로그로 남긴다.				
		const char *DBError = mysql_error(&m_Sql);		

		// UTF8 문자열을 UTF16으로 변환하여 로그로 남긴다.
		MultiByteToWideChar(CP_UTF8, MB_PRECOMPOSED, DBError, -1, UTF16_DBError, 256);

		// 에러문자열, 에러코드, 문제가 된 쿼리를 로그에 남긴다.
		CLogger::GetInstance()->Log(_T("DB_LOG"), LEVEL::SYS, OUTMODE::FILE, _T("%s [Error Code] %d"), UTF16_DBError, DBErrno);
		CLogger::GetInstance()->Log(_T("DB_LOG"), LEVEL::SYS, OUTMODE::FILE, _T("[Error Query] %s"), QueryString_UTF16);

		// 서버 죽는다.
		CCrashDump::ForceCrash();
	}

	// 결과셋을 저장해야할 쿼리라면 결과셋 얻어서 저장한다.
	if (ResultKeep == true)
	{
		// 쿼리에 대한 결과셋 얻기
		if (m_pResult != nullptr)
			mysql_free_result(m_pResult);

		m_pResult = mysql_store_result(&m_Sql);

		// 결과셋을 얻으려고 했는데 nullptr이다.
		if (m_pResult == nullptr)
		{
			// 쿼리가 실패한 것인지 아니면 return할 데이터가 실제로 없는 것인지 확인한다.
			int FieldCount = mysql_field_count(&m_Sql);

			// m_pResult == nullptr 인데 FieldCount가 0보다 크다면 이 쿼리는 진짜 오류가 난 것이다.
			if(FieldCount > 0)
			{
				// 쿼리 송신 실패하면 보냈던 쿼리를 모두 로그로 남긴다.
				// 정확한 확인을 위해 송신한 쿼리를 UTF 16으로 변환해 놓는다.	
				MultiByteToWideChar(CP_UTF8, MB_PRECOMPOSED, QueryString, -1, QueryString_UTF16, MAX_QUERY_STRING_LEN);

				// 에러번호와 문자열 얻어서 로그로 남긴다.				
				const char *DBError = mysql_error(&m_Sql);			

				// UTF8 문자열을 UTF16으로 변환하여 로그로 남긴다.
				MultiByteToWideChar(CP_UTF8, MB_PRECOMPOSED, DBError, -1, UTF16_DBError, 256);

				// 에러문자열, 에러코드, 문제가 된 쿼리를 로그에 남긴다.
				CLogger::GetInstance()->Log(_T("DB_LOG"), LEVEL::SYS, OUTMODE::FILE, _T("%s [Error Code] %d"), UTF16_DBError, mysql_errno(&m_Sql));
				CLogger::GetInstance()->Log(_T("DB_LOG"), LEVEL::SYS, OUTMODE::FILE, _T("[Error Query] %s"), QueryString_UTF16);

				// 서버 죽는다.
				CCrashDump::ForceCrash();
			}
		}		
	}

	return true;
}

MYSQL_ROW GGM::CDBConnector::FetchRow()
{
	// 뽑을 결과셋이 없으면 안됨
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
	// 결과셋 다 사용했으며 프리한다.
	if(m_pResult != nullptr)
	{
		mysql_free_result(m_pResult);
		m_pResult = nullptr;
	}

}


