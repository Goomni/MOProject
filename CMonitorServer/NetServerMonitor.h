#pragma once
#include "CNetServer\CNetServer.h"
#include "ConfigLoader\ConfigLoader.h"

namespace GGM
{
	///////////////////////////////////////////////////////////////////////////////////////////
	// 각 서버로부터 모니터링 정보를 전달받아서 뷰어인 모니터링 클라이언트로 쏴준다.
	// 서버 작동 흐름
	// 1. 채팅, 배틀, 매칭 등의 다른 서버들이 LanClient로서 LanServer인 모니터링 서버로 접속한다.
	// 2. 모니터링 정보 뷰어인 모니터링 클라이언트가 NetClient로서 NetServer인 모니터링 서버로 접속한다.
	// 3. LanClient가 LanServer에게 1초에 한번씩 모니터링 정보를 송신한다.
	//  3.1. 모니터링 정보는 크게 시스템 전체 정보와 서버별 정보로 나뉜다.
	//  3.2. 시스템 전체 정보는 하나의 서버가 담당하고, 나머지는 서버별로 각각 담당한다.
	// 4. LanServer는 LanClient에게 전달받은 정보를 NetClient에게 송신한다. 단순 중개 역할
	// 5. 4번에서 전달받은 정보를 메모리에 저장해두었다가 1분에 한번씩 로그 디비에 저장한다.
	//  5.1. 로그 디비에는 해당 서버 모니터링 데이터의 가장 최신정보, 평균, 최대, 최소 등이 들어간다.	
	///////////////////////////////////////////////////////////////////////////////////////////	

	// 서버 구동정보 
	// 외부 CONFIG 파일로부터 파서를 이용해서 가지고 온다.
	class NetServerMonitorConfig : public CConfigLoader
	{
	public:

		TCHAR  BindIP[16];
		USHORT Port;
		DWORD  ConcurrentThreads;
		DWORD  MaxThreads;
		bool   IsNoDelay;
		WORD   MaxSessions;
		BYTE   PacketCode;
		BYTE   PacketKey1;
		BYTE   PacketKey;
		int    LogLevel;
		TCHAR  LoginSessionkey[40];			

	public:

		virtual bool LoadConfig(const TCHAR* ConfigFileName) override;

	};

	// 모니터링 서버에 연결된 모니터링 뷰어의 정보	
	struct Viewer
	{
		ULONGLONG SessionID = INIT_SESSION_ID;
		bool      IsLogin = false;
	};

	// 전방선언 

	// 모니터링 서버의 LanServer용 설정정보
	class LanServerMonitorConfig;

	// 모니터링 서버의 LanServer의 포인터
	class CLanServerMonitor;

	// NetClient인 모니터링 클라이언트와 통신하는 NetServer
	class CNetServerMonitor : public CNetServer
	{
	public:

		// 기본생성자는 사용하지 않는다. 반드시 설정정보를 받아와야 함
		CNetServerMonitor() = delete;

		/////////////////////////////////////////////////////////
		// 생성자
		// 인자 1 : CNetServerMonitor를 위한 설정정보
		// 인자 2 : LanServerMonitorConfig 를 위한 설정정보
		// 반환   : 없음
		/////////////////////////////////////////////////////////
		CNetServerMonitor(NetServerMonitorConfig *pNetConfig, LanServerMonitorConfig *pLanConfig);

		/////////////////////////////////////////////////////////
		// 소멸자
		// 인자 : 없음
		// 반환 : 없음
		/////////////////////////////////////////////////////////
		virtual ~CNetServerMonitor();

		// 모니터링 서버가 모니터링 툴에게 데이터 전송
		void Monitor_Tool_Data_Update(BYTE ServerNo, char *pData);

		void PrintInfo();

	private:

		//////////////////////////////////////////////
		// 각종 이벤트 핸들링 함수
		//////////////////////////////////////////////

		// Accept 성공 후 접속이 확정되면 호출 
		virtual void OnClientJoin(const SOCKADDR_IN& ClientAddr, ULONGLONG SessionID) override;

		// 해당 세션에 대한 모든 리소스가 정리되면 호출  
		virtual void OnClientLeave(ULONGLONG SessionID) override;

		// Accept 직후 호출 
		virtual bool OnConnectionRequest(const SOCKADDR_IN& ClientAddr) override;

		// 패킷 수신 완료 후 호출
		virtual void OnRecv(ULONGLONG SessionID, CNetPacket *pPacket) override;

		// 패킷 송신 완료 후 호출
		virtual void OnSend(ULONGLONG SessionID, int SendSize) override;

		// 워커스레드 GQCS 바로 하단에서 호출 
		virtual void OnWorkerThreadBegin() override;

		// 워커스레드 1루프 종료 후 호출
		virtual void OnWorkerThreadEnd() override;

		// 내부적으로 진행되는 함수에서 오류가 발생할 시 본 함수를 호출해서 알려줌 
		virtual void OnError(int ErrorNo, const TCHAR* ErrorMsg) override;

		virtual void OnSemError(ULONGLONG SessionID) override;

		///////////////////////////////////////////////////////////////////////////
		// 패킷 프로시저 : 세부내용은 프로시저 참조 
		///////////////////////////////////////////////////////////////////////////

		// 모니터링 툴이 모니터링 서버에게 로그인 요청 
		void Monitor_Tool_Req_Login(ULONGLONG SessionID, CNetPacket *pPacket);

		// 모니터링 서버가 모니터링 툴로 로그인 응답 
		void Monitor_Tool_Res_Login(ULONGLONG SessionID, BYTE status);
		void CreatePacket_Monitor_Tool_Res_Login(BYTE status, CNetPacket *pPacket);

		// 모니터링 서버가 모니터링 툴에게 데이터 전송	
		void CreatePacket_Monitor_Tool_Data_Update(BYTE ServerNo, char *pData, CNetPacket *pPacket);

	private:

		// 연결된 모니터링 뷰어 관리배열, NetPacket을 전송하기 위한 세션 아이디만 있으면 된다.
		Viewer		      *m_ViewerArr;
		
		// 연결된 모니터링 뷰어의 수
		size_t			   m_ViewerCount = 0;
		size_t             m_MaxViewer = 0;

		// 뷰어 관리 배열의 락
		SRWLOCK            m_Lock;

		// CLanServerMonitor의 포인터 
		CLanServerMonitor *m_pLanMonitor;

		// 고정 세션키
		char               m_SessionKey[33];
	};

}