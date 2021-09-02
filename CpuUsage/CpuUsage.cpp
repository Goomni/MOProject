#include "CpuUsage.h"
#include "CrashDump\CrashDump.h"

namespace GGM
{
	GGM::CCpuUsage::CCpuUsage(HANDLE hProcess)
	{
		// �������� �Ű������� NULL�� ���Դٸ� ���� ���μ����� ����� �ȴ�.
		if (hProcess == NULL)
			m_hProcess = GetCurrentProcess();
		else
			m_hProcess = hProcess;

		// ���� �ӽ��� �� ���μ��� ������ ���Ѵ�.
		// ���μ����� CPU ������ ���� ��, ��Ƽ ���μ��� ȯ�濡���� �� ������ �������־�� �Ѵ�.
		SYSTEM_INFO sysInfo;
		GetSystemInfo(&sysInfo);
		m_NumOfProcessors = sysInfo.dwNumberOfProcessors;

		// �ʱ�ð� �� ����
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
		// ��ü ���μ��� ������ �����Ѵ�.		
		__int64 CurSysIdle;
		__int64 CurSysKernel;
		__int64 CurSysUser;

		// ������� CPU ����� �ð��� ���Ѵ�.
		if (GetSystemTimes((LPFILETIME)&CurSysIdle, (LPFILETIME)&CurSysKernel, (LPFILETIME)&CurSysUser) == false)
		{
			DWORD errorNo = GetLastError();
			CCrashDump::ForceCrash();
		}		

		// CPU ���ð��� ���ϱ� ���� ������ ���� �ð��� ���� �ð��� ���̸� ����
		__int64 SysIdleDiff = CurSysIdle - m_PrevSysIdle;
		__int64 SysKernelDiff = CurSysKernel - m_PrevSysKernel;
		__int64 SysUserDiff = CurSysUser - m_PrevSysUser;

		// Ŀ�νð��� �����ð��� ��� ���ؼ� �� �ð��� ����
		__int64 SysTotal = SysKernelDiff + SysUserDiff;

		// Ŀ�� �ð����� idle�� ���ԵǾ� �����Ƿ� Idle�� ����
		__int64 RealKernelDiff = SysKernelDiff - SysIdleDiff;

		if (RealKernelDiff < 0)
			RealKernelDiff = 0;

		// ���� �ð��� ���� �ð� ������ CPU ������ ���� 
		m_SysTotalUsage = (((float)(RealKernelDiff + SysUserDiff)) * 100.0f) / (float) SysTotal;
		m_SysKernelUsage = ((float)(RealKernelDiff) * 100.0f) / (float)SysTotal;
		m_SysUserUsage = ((float)(SysUserDiff) * 100.0f) / (float)SysTotal;

		// ���� CPU ���� ����� ���� �̹��� ���� �ð����� ������
		m_PrevSysIdle = CurSysIdle;
		m_PrevSysKernel = CurSysKernel;
		m_PrevSysUser = CurSysUser;
	}

	void CCpuUsage::UpdateProcessUsage()
	{
		// ���ݱ��� �帥 �� �ý��� Ÿ���� ���Ѵ�.		
		__int64 CurSysTime;				
		GetSystemTimeAsFileTime((LPFILETIME)&CurSysTime);

		// ���� ���μ����� CPU ��� �ð��� ���Ѵ�.
		__int64 CurProcessKernel;
		__int64 CurProcessUser;
		__int64 Dummy;
		GetProcessTimes(m_hProcess, (LPFILETIME)&Dummy, (LPFILETIME)&Dummy, (LPFILETIME)&CurProcessKernel, (LPFILETIME)&CurProcessUser);

		// CPU ���ð��� ���ϱ� ���� ������ ���� �ð��� ���� �ð��� ���̸� ����				
		__int64 SysTime = CurSysTime - m_PrevSysTime;
		__int64 ProcessKernelDiff = CurProcessKernel - m_PrevProcessKernel;
		__int64 ProcessUserDiff = CurProcessUser - m_PrevProcessUser;

		// ���μ����� CPU ������ ���� 
		// �� �� ���μ����� ����� �� CPU ������ �ھ� ���� ������ �־�� �Ѵ�.
		float NumOfProcessors = (float)m_NumOfProcessors;
		m_ProcessTotalUsage = ((float)(ProcessKernelDiff + ProcessUserDiff)) / NumOfProcessors * 100.0f / (float)SysTime;
		m_ProcessKernelUsage = ((float)(ProcessKernelDiff) / NumOfProcessors) * 100.0f / (float)SysTime;
		m_ProcessUserUsage = ((float)(ProcessUserDiff) / NumOfProcessors) * 100.0f / (float)SysTime;	

		// ���� ������Ʈ�� ���� ������ ����
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
