#pragma once

//////////////////////////////////////////////////////////////
// Ŀ���� �����ڵ�
//////////////////////////////////////////////////////////////

namespace GGM
{
	enum GGM_ERROR
	{
		// ���� ����
		TOO_MANY_CONNECTION = 90000,
		INVALID_SERVER_IP,
		CONNECT_TIME_OUT,
		CONNECT_FAILED,
		ONCONNECTION_REQ_FAILED,
		STARTUP_FAILED,
		INDEX_STACK_POP_FAILED,

		// ���� ����
		AUTH_FAILED,

		// ���� ����ȭ ���� 
		SESSION_SYNC_FAILED,
		NEGATIVE_IO_COUNT,
		ALREADY_LOGIN,

		// ���� ����
		BUFFER_WRITE_FAILED,
		BUFFER_READ_FAILED,
		BUFFER_FULL,
		LOCK_FREE_Q_ENQ_FAILED,
		LOCK_FREE_Q_DEQ_FAILED,

		// ��Ŷ ������ ����
		WRONG_PACKET_TYPE,

		// ����͸� ����
		ADD_COUNTER_FAILED,

		// shdbAPI, �� ���� ���� 
		JSON_MEMBER_NOT_FOUND,
		SHDB_API_SUCCESS = 1,
		SHDB_API_INVALID_ACCOUNTNO = -10,
		SHDB_API_DATA_NO_ACCOUNT = -11,
		SHDB_API_DATA_NO_CONTENTS = -12,
		SHDB_API_MASTER_CONNECT_FAILED = -50,
		SHDB_API_SLAVE_CONNECT_FAILED = -51,
		SHDB_API_DATA_CONNECT_FAILED = -52,
		SHDB_API_QUERY_FAILED = -60,
		SHDB_INVALID_COLUMN = -61,
		SHDB_INVALID_TABLE = -62

	};

}