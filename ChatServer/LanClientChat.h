#pragma once
#include "CLanClient\CLanClient.h"
#include "ConfigLoader\ConfigLoader.h"
#include <unordered_map>

namespace GGM
{	
	// 서버 구동정보 
	// 외부 CONFIG 파일로부터 파서를 이용해서 가지고 온다.
	class LanClientChatConfig : public CConfigLoader
	{
	public:

		TCHAR  ServerIP[16];
		USHORT Port;
		DWORD  ConcurrentThreads;
		DWORD  MaxThreads;
		bool   IsNoDelay;
		int    LogLevel;
		bool   IsReconnect;
		int    ReconnectDelay;

	public:

		virtual bool LoadConfig(const TCHAR* ConfigFileName) override;

	};

	// 인증 토큰
	struct Token
	{
		char       SessionKey[64];
		ULONGLONG  InsertTime;
	};

	// 전방선언
	class CNetServerChat;	

	// 랜 클라이언트 채팅
	// 로그인 서버와 통신하면서 클라이언트 로그인 요청에 대한 인증토큰을 받아온다.
	class CLanClientChat : public CLanClient
	{
	public:

		CLanClientChat() = delete;

		CLanClientChat(
			LanClientChatConfig               *pLanConfig,  // 서버 구동시 설정 정보
			CNetServerChat				      *pNetChat,    // CNetServerChat 포인터 (HAS-A)
			std::unordered_map<INT64, Token*> *pTokenTable, // 토큰 저장 테이블 
			PSRWLOCK                           pLock,       // 토큰 락 객체
			CTlsMemoryPool<Token>             *pTokenPool   // 토큰 메모리 풀
		);

		virtual ~CLanClientChat();

	protected:

		/////////////////////////////////////////////////////////////////
		// * 네트워크 라이브러리 순수 가상함수
		// * 모든 이벤트 핸들링 함수는 순수가상함수로서 상속받는 쪽에서 정의한다.
		/////////////////////////////////////////////////////////////////		

		// LanServer와의 연결이 성공한 직후 호출 
		virtual void OnConnect() override;

		// 한 세션에 대해 I/O Count가 정리되고 연결 및 리소스가 정리되었을 때 호출된다. 
		virtual void OnDisconnect() override;

		// 패킷 수신 완료 후 호출
		virtual void OnRecv(CSerialBuffer *Packet) override;

		// 패킷 송신 완료 후 호출
		virtual void OnSend(int SendSize) override;

		// 워커스레드 GQCS 바로 하단에서 호출 
		virtual void OnWorkerThreadBegin() override;

		// 워커스레드 1루프 종료 후 호출
		virtual void OnWorkerThreadEnd() override;

		// 내부적으로 진행되는 함수에서 오류가 발생할 시 본 함수를 호출해서 알려줌 
		virtual void OnError(int ErrorNo, const TCHAR* ErrorMsg) override;	

		void Res_Client_Login_Proc(INT64 AccountNo, INT64 Param);
	protected:

		// NetServerChat의 포인터
		CNetServerChat                    *m_pNetChat;

		// 토큰 테이블
		std::unordered_map<INT64, Token*> *m_pTokenTable;
		
		// 토큰 테이블 락
		PSRWLOCK                           m_pLock;

		// 토큰 할당 풀
		CTlsMemoryPool<Token>             *m_pTokenPool;
	};
}