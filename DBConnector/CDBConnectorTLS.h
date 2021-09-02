#pragma once
#include <Windows.h>
#include "CDBConnector.h"

namespace GGM
{ 
	// 전방선언
	class CDBConnector;

	template<typename T>
	class CLockFreeStack;

	//////////////////////////////////////////////////////////////
	// 멀티 스레드 환경에서 안전하게 MYSQL DB 연결을 하기위한 클래스
	// DB 연결을 위한 기본 라이브러리 함수들을 가볍게 래핑한 클래스
	// TLS CDBConnector 클래스를 저장하여 사용한다.
	//////////////////////////////////////////////////////////////	
	class CDBConnectorTLS
	{
	public:
		//////////////////////////////////////////////////////////////
		//	TLS 인덱스 초기화, 연결 정보 저장 				
		//////////////////////////////////////////////////////////////
					  CDBConnectorTLS() = delete;
		              CDBConnectorTLS(const char * IP, const char * Username, const char * password, const char * DB, USHORT port);

		//////////////////////////////////////////////////////////////
		//	모든 스레드의 TLS에 저장되었던 DBConnector에 대해서 정리
		//////////////////////////////////////////////////////////////
		virtual       ~CDBConnectorTLS();

		//////////////////////////////////////////////////////////////
		//	외부에서 개별 스레드마다 자신의 CDBConnector을 얻을때 사용
		//////////////////////////////////////////////////////////////
		CDBConnector* GetTlsDBConnector();

	protected:

		// CDBConnector가 저장될 TLS 인덱스
		DWORD                         m_TlsIndex;

		// 마지막에 CDBConnecotr를 정리해주기 위해 스택에 보관 
		// 여러 스레드가 동시접근하므로 락프리 스택사용 
		// 리소스 정리 + DB 연결세션 종료
		CLockFreeStack<CDBConnector*> *m_pConnectorStack;

		// 연결 정보		
		char     m_IP[16];
		char     m_Username[64];
		char     m_Password[64];
		char     m_DBname[64];
		USHORT   m_DBPort;
	};

}