#pragma once

namespace GGM
{
	// CNetServer�� ��� ũ��
	constexpr auto NET_HEADER_LENGTH = 5;

	// CLanServer�� ��� ũ��
	constexpr auto LAN_HEADER_LENGTH = 2;

	// SendPost�� �ѹ��� ���� ��Ŷ�� ����
	constexpr auto MAX_PACKET_COUNT = 200;

	// LockFreeQueue Enqueue ����ġ
	constexpr auto MAX_LOCK_FREE_Q_SIZE = 100000;

	// SessionID �ʱⰪ
	constexpr auto INIT_SESSION_ID = 0xffffffffffffffff;

	// MMOServer SendThread ����
	constexpr auto SEND_THREAD_COUNT = 1;

	// MMOServer SendBufSize
	constexpr auto SEND_BUF_SIZE = 65536;	

	// IPV4 IP����
	constexpr auto IP_LEN = 16;

	// ������ ����õ� 
	constexpr auto WEB_TRY_LIMIT = 15;

	// �� ��û ������
	constexpr auto WEB_REQ_DELAY = 100;

	// �������Ͽ��� DB���� �о�� �� �ش� �������� �ִ� ���ڿ� ���� 
	constexpr auto DB_INFO_LEN = 64;

	// ���� ��ū�� ����
	constexpr auto TOKEN_LEN = 32;

	// ���� ����Ű ����
	constexpr auto SESSIONKEY_LEN = 64;

	// SHDB API�� ������ �� �� URL �ִ���� 
	constexpr auto SHDB_URL_LEN = 512;

	// ��Ʋ ������ũ �г���, ���̵� ���� 
	constexpr auto ID_LEN = 20;
	constexpr auto NICK_LEN = 20;

	// ��Ʋ���� ���� ��ū ���� �ֱ� (ms)
	constexpr auto TOKEN_CREATION_PERIOD = 20000;

	// ���� ���� ���� �� �ı����� ���ð�
	constexpr auto DESTROY_ROOM_TIME = 5000;

	// Ŭ���̾�ƮŰ �ʱⰪ
	constexpr auto INIT_CLIENTKEY = 0xffffffffffffffff;	

	// HitDamage ��Ŷ �����ð� (ms)
	constexpr auto HIT_DAMAGE_LIMIT_TIME = 100;

	// ������ ���� �浹 ���� 
	constexpr auto HIT_DISTANCE = 2.0f;
	constexpr auto HIT_DISTANCE_POW2 = 4.0f;
	constexpr auto HIT_DISTANCE_LIMIT = 17.0f;
	constexpr auto HIT_DISTANCE_LIMIT_POW2 = 289.0f;

	// ������ ȹ�� ���� ����
	constexpr auto ITEM_GET_DISTANCE = 2;

	// RedZone Time Sec 
	constexpr auto REDZONE_UNIT_TIME = 20000;	
	constexpr auto REDZONE_ALERT_TIME_IN_SEC = 20;
	constexpr auto REDZONE_TICK_RATE = 1000;
	constexpr auto TOTAL_REDZONE_COUNT = 9;

	constexpr auto DB_MSG_NULL = -1;

	// ä�ü��� �ִ� �޽��� ���� 
	constexpr auto CHAT_MSG_LEN = 512;
	constexpr auto CHAT_MAX_ROOM_USER = 10;
}
