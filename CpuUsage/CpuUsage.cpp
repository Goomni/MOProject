#include "CpuUsage.h"
#include "CrashDump\CrashDump.h"

namespace GGM
{
	GGM::CCpuUsage::CCpuUsage(HANDLE hProcess)
	{
		// 생성자의 매개변수로 NULL이 들어왔다면 현재 프로세스가 대상이 된다.
		if (hProcess == NULL)
			m_hProcess = GetCurrentProcess();
		else
			m_hProcess = hProcess;

		// 현재 머신의 논리 프로세서 개수를 구한다.
		// 프로세스별 CPU 사용률을 구할 때, 멀티 프로세서 환경에서는 이 값으로 나누어주어야 한다.
		SYSTEM_INFO sysInfo;
		GetSystemInfo(&sysInfo);
		m_NumOfProcessors = sysInfo.dwNumberOfProcessors;

		// 초기시간 값 설정
		if (GetSystemTimes((LPFILETIME)&m_PrevSysIdle, (LPFILETIME)&m_PrevSysKernel, (LPFILETIME)&m_PrevSysUser) == false)
		{
			DWORD errorNo = GetLastError();
			CCrashDump::ForceCrash();
		}

		GetSystemTimeAsFileTime((LPFILETIME)&m_PrevSysTime);

		__int64 Dummy;
		if (GetProcessTimes(m_hProcess, (LPFILETIME)&Dummy, (LPFILETIME)&Dummy, (LPFILETIME)&m_PrevProcessKernel, (LPFILETIME)&m_PrevProcessUser) == false)
		{
			DWORD errorNo = GetLastError();
			CCrashDump::ForceCrash();
		}
		
		SleepEx(1000, false);
	}

	void CCpuUsage::UpdateTotalUsage()
	{
		// 전체 프로세서 사용률을 갱신한다.		
		__int64 CurSysIdle;
		__int64 CurSysKernel;
		__int64 CurSysUser;

		// 현재까지 CPU 사용한 시간을 구한다.
		if (GetSystemTimes((LPFILETIME)&CurSysIdle, (LPFILETIME)&CurSysKernel, (LPFILETIME)&CurSysUser) == false)
		{
			DWORD errorNo = GetLastError();
			CCrashDump::ForceCrash();
		}		

		// CPU 사용시간을 구하기 위해 이전에 구한 시간과 현재 시간의 차이를 구함
		__int64 SysIdleDiff = CurSysIdle - m_PrevSysIdle;
		__int64 SysKernelDiff = CurSysKernel - m_PrevSysKernel;
		__int64 SysUserDiff = CurSysUser - m_PrevSysUser;

		// 커널시간과 유저시간을 모두 더해서 총 시간을 구함
		__int64 SysTotal = SysKernelDiff + SysUserDiff;

		// 커널 시간에는 idle이 포함되어 있으므로 Idle을 빼줌
		__int64 RealKernelDiff = SysKernelDiff - SysIdleDiff;

		if (RealKernelDiff < 0)
			RealKernelDiff = 0;

		// 이전 시간과 현재 시간 사이의 CPU 사용률을 구함 
		m_SysTotalUsage = (((float)(RealKernelDiff + SysUserDiff)) * 100.0f) / (float) SysTotal;
		m_SysKernelUsage = ((float)(RealKernelDiff) * 100.0f) / (float)SysTotal;
		m_SysUserUsage = ((float)(SysUserDiff) * 100.0f) / (float)SysTotal;

		// 다음 CPU 사용률 계산을 위해 이번에 구한 시간들을 저장함
		m_PrevSysIdle = CurSysIdle;
		m_PrevSysKernel = CurSysKernel;
		m_PrevSysUser = CurSysUser;
	}

	void CCpuUsage::UpdateProcessUsage()
	{
		// 지금까지 흐른 총 시스템 타임을 구한다.		
		__int64 CurSysTime;				
		GetSystemTimeAsFileTime((LPFILETIME)&CurSysTime);

		// 현재 프로세스의 CPU 사용 시간을 구한다.
		__int64 CurProcessKernel;
		__int64 CurProcessUser;
		__int64 Dummy;
		GetProcessTimes(m_hProcess, (LPFILETIME)&Dummy, (LPFILETIME)&Dummy, (LPFILETIME)&CurProcessKernel, (LPFILETIME)&CurProcessUser);

		// CPU 사용시간을 구하기 위해 이전에 구한 시간과 현재 시간의 차이를 구함				
		__int64 SysTime = CurSysTime - m_PrevSysTime;
		__int64 ProcessKernelDiff = CurProcessKernel - m_PrevProcessKernel;
		__int64 ProcessUserDiff = CurProcessUser - m_PrevProcessUser;

		// 프로세스별 CPU 사용률을 구함 
		// 이 때 프로세스가 사용한 총 CPU 사용률을 코어 수로 나누어 주어야 한다.
		float NumOfProcessors = (float)m_NumOfProcessors;
		m_ProcessTotalUsage = ((float)(ProcessKernelDiff + ProcessUserDiff)) / NumOfProcessors * 100.0f / (float)SysTime;
		m_ProcessKernelUsage = ((float)(ProcessKernelDiff) / NumOfProcessors) * 100.0f / (float)SysTime;
		m_ProcessUserUsage = ((float)(ProcessUserDiff) / NumOfProcessors) * 100.0f / (float)SysTime;	

		// 다음 업데이트를 위해 정보를 갱신
		m_PrevSysTime = CurSysTime;
		m_PrevProcessKernel = CurProcessKernel;
		m_PrevProcessUser = CurProcessUser;
	}

	float CCpuUsage::GetSysTotalUsage() const
	{
		return m_SysTotalUsage;
	}

	float CCpuUsage::GetSysKernelUsage() const
	{
		return m_SysKernelUsage;
	}

	float CCpuUsage::GetSysUserUsage() const
	{
		return m_SysUserUsage;
	}

	float CCpuUsage::GetProcessTotalUsage() const
	{
		return m_ProcessTotalUsage;
	}

	float CCpuUsage::GetProcessKernelUsage() const
	{
		return m_ProcessKernelUsage;
	}

	float CCpuUsage::GetProcessUserUsage() const
	{
		return m_ProcessUserUsage;
	}

}
