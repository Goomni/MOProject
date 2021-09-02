#include "CMMOSession.h"
#include "CrashDump\CrashDump.h"

namespace GGM
{
	void GGM::CMMOSession::SendPacket(CNetPacket * pPacket)
	{
		// 로그아웃 플래그가 켜져있으면 곧 릴리즈 절차에 돌입할 예정이거나 릴리즈 중이라는 의미이므로 더 이상 패킷을 인큐하지 않음
		if (m_IsLogout == true)
			return;

		// 패킷을 인코딩한다.
		pPacket->Encode();

		// SendQ에 패킷의 포인터를 담는다.	
		InterlockedIncrement(&(pPacket->m_RefCnt));
		m_SendQ.Enqueue(pPacket);			
	}

	void CMMOSession::SendPacketAndDisconnect(CNetPacket * pPacket)
	{		
		// 로그아웃 플래그가 켜져있으면 곧 릴리즈 절차에 돌입할 예정이거나 릴리즈 중이라는 의미이므로 더 이상 패킷을 인큐하지 않음
		if (m_IsLogout == true)
			return;

		// 패킷을 인코딩한다.
		pPacket->Encode();

		// 해당 패킷의 완료통지가 도착하면 연결을 끊을 수 있도록 플래그를 켠다.
		// 나중에 완료통지에서 확인할 필요가 있으므로 해당 패킷의 포인터를 저장한다.
		m_pDisconnectPacket = pPacket;
		m_IsSendAndDisconnect = true;

		// SendQ에 패킷의 포인터를 담는다.	
		InterlockedIncrement(&(pPacket->m_RefCnt));
		m_SendQ.Enqueue(pPacket);					
	}

	void GGM::CMMOSession::Disconnect()
	{
		// 로그아웃 플래그가 켜져있으면 곧 릴리즈 절차에 돌입한다는 의미이므로 Disconnect 하지 않음
		if (m_IsLogout == true)
			return;
	
		m_IsCanceled = true;
		CancelIoEx((HANDLE)m_socket, nullptr);
	}

	void GGM::CMMOSession::SetAuthToGame()
	{
		// 컨텐츠 모듈에서 Auth 스레드에 존재하는 세션을 Game 스레드로 보내고 싶을 때 이 함수를 호출하여 플래그 셋팅
		// 이 함수 내부에서 직접 모드를 바꾸지 않는 이유는, 해당 세션의 모드는 해당 스레드에서만 바꾸는 것이 원칙이기 때문이다.
		// 만약 그렇게 하지 않으면 이전 모드에서 다른 모드로 바꾸기전에 해야 할일이나 새로운 모드에서 해야 할일을 하지 못하게 된다.
		// ex) Auth 모드에 있던 세션을 임의로 Game모드로 바꾸면 OnAuth_ClientLeave나 OnGame_ClientJoin에서 해야 할일을 하지 못하게 됨
		m_IsAuthToGame = true;
	}

	void CMMOSession::DecreaseIoCount()
	{
		// 세션에 대한 접근이 모두 끝났으므로 이전에 올린 I/O 카운트를 감소 시킨다.	
		LONG IoCount = InterlockedDecrement(&(m_IoCount));
		if (IoCount <= 0)
		{			
			if (IoCount < 0)
			{				
				CCrashDump::ForceCrash();
			}		

			// I/O Count가 0 이라면 로그아웃 플래그 온
			m_IsLogout = true;
		}
	}

}
