#pragma once
#include "CLanClient\CLanClient.h"
#include "ConfigLoader\ConfigLoader.h"

namespace GGM
{
	// 모니터링 서버로 데이터를 송신하는 주기 
	constexpr auto MONITOR_PERIOD = 1000;

	// 서버 구동정보 
	// 외부 CONFIG 파일로부터 파서를 이용해서 가지고 온다.
	class LanClientMonitorConfig : public CConfigLoader
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
		int    ServerNo;
		bool   IsSysCollector;

	public:

		virtual bool LoadConfig(const TCHAR* ConfigFileName) override;
	};	

	// 전방선언

	// 채팅서버 포인터 (모니터링 정보를 얻어오기 위해 필요함)
	class CNetServerChat;
	
	// CPU 사용률 구하는 클래스
	class CCpuUsage;

	// 퍼포먼스 데이터 수집 클래스 
	class CPerfCollector;

	// 랜 클라이언트 모니터
	// 모니터링 서버에게 현재 채팅서버의 모니터링 정보를 송신한다.
	class CLanClientMonitor : public CLanClient
	{
	public:

		CLanClientMonitor() = delete;

		CLanClientMonitor(LanClientMonitorConfig *pLanConfig, CNetServerChat *pNetChat);

		virtual ~CLanClientMonitor();

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

		// 패킷 프로시저
		void Monitor_Login_Proc(int ServerNo);
		void Monitor_Data_Update_Proc(BYTE DataType, int DataValue, int TimeStamp);

		// 패킷 생성 함수
		void CreatePacket_Monitor_Login(CSerialBuffer *pPacket, int ServerNo);
		void CreatePacket_Monitor_Data_Update(CSerialBuffer *pPacket, BYTE DataType, int DataValue, int TimeStamp);

		// 모니터링 스레드 함수
		static unsigned int __stdcall MonitorThread(LPVOID Param);

		// 종료용 더미함수
		static void __stdcall LanMonitorExitFunc(ULONG_PTR Param);	
	
	protected:

		// 연결 유효성
		bool            m_IsConnected = false;

		// 내 서버 번호
		int             m_ServerNo;

		// 시스템 전체 데이터를 수집해야하는 서버인가
		bool            m_IsSysCollector;

		// 모니터링 스레드 핸들
		HANDLE          m_hMonitorThread;		

		// 채팅서버 포인터
		CNetServerChat  *m_pNetChat;

		// CPU 사용률 계산
		CCpuUsage	   *m_pCpuUsage;

		// 퍼포먼스 데이터 수집
		CPerfCollector *m_pPerfCollector;
	};

}