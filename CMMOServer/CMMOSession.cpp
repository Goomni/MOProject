#include "CMMOSession.h"
#include "CrashDump\CrashDump.h"

namespace GGM
{
	void GGM::CMMOSession::SendPacket(CNetPacket * pPacket)
	{
		// �α׾ƿ� �÷��װ� ���������� �� ������ ������ ������ �����̰ų� ������ ���̶�� �ǹ��̹Ƿ� �� �̻� ��Ŷ�� ��ť���� ����
		if (m_IsLogout == true)
			return;

		// ��Ŷ�� ���ڵ��Ѵ�.
		pPacket->Encode();

		// SendQ�� ��Ŷ�� �����͸� ��´�.	
		InterlockedIncrement(&(pPacket->m_RefCnt));
		m_SendQ.Enqueue(pPacket);			
	}

	void CMMOSession::SendPacketAndDisconnect(CNetPacket * pPacket)
	{		
		// �α׾ƿ� �÷��װ� ���������� �� ������ ������ ������ �����̰ų� ������ ���̶�� �ǹ��̹Ƿ� �� �̻� ��Ŷ�� ��ť���� ����
		if (m_IsLogout == true)
			return;

		// ��Ŷ�� ���ڵ��Ѵ�.
		pPacket->Encode();

		// �ش� ��Ŷ�� �Ϸ������� �����ϸ� ������ ���� �� �ֵ��� �÷��׸� �Ҵ�.
		// ���߿� �Ϸ��������� Ȯ���� �ʿ䰡 �����Ƿ� �ش� ��Ŷ�� �����͸� �����Ѵ�.
		m_pDisconnectPacket = pPacket;
		m_IsSendAndDisconnect = true;

		// SendQ�� ��Ŷ�� �����͸� ��´�.	
		InterlockedIncrement(&(pPacket->m_RefCnt));
		m_SendQ.Enqueue(pPacket);					
	}

	void GGM::CMMOSession::Disconnect()
	{
		// �α׾ƿ� �÷��װ� ���������� �� ������ ������ �����Ѵٴ� �ǹ��̹Ƿ� Disconnect ���� ����
		if (m_IsLogout == true)
			return;
	
		m_IsCanceled = true;
		CancelIoEx((HANDLE)m_socket, nullptr);
	}

	void GGM::CMMOSession::SetAuthToGame()
	{
		// ������ ��⿡�� Auth �����忡 �����ϴ� ������ Game ������� ������ ���� �� �� �Լ��� ȣ���Ͽ� �÷��� ����
		// �� �Լ� ���ο��� ���� ��带 �ٲ��� �ʴ� ������, �ش� ������ ���� �ش� �����忡���� �ٲٴ� ���� ��Ģ�̱� �����̴�.
		// ���� �׷��� ���� ������ ���� ��忡�� �ٸ� ���� �ٲٱ����� �ؾ� �����̳� ���ο� ��忡�� �ؾ� ������ ���� ���ϰ� �ȴ�.
		// ex) Auth ��忡 �ִ� ������ ���Ƿ� Game���� �ٲٸ� OnAuth_ClientLeave�� OnGame_ClientJoin���� �ؾ� ������ ���� ���ϰ� ��
		m_IsAuthToGame = true;
	}

	void CMMOSession::DecreaseIoCount()
	{
		// ���ǿ� ���� ������ ��� �������Ƿ� ������ �ø� I/O ī��Ʈ�� ���� ��Ų��.	
		LONG IoCount = InterlockedDecrement(&(m_IoCount));
		if (IoCount <= 0)
		{			
			if (IoCount < 0)
			{				
				CCrashDump::ForceCrash();
			}		

			// I/O Count�� 0 �̶�� �α׾ƿ� �÷��� ��
			m_IsLogout = true;
		}
	}

}
