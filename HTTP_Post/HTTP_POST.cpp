#include "HTTP_POST.h"
#include <stdlib.h>
#include "Logger\Logger.h"
#include "CrashDump\CrashDump.h"

namespace GGM
{
	CHttpPost::CHttpPost(const TCHAR *RemoteHost, HOST addr)
	{
		//////////////////////////////////////////////////////////////////////////////////////////
		// ���� ������ IP �ּҰ� �ƴ� ������ �̸��� �Է��ߴٸ� connect �ϱ� ���ؼ� �ּ������� ���ͼ�
		// �ּұ���ü�� �־��־�� �Ѵ�.
		// ȣ��Ʈ �̸��� ���� �ּ������� ������ ���� ����
		//////////////////////////////////////////////////////////////////////////////////////////
		if (addr == HOST::DOMAIN_NAME)
		{
			ADDRINFOW *result = nullptr;
			ADDRINFOW *ptr = nullptr;

			ADDRINFOW hints;
			ZeroMemory(&hints, sizeof(hints));
			hints.ai_family = AF_INET;
			hints.ai_socktype = SOCK_STREAM;
			hints.ai_protocol = IPPROTO_TCP;

			TCHAR szAddr[16];
			int iResult;

			// �ּ� ���� ������ �Լ� ȣ��
			iResult = GetAddrInfoW(RemoteHost, _T("http"), &hints, &result);

			if (iResult == 0)
			{
				// �ּ� ���� ���������� ���Դٸ� ��� �ּ� ����ü�� �����Ѵ�.
				SOCKADDR_IN *pAddr = (SOCKADDR_IN*)result->ai_addr;
				m_RemoteAddr = *pAddr;

				// �ش� �ּҸ� ���ڿ��� ��ȯ�ϰ�
				InetNtop(AF_INET, &(pAddr->sin_addr), szAddr, 16);

				// ANSI ���ڿ��� ��ȯ�ؼ� ����� �����Ѵ�. �� ���ڿ��� ���߿� HTTP �� �� ����� ���Ե� ���̴�.
				
				(CP_ACP, 0, szAddr, 16, m_RemoteIP, 16, NULL, NULL);
			}
		}
		else
		{
			// �������� ���ڷ� IP�� �ּҸ� �Է¹޾��� ��쿡�� ���� ���� ������ ��ĥ �ʿ䰡 ����.
			// ANSI ���ڿ��� ��ȯ�ؼ� ����� �����Ѵ�. �� ���ڿ��� ���߿� HTTP �� �� ����� ���Ե� ���̴�.
			WideCharToMultiByte(CP_ACP, 0, RemoteHost, -1, m_RemoteIP, 16, NULL, NULL);
			m_RemoteAddr.sin_family = AF_INET;
			m_RemoteAddr.sin_port = htons(WEB_SERVER_PORT);
			InetPton(AF_INET, RemoteHost, &m_RemoteAddr.sin_addr);
		}
	}

