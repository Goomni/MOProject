#pragma once
#include "sql\include\mysql.h"
#include "sql\include\errmsg.h"
#include "Logger\Logger.h"
#pragma comment(lib, "DBConnector/mysqlclient.lib")

namespace GGM
{
	//////////////////////////////////////////////////////////////
	// MYSQL DB 연결을 위한 클래스
	// DB 연결을 위한 기본 라이브러리 함수들을 가볍게 래핑한 클래스
	// 스레드 세이프하지 않다.
	// 멀티 스레드 환경에서 사용한다면 CDBConnectorTLS를 사용해야 함
	//////////////////////////////////////////////////////////////	

	constexpr auto MAX_QUERY_STRING_LEN = 4096;

	class CDBConnector
	{
	public:		

		/////////////////////////////////////////////////////////
		// 인자1 :  const TCHAR  *IP       [DB 서버 IP]
		// 인자2 :  const TCHAR  *Username [DB 서버 계정]
		// 인자3 :  const TCHAR  *password [DB 서버 패스워드]
		// 인자4 :  const TCHAR  *DB       [연결한 DB(스키마)]
		// 인자5 :  const USHORT *DB       [DB 서버 연결 포트]
		// 기능  :  DB 연결 정보 초기화, MYSQL 구조체 초기화
		/////////////////////////////////////////////////////////
				 CDBConnector() = delete;
				 CDBConnector(const char *IP, const char *Username, const char *password, const char *DB, USHORT port);
		virtual ~CDBConnector();

		/////////////////////////////////////////////////////////
	    // 인자  : 없음
		// 반환  : 없음
		// 기능  : DB 연결, 연결 실패시 서버 죽음		
		/////////////////////////////////////////////////////////
		void     DBConnect();	

		/////////////////////////////////////////////////////////
		// 인자  : 없음
		// 반환  : 없음
		// 기능  : DB 연결 끊기
		/////////////////////////////////////////////////////////
		void     DBDisconnect();	

		//////////////////////////////////////////////////////////////////////	
		// 인자  : bool  ResultKeep               [ 결과셋 임시 저장 여부 ]
		// 인자  : const TCHAR *StringFormat, ... [ 쿼리 문자열, 가변인자 ]		
		// 반환  : bool ( 성공시 true, 실패시 false) 		
		// 기능  : DB 서버에 쿼리 송신
		//////////////////////////////////////////////////////////////////////
		bool	 SendQuery(bool ResultKeep, const char *StringFormat, ...);

		/////////////////////////////////////////////////////////////////////////
		// 인자  : 없음
		// 반환  : MYSQL_ROW 
		// 기능  
		// - 결과를 저장할 필요가 있는 쿼리를 날린 경우, 멤버에 결과셋이 임시로 저장됨
		// - 받아올 유효한 결과셋이 있다면 해당 내용, 없거나 함수 호출 실패시 nullptr
		/////////////////////////////////////////////////////////////////////////
		MYSQL_ROW FetchRow();

		//////////////////////////////////////////////////////////////////////	
		// 인자  : 없음
		// 반환  : MYSQL_RES*
		// 기능  : 멤버에 임시로 저장된 결과셋을 반환
		//////////////////////////////////////////////////////////////////////
		MYSQL_RES* GetResult();

		//////////////////////////////////////////////////////////////////////	
		// 인자  : 없음
		// 반환  : 없음 
		// 기능  : 멤버에 임시로 저장된 결과셋을 해제함
		//////////////////////////////////////////////////////////////////////
		void      FreeResult();	

	private:		

		// MYSQL 구조체와 포인터
		MYSQL      m_Sql;
		MYSQL     *m_pSql = nullptr;

		// 쿼리를 송신한 후 결과셋을 임시로 저장할 포인터 변수
		MYSQL_RES *m_pResult = nullptr;

		// 연결 정보
		// 쿼리를 송신하다가 모종의 이유로 연결이 끊기면 일정횟수 재시도하기 위해 연결정보 저장
		char     m_IP[16];
		char     m_Username[64];
		char     m_Password[64];
		char     m_DBname[64];
		USHORT   m_DBPort;			
	};
	
}




