#pragma once

namespace GGM
{
	// CNetServer의 헤더 크기
	constexpr auto NET_HEADER_LENGTH = 5;

	// CLanServer의 헤더 크기
	constexpr auto LAN_HEADER_LENGTH = 2;

	// SendPost시 한번에 보낼 패킷의 개수
	constexpr auto MAX_PACKET_COUNT = 200;

	// LockFreeQueue Enqueue 제한치
	constexpr auto MAX_LOCK_FREE_Q_SIZE = 100000;

	// SessionID 초기값
	constexpr auto INIT_SESSION_ID = 0xffffffffffffffff;

	// MMOServer SendThread 개수
	constexpr auto SEND_THREAD_COUNT = 1;

	// MMOServer SendBufSize
	constexpr auto SEND_BUF_SIZE = 65536;	

	// IPV4 IP길이
	constexpr auto IP_LEN = 16;

	// 웹서버 연결시도 
	constexpr auto WEB_TRY_LIMIT = 15;

	// 웹 요청 딜레이
	constexpr auto WEB_REQ_DELAY = 100;

	// 설정파일에서 DB정보 읽어올 시 해당 데이터의 최대 문자열 길이 
	constexpr auto DB_INFO_LEN = 64;

	// 각종 토큰의 길이
	constexpr auto TOKEN_LEN = 32;

	// 인증 세션키 길이
	constexpr auto SESSIONKEY_LEN = 64;

	// SHDB API를 웹으로 쏠 때 URL 최대길이 
	constexpr auto SHDB_URL_LEN = 512;

	// 배틀 스네이크 닉네임, 아이디 길이 
	constexpr auto ID_LEN = 20;
	constexpr auto NICK_LEN = 20;

	// 배틀서버 접속 토큰 생성 주기 (ms)
	constexpr auto TOKEN_CREATION_PERIOD = 20000;

	// 게임 종료 이후 방 파괴까지 대기시간
	constexpr auto DESTROY_ROOM_TIME = 5000;

	// 클라이언트키 초기값
	constexpr auto INIT_CLIENTKEY = 0xffffffffffffffff;	

	// HitDamage 패킷 인정시간 (ms)
	constexpr auto HIT_DAMAGE_LIMIT_TIME = 100;

	// 발차기 공격 충돌 범위 
	constexpr auto HIT_DISTANCE = 2.0f;
	constexpr auto HIT_DISTANCE_POW2 = 4.0f;
	constexpr auto HIT_DISTANCE_LIMIT = 17.0f;
	constexpr auto HIT_DISTANCE_LIMIT_POW2 = 289.0f;

	// 아이템 획득 가능 범위
	constexpr auto ITEM_GET_DISTANCE = 2;

	// RedZone Time Sec 
	constexpr auto REDZONE_UNIT_TIME = 20000;	
	constexpr auto REDZONE_ALERT_TIME_IN_SEC = 20;
	constexpr auto REDZONE_TICK_RATE = 1000;
	constexpr auto TOTAL_REDZONE_COUNT = 9;

	constexpr auto DB_MSG_NULL = -1;

	// 채팅서버 최대 메시지 길이 
	constexpr auto CHAT_MSG_LEN = 512;
	constexpr auto CHAT_MAX_ROOM_USER = 10;
}