	int CHttpPost::ConnectToWebServer(SOCKADDR_IN *pAddr)
	{
		// ���� ����
		SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

		if (sock == INVALID_SOCKET)
			return SOCKET_ERROR;

		// connect �� �� ���� ������ �̿��Ѵ�.
		u_long NonBlockOn = 1;
		int iResult = ioctlsocket(sock, FIONBIO, &NonBlockOn);

		if (iResult == SOCKET_ERROR)
		{
			return SOCKET_ERROR;
		}

		//linger
		linger lingeropt = { 0,0 };
		iResult = setsockopt(sock, SOL_SOCKET, SO_LINGER, (char*)&lingeropt, sizeof(lingeropt));

		if (iResult == SOCKET_ERROR)
		{
			return SOCKET_ERROR;
		}

		// REUSEADDR
		ULONG IsReuseAddr = TRUE;
		iResult = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)&IsReuseAddr, sizeof(IsReuseAddr));

		if (iResult == SOCKET_ERROR)
		{
			return SOCKET_ERROR;
		}

		// send �� recv�� ����������� ������ ���̱� ������ ������ ������ ���� ���� ����� Ÿ�Ӿƿ��� �ش�.
		int iTimeOut = TRANSMIT_TIMEOUT;
		iResult = setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&iTimeOut, sizeof(iTimeOut));

		if (iResult == SOCKET_ERROR)
		{
			return SOCKET_ERROR;
		}

		iResult = setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char*)&iTimeOut, sizeof(iTimeOut));

		if (iResult == SOCKET_ERROR)
		{
			return SOCKET_ERROR;
		}

		int ConnectTry = 0;
		while(true)
		{
			// �������� ���� ���н� 5������ �õ��Ѵ�. 
			ConnectTry++;

			// Connect�� �������� ó���ϰ� �� ����� Ȯ���ϱ� ���ؼ� select�� Ȱ���Ѵ�.			
			// Connect �ι� ȣ���ϴ� ����
			// ���࿡ �ι�° connect ȣ�� ���� �� Error ���� WSAEISCONN �� �ָ� Select ���ص� �Ǳ� �����̴�.		
			iResult = connect(sock, (SOCKADDR*)pAddr, sizeof(SOCKADDR_IN));
			connect(sock, (SOCKADDR*)pAddr, sizeof(SOCKADDR_IN));

			// ���� ������ ��� connect�� ȣ���ϸ� ���� �� �ִ� ������ ����� ���� ������ ����.
			// 1. ���� ȣ��� �ٷ� WSAEWOULDBLOCK 
			// 2. ���� ������ �Ϸ���� �ʾҴµ� �ٽ� connect ȣ���ϸ� WSAEALREADY
			// 3. �̹� ������ �Ϸ�� ���Ͽ� ���ؼ� �ٽ� connect ȣ���ϸ� WSAEISCONN
			// 4. �� �̿��� ���� WSAECONNREFUSED, WSAENETUNREACH, WSAETIMEDOUT 
			DWORD dwError = WSAGetLastError();

			// ���� ���� 
			if (dwError == WSAEISCONN)
				break;

			if (dwError == WSAEWOULDBLOCK || dwError == WSAEALREADY)
			{
				// ����¿� ���� �ִ´�.
				FD_SET WriteSet;
				WriteSet.fd_count = 0;
				WriteSet.fd_array[0] = sock;
				WriteSet.fd_count++;

				// ���ܼ¿� ���� �ִ´�.
				FD_SET ExceptionSet;
				ExceptionSet.fd_count = 0;
				ExceptionSet.fd_array[0] = sock;
				ExceptionSet.fd_count++;
				timeval TimeVal = { 0, 500000 };

				// select�� timeval ����ŭ�� ������ ��ٸ���.
				iResult = select(0, nullptr, &WriteSet, &ExceptionSet, &TimeVal);

				if ((iResult == 0 || ExceptionSet.fd_count == 1) && ConnectTry >= 5)
				{
					// �� ��쿣 connect ��û �� 500ms ������ select�� Ÿ�Ӿƿ��� �Ǿ��ų� ������ �����ؼ� ���ܼ¿� ������ ����ִ� ���̴�.
					// 5�������� ��õ��Ѵ�. ���Ŀ��� ���� ���з� ���� 
					closesocket(sock);
					sock = INVALID_SOCKET;
					return SOCKET_ERROR;
				}
				else if (WriteSet.fd_count == 1)
				{
					// ���� ���� 
					break;
				}
			}

		}

		// ���� �������� �ٲ� ������ �ٽ� ��� �������� �ٲپ� �ش�		
		NonBlockOn = 0;
		iResult = ioctlsocket(sock, FIONBIO, &NonBlockOn);

		if (iResult == SOCKET_ERROR)
		{
			return SOCKET_ERROR;
		}

		m_Socket = sock;

		return 0;
	}

	int CHttpPost::SendHttpRequest(char *RemoteIP, IN const char *Path, IN const char *Body)
	{
		// HTTP�� �����ϰ��� �ϴ� Path, Body�� ���ڷ� ���޹޾� �������� ����Ʈ�Ѵ�.
		// �ϴ� ������ POST�� �� ���̴�.			
		// PATH ������ �����Ѵ�. 	
		char *Request = m_Request;
		int iReqBuf = MAX_REQUEST_LEN;
		char *pBuf = Request + 12; 
		int iRequestSize = 12;
		iReqBuf -= 12;
		int iLength = (int)strlen(Path);
		memcpy_s(pBuf, MAX_REQUEST_LEN, Path, iLength);
		pBuf += iLength;
		iRequestSize += iLength;
		iReqBuf -= iLength;

		// �� �κ��� �Ȱ��� ������ ���̹Ƿ� �ϵ��ڵ� 		
		memcpy_s(pBuf, iReqBuf, " HTTP/1.1\r\nUser-Agent: GUMIN\r\nHost: ", 36);
		pBuf += 36;
		iRequestSize += 36;
		iReqBuf -= 36;

		// HOST IP ��������
		iLength = (int)strlen(RemoteIP);
		memcpy_s(pBuf, iReqBuf, RemoteIP, iLength);
		pBuf += iLength;
		iRequestSize += iLength;
		iReqBuf -= iLength;

		// Content-Length�� ��ġ�������� ��� �Ȱ��� ���̹Ƿ� �� �κе� �ϵ��ڵ� 		
		memcpy_s(pBuf, iReqBuf, "\r\nConnection: Close\r\nContent-Length: ", 37);
		pBuf += 37;
		iRequestSize += 37;
		iReqBuf -= 37;		

		int iBodyLen = 0;

		// �ٵ� ������ �ִ� ����� ������ ǥ���� ������ ���̸� ������ ǥ��
		if (Body != nullptr)
		{
			// Content-Length ���ؼ� ����
			iLength = (int)strlen(Body);
			iBodyLen = iLength;

			// ������ ���ڿ��� �ٲپ ���ۿ� �ְ� ������ �ű��
			int nTemp = iLength;
			int iCount = 0;

			// ������ �ڸ��� ���Ѵ�.
			while (nTemp != 0)
			{
				nTemp /= 10;
				iCount++;
			}

			iRequestSize += iCount;

			// �ڸ��� ��ŭ ���鼭 ������ ���ڿ��� �ٲپ� ���ۿ� �ֱ�
			for (int i = iCount - 1; i >= 0; i--)
			{
				*(pBuf + i) = (iLength % 10) + '0';
				iLength /= 10;
			}

			// ������ \r\n �ι� �����ֱ�
			pBuf += iCount;
		}
		else
		{
			*pBuf = '0';
			++pBuf;
			iRequestSize++;
		}

		char pHeaderEnd[5] = "\r\n\r\n";
		*((int*)pBuf) = *((int*)pHeaderEnd);

		pBuf += 4;
		iRequestSize += 4;

		if (Body != nullptr)
		{
			// ������ �ٵ� �����ֱ�
			// �ٵ� �ʹ� ũ�ٸ� memcpy �Ұ�
			if (iBodyLen > iReqBuf)
				return SOCKET_ERROR;
			memcpy_s(pBuf, iReqBuf, Body, iBodyLen);
			iRequestSize += iBodyLen;
		}

		// ��û ������.
		int iResult = send(m_Socket, Request, iRequestSize, 0);

		// ���Ͽ����� ���ٸ� ���´�.
		if (iResult == SOCKET_ERROR)
		{			
			closesocket(m_Socket);			
			return SOCKET_ERROR;
		}

		// Send�� �����ߴٸ� ������Ʈ ���� ������ ��ȯ���ش�.
		return iRequestSize;
	}

	int CHttpPost::RecvHttpResponse()
	{	
		char *Response = m_UTF8Response;
		Response[8] = 'A'; // ����ڵ� �� ������ �����ϱ� ���� �ʱⰪ���� 'A'�� �־��
		Response[9] = 'A'; // ���������� ����ڵ� ù��° �ڸ��� '2'���� ���� ���� 'A'�� �־��	

		int iReceived = 0;		

		// HTTP ���� �ޱ� 
		while (true)
		{
			// iRecvLimitation ����Ʈ������ recv�ϰ� �� �̻��� ���� �ʴ´�.
			if (iReceived >= MAX_RESPONSE_LEN)
			{
				closesocket(m_Socket);
				break;
			}		
			
			int iResult = recv(m_Socket, Response, MAX_RESPONSE_LEN, 0);

			// �����̰ų� ����������� ���� ������� �ش� ���� ��ȯ
			if (iResult == SOCKET_ERROR || iResult == 0)
			{				
				closesocket(m_Socket);
				return SOCKET_ERROR;
			}

			iReceived += iResult;

			// �ּ� ��� ���̸�ŭ �Դ��� Ȯ�� 
			if (iReceived < HEADER_LENGTH)
				continue;

			// ���� �ڵ� ã��
			if (Response[8] == 0x20)
			{
				if (Response[9] != 'A')
				{
					char ResCode[4];
					ResCode[3] = '\0';
					int  iResCode;		

					// �ؽ�Ʈ�� �� �����ڵ� ���
					ResCode[0] = Response[9];
					ResCode[1] = Response[10];
					ResCode[2] = Response[11];					

					// �����ڵ� ������ ��ȯ
					iResCode = atoi(ResCode);

					// �����ڵ尡 200�̶�� ������ ���� ����
					// 200�̿��� ��� �͵� ���� ����. 
					if (iResCode == 200)
					{
						// ���ڿ� �ٵ��� ũ�⸦ ������ ���ؼ� "Content-Length: "�� ���ڿ��� �˻�
						char *ptr = strstr(Response + 12, "Content-Length: ");

						if (ptr == nullptr)
							continue;						

						// ���� �ٵ� ���̰� �ִ� ������ �̵�							
						ptr += 16;
						char *pLength = ptr;

						// '\r'�� ã�� ������ �̵�, '\r'�� ������ �� �� �κ��� ������ ��
						ptr = strchr(ptr, '\r');

						if (ptr == nullptr)
							continue;
						
						// \r�� ã������ ������ ���� ��ġ�� ������ �ξ���.
						int iContentLen = atoi(pLength);

						// �ٵ� ������ �׳� ����
						if (iContentLen == 0)
							return 0;

						// �ٵ� ������ ���� \r\n\r\n�� ã�Ƽ� �� ������ �ִ� �ٵ� �̾Ƴ���.						
						ptr = strstr(ptr, "\r\n\r\n");

						if (ptr == nullptr)
							continue;

						// \r\n\r\n�� ã������ �� ���ĺ��� ContentLen�� �ٵ��̴�.
						ptr += 4;

						// �ٵ� ���������� �� �����ߴ��� Ȯ�� 
						int HeaderLength = (int)(ptr - Response);
						if (HeaderLength + iContentLen < iReceived)
							continue;

						// �ٵ� ��´�, Response���� UTF-8������ �ٵ� ����.														
						memcpy_s(Response, iContentLen, ptr, iContentLen);
						Response[iContentLen] = 0;

						//// UTF-8 ������ �ٵ� UTF-16���� ��ȯ�Ѵ�.														
						//MultiByteToWideChar(CP_UTF8, MB_PRECOMPOSED, Response, iContentLen, m_UTF16Response, MAX_RESPONSE_LEN);
						//m_UTF16Response[iContentLen] = 0;
						
						// �ٵ� ���������� ���������� ������� 200�� ��ȯ�Ѵ�.
						closesocket(m_Socket);
						return iResCode;
												
					}
					else
					{						
						closesocket(m_Socket);
						return SOCKET_ERROR; // �����ڵ尡 200�� �ƴ϶�� ����
					}
						
				}
			}

		}	
		
		return SOCKET_ERROR;

	}

	char * CHttpPost::GetResponseUTF8() const
	{
		return (char*)m_UTF8Response;
	}

	TCHAR * CHttpPost::GetResponseUTF16() const
	{
		return (TCHAR*)m_UTF16Response;
	}

}