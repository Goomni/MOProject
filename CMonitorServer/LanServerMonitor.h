#pragma once
#include "CLanServer\CLanServer.h"
#include "ConfigLoader\ConfigLoader.h"
#include "Define\GGM_CONSTANTS.h"

namespace GGM
{
	// 모니터링 정보 DB 저장 주기 
	constexpr auto DB_WRITE_PERIOD = 60000;

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
	class LanServerMonitorConfig : public CConfigLoader
	{
	public:

		TCHAR  BindIP[16];
		USHORT Port;
		DWORD  ConcurrentThreads;
		DWORD  MaxThreads;
		bool   IsNoDelay;
		WORD   MaxSessions;
		int    LogLevel;
		int    NumOfDataType;

		// 서버번호
		int    SysCollectorNo;
		int    MatchServerNo;
		int    MasterServerNo;
		int    BattleServerNo;
		int    ChatServerNo;

		// 모니터링 정보를 1분주기로 DB에 저장해야 하므로 DB 연결정보도 설정파일로부터 받는다.
		TCHAR  DB_ip[17];
		TCHAR  DB_username[65];
		TCHAR  DB_password[65];
		TCHAR  DB_dbname[65];
		USHORT DB_port;	
	public:

		virtual bool LoadConfig(const TCHAR* ConfigFileName) override;

	};	
	
	// LanClient로부터 전달받은 모니터링 데이터를 모니터링 뷰어에게 전달해줄 때 필요함
	// LanClient가 NetServer가 제공하는 함수를 통해 데이터를 송신한다.
	class CNetServerMonitor;

	// DB 연동시 사용할 커넥터		
	class CDBConnector;

	// 1분마다 한번씩 DB에 저장할 때 참조할 구조체 
	struct MonitoringData
	{
		// 가장 최근에 온 데이터를 저장, 최근 1분동안 아무런 데이터도 오지 않았다면 DB에 저장하지 않음
		int DataValue;

		// 1분동안 온 데이터 집계 수, 0 이면 DB에 데이터 저장하지 않음
		size_t DataCount = 0;

		// 1분동안 온 데이터의 총계 DB 저장시 평균 구할 때 사용
		ULONGLONG DataSum = 0;

		// 1분동안 온 데이터 중 최댓값
		int MaxValue = 0;

		// 1분동안 온 데이터 중 최솟값
		int MinValue = 0x7fffffff;

		// 동기화를 위한 객체
		// DB 저장 스레드와 LanClient의 워커 스레드가 이 구조체에 동시접근하므로 동기화 필요하다.
		SRWLOCK DataLock;

		// 해당 데이터를 송신한 서버의 정보
		char    ServerName[256];
		int     ServerNo;
	};

	// 모니터링 데이터를 제공하는 LanClient들의 구조체 
	struct MonitoringWorker
	{
		int        ServerNo;
		ULONGLONG  SessionID = INIT_SESSION_ID;
	};

	class CLanServerMonitor : public CLanServer
	{
	public:

		// 기본생성자는 사용하지 않는다. 반드시 설정정보를 받아와야 함
		CLanServerMonitor() = delete;

		/////////////////////////////////////////////////////////
		// 생성자
		// 인자 1 : LanServerMonitor를 위한 설정정보
		// 인자 2 : CNetServerMonitor의 포인터
		// 반환   : 없음
		/////////////////////////////////////////////////////////
		CLanServerMonitor(LanServerMonitorConfig *pLanMonitorConfig, CNetServerMonitor *pNetMonitor);

		/////////////////////////////////////////////////////////
		// 소멸자
		// 인자 : 없음
		// 반환 : 없음
		/////////////////////////////////////////////////////////
		virtual ~CLanServerMonitor();	

		ULONGLONG GetLanClientCount() const;

	protected:

		// CLanServer의 이벤트 핸들링용 순수가상함수를 이곳에서 구현

		// Accept 성공 후 접속이 확정되면 호출 
		virtual void OnClientJoin(const SOCKADDR_IN& ClientAddr, ULONGLONG SessionID) override;

		// 해당 세션에 대한 모든 리소스가 정리되면 호출  
		virtual void OnClientLeave(ULONGLONG SessionID) override;

		// Accept 직후 호출 
		virtual bool OnConnectionRequest(const SOCKADDR_IN& ClientAddr) override;

		// 패킷 수신 완료 후 호출
		virtual void OnRecv(ULONGLONG SessionID, CPacket *pPacket) override;

		// 패킷 송신 완료 후 호출
		virtual void OnSend(ULONGLONG SessionID, int SendSize) override;

		// 워커스레드 GQCS 바로 하단에서 호출 
		virtual void OnWorkerThreadBegin() override;

		// 워커스레드 1루프 종료 후 호출
		virtual void OnWorkerThreadEnd() override;

		// 내부적으로 진행되는 함수에서 오류가 발생할 시 본 함수를 호출해서 알려줌 
		virtual void OnError(int ErrorNo, const TCHAR* ErrorMsg) override;
	
		/////////////////////////////////////////////////
		// CLanServerMonitor 패킷 프로시저
		/////////////////////////////////////////////////

		// 일반서버의 LanClient가 모니터링 서버의 LanServer로 로그인 요청 
		void Monitor_Login(ULONGLONG SessionID, CPacket *pPacket);	

		// 일반서버의 LanClient가 모니터링 서버로 모니터링 데이터 전송
		void Monitor_Data_Update(ULONGLONG SessionID, CPacket *pPacket);

		///////////////////////////////////////////////////////////////
		// WriteDB
		// 인자 : LPVOID pThis [ CLanServerMonitor 객체의 This 포인터 ]
		// 반환 : unsigned int [ 스레드 종료 코드 ]
		// 기능 : 1분마다 주기적으로 모니터링 정보를 DB에 저장한다.
		///////////////////////////////////////////////////////////////

		static unsigned int __stdcall WriteDB(LPVOID pThis);

		///////////////////////////////////////////////////////////////
		// DBWriterExitFunc : DB Writer 스레드를 종료할 더미 콜백 함수	
		///////////////////////////////////////////////////////////////
		static void __stdcall DBWriterExitFunc(ULONG_PTR Param);

	protected:

		// CNetServerMonitor의 포인터, LanClient로부터 응답을 받았을 때 CNetServerMonitor에게 알려주기 위해 필요함.
		CNetServerMonitor  *m_pNetMonitor = nullptr;

		// LanServer와 연결된 LanClient를 관리하기 위한 배열
		MonitoringWorker  *m_LanClientArr = nullptr;
		ULONGLONG          m_LanClientCount = 0;					

		// 1분마다 데이터를 주기적으로 저장할 데이터 덩어리
		MonitoringData    *m_DataArray = nullptr;
		ULONGLONG          m_DataArrSize = 0;

		// TLS DB
		CDBConnector      *m_pDBConnector = nullptr;

		// DB 저장 스레드의 핸들
		HANDLE             m_hDBWriter;	
	};
}