#ifndef __CPU_USAGE__
#define __CPU_USAGE__
#include <Windows.h>

namespace GGM
{
	//////////////////////////////////////////////////////
	// CCpuUsage Ŭ����
	// 1. �ֱ������� �ý��� ��ü CPU ������ ���Ѵ�.
	// 2. �ֱ������� Ư�� ���μ����� CPU ������ ���Ѵ�.
	//////////////////////////////////////////////////////
	class CCpuUsage
	{
	public:

		// �����ڿ��� CPU ������ ���� ���μ����� �ڵ��� ���޹޴´�.
		// ����Ʈ �Ű������� ���޵Ǹ� �������� ���� ���μ����� ����̵ȴ�.
		CCpuUsage(HANDLE hProcess = NULL);

		virtual ~CCpuUsage() = default;

		// ���� �ð� ���� ��ü / ���μ����� CPU ������ ������Ʈ �Ѵ�.
		void    UpdateTotalUsage();
		void    UpdateProcessUsage();

		// �ý��� ��ü CPU ������ ��´�.
		float   GetSysTotalUsage() const;
		float   GetSysKernelUsage() const;
		float   GetSysUserUsage() const;

		// Ư�� ���μ����� CPU ������ ��´�.
		float   GetProcessTotalUsage() const;
		float   GetProcessKernelUsage() const;
		float   GetProcessUserUsage() const;

	protected:

		// ���� CPU ������ ������ ���μ����� �ڵ�
		HANDLE	  m_hProcess = NULL;

		// ���� �ӽ��� ���μ��� ����
		DWORD	  m_NumOfProcessors;

		// ���� �ý��� ����
		__int64  m_PrevSysKernel = 0;
		__int64  m_PrevSysUser = 0;
		__int64  m_PrevSysIdle = 0;

		// ���� ���μ��� ����
		__int64  m_PrevSysTime = 0;
		__int64  m_PrevProcessKernel = 0;
		__int64  m_PrevProcessUser = 0;

		// �ý��� ��ü CPU ����
		float     m_SysTotalUsage = 0.0;
		float     m_SysKernelUsage = 0.0;
		float     m_SysUserUsage = 0.0;

		// ���μ��� CPU ����
		float     m_ProcessTotalUsage = 0.0;
		float     m_ProcessKernelUsage = 0.0;
		float     m_ProcessUserUsage = 0.0;
	};	
}

#endif