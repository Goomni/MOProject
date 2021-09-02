#ifndef __CPU_USAGE__
#define __CPU_USAGE__
#include <Windows.h>

namespace GGM
{
	//////////////////////////////////////////////////////
	// CCpuUsage 클래스
	// 1. 주기적으로 시스템 전체 CPU 사용률을 구한다.
	// 2. 주기적으로 특정 프로세스의 CPU 사용률을 구한다.
	//////////////////////////////////////////////////////
	class CCpuUsage
	{
	public:

		// 생성자에서 CPU 사용률을 구할 프로세스의 핸들을 전달받는다.
		// 디폴트 매개변수가 전달되면 실행중인 현재 프로세스가 대상이된다.
		CCpuUsage(HANDLE hProcess = NULL);

		virtual ~CCpuUsage() = default;

		// 일정 시간 마다 전체 / 프로세스별 CPU 사용률을 업데이트 한다.
		void    UpdateTotalUsage();
		void    UpdateProcessUsage();

		// 시스템 전체 CPU 사용률을 얻는다.
		float   GetSysTotalUsage() const;
		float   GetSysKernelUsage() const;
		float   GetSysUserUsage() const;

		// 특정 프로세스의 CPU 사용률을 얻는다.
		float   GetProcessTotalUsage() const;
		float   GetProcessKernelUsage() const;
		float   GetProcessUserUsage() const;

	protected:

		// 현재 CPU 사용률을 측정할 프로세스의 핸들
		HANDLE	  m_hProcess = NULL;

		// 현재 머신의 프로세서 개수
		DWORD	  m_NumOfProcessors;

		// 이전 시스템 정보
		__int64  m_PrevSysKernel = 0;
		__int64  m_PrevSysUser = 0;
		__int64  m_PrevSysIdle = 0;

		// 이전 프로세스 정보
		__int64  m_PrevSysTime = 0;
		__int64  m_PrevProcessKernel = 0;
		__int64  m_PrevProcessUser = 0;

		// 시스템 전체 CPU 사용률
		float     m_SysTotalUsage = 0.0;
		float     m_SysKernelUsage = 0.0;
		float     m_SysUserUsage = 0.0;

		// 프로세스 CPU 사용률
		float     m_ProcessTotalUsage = 0.0;
		float     m_ProcessKernelUsage = 0.0;
		float     m_ProcessUserUsage = 0.0;
	};	
}

#endif