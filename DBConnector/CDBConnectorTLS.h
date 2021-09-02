#pragma once
#include <Windows.h>
#include "CDBConnector.h"

namespace GGM
{ 
	// ���漱��
	class CDBConnector;

	template<typename T>
	class CLockFreeStack;

	//////////////////////////////////////////////////////////////
	// ��Ƽ ������ ȯ�濡�� �����ϰ� MYSQL DB ������ �ϱ����� Ŭ����
	// DB ������ ���� �⺻ ���̺귯�� �Լ����� ������ ������ Ŭ����
	// TLS CDBConnector Ŭ������ �����Ͽ� ����Ѵ�.
	//////////////////////////////////////////////////////////////	
	class CDBConnectorTLS
	{
	public:
		//////////////////////////////////////////////////////////////
		//	TLS �ε��� �ʱ�ȭ, ���� ���� ���� 				
		//////////////////////////////////////////////////////////////
					  CDBConnectorTLS() = delete;
		              CDBConnectorTLS(const char * IP, const char * Username, const char * password, const char * DB, USHORT port);

		//////////////////////////////////////////////////////////////
		//	��� �������� TLS�� ����Ǿ��� DBConnector�� ���ؼ� ����
		//////////////////////////////////////////////////////////////
		virtual       ~CDBConnectorTLS();

		//////////////////////////////////////////////////////////////
		//	�ܺο��� ���� �����帶�� �ڽ��� CDBConnector�� ������ ���
		//////////////////////////////////////////////////////////////
		CDBConnector* GetTlsDBConnector();

	protected:

		// CDBConnector�� ����� TLS �ε���
		DWORD                         m_TlsIndex;

		// �������� CDBConnecotr�� �������ֱ� ���� ���ÿ� ���� 
		// ���� �����尡 ���������ϹǷ� ������ ���û�� 
		// ���ҽ� ���� + DB ���Ἴ�� ����
		CLockFreeStack<CDBConnector*> *m_pConnectorStack;

		// ���� ����		
		char     m_IP[16];
		char     m_Username[64];
		char     m_Password[64];
		char     m_DBname[64];
		USHORT   m_DBPort;
	};

}