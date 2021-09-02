#include "HTTP_POST.h"
#include <stdlib.h>
#include "Logger\Logger.h"
#include "CrashDump\CrashDump.h"

namespace GGM
{
	CHttpPost::CHttpPost(const TCHAR *RemoteHost, HOST addr)
	{
		//////////////////////////////////////////////////////////////////////////////////////////
		// 만약 유저가 IP 주소가 아닌 도메인 이름을 입력했다면 connect 하기 위해서 주소정보를 얻어와서
		// 주소구조체에 넣어주어야 한다.
		// 호스트 이름을 통해 주소정보를 얻어오기 위한 로직
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

			// 주소 정보 얻어오는 함수 호출
			iResult = GetAddrInfoW(RemoteHost, _T("http"), &hints, &result);

			if (iResult == 0)
			{
				// 주소 정보 성공적으로 얻어왔다면 멤버 주소 구조체에 저장한다.
				SOCKADDR_IN *pAddr = (SOCKADDR_IN*)result->ai_addr;
				m_RemoteAddr = *pAddr;

				// 해당 주소를 문자열로 변환하고
				InetNtop(AF_INET, &(pAddr->sin_addr), szAddr, 16);

				// ANSI 문자열로 변환해서 멤버에 저장한다. 이 문자열은 나중에 HTTP 쏠 때 헤더에 포함될 것이다.
				
				(CP_ACP, 0, szAddr, 16, m_RemoteIP, 16, NULL, NULL);
			}
		}
		else
		{
			// 생성자의 인자로 IP의 주소만 입력받았을 경우에는 따로 위의 과정을 거칠 필요가 없다.
			// ANSI 문자열로 변환해서 멤버에 저장한다. 이 문자열은 나중에 HTTP 쏠 때 헤더에 포함될 것이다.
			WideCharToMultiByte(CP_ACP, 0, RemoteHost, -1, m_RemoteIP, 16, NULL, NULL);
			m_RemoteAddr.sin_family = AF_INET;
			m_RemoteAddr.sin_port = htons(WEB_SERVER_PORT);
			InetPton(AF_INET, RemoteHost, &m_RemoteAddr.sin_addr);
		}
	}

	int CHttpPost::ConnectToWebServer(SOCKADDR_IN *pAddr)
	{
		// 소켓 생성
		SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

		if (sock == INVALID_SOCKET)
			return SOCKET_ERROR;

		// connect 할 때 논블락 소켓을 이용한다.
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

		// send 와 recv를 블락소켓으로 진행할 것이기 때문에 서버가 응답이 없을 때를 대비해 타임아웃을 준다.
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
			// 웹서버에 연결 실패시 5번까지 시도한다. 
			ConnectTry++;

			// Connect를 논블락으로 처리하고 그 결과를 확인하기 위해서 select를 활용한다.			
			// Connect 두번 호출하는 이유
			// 만약에 두번째 connect 호출 했을 때 Error 값이 WSAEISCONN 떠 주면 Select 안해도 되기 때문이다.		
			iResult = connect(sock, (SOCKADDR*)pAddr, sizeof(SOCKADDR_IN));
			connect(sock, (SOCKADDR*)pAddr, sizeof(SOCKADDR_IN));

			// 논블락 소켓일 경우 connect를 호출하면 나올 수 있는 에러의 경우의 수는 다음과 같다.
			// 1. 최초 호출시 바로 WSAEWOULDBLOCK 
			// 2. 아직 연결이 완료되지 않았는데 다시 connect 호출하면 WSAEALREADY
			// 3. 이미 연결이 완료된 소켓에 대해서 다시 connect 호출하면 WSAEISCONN
			// 4. 그 이외의 경우는 WSAECONNREFUSED, WSAENETUNREACH, WSAETIMEDOUT 
			DWORD dwError = WSAGetLastError();

			// 연결 성공 
			if (dwError == WSAEISCONN)
				break;

			if (dwError == WSAEWOULDBLOCK || dwError == WSAEALREADY)
			{
				// 쓰기셋에 소켓 넣는다.
				FD_SET WriteSet;
				WriteSet.fd_count = 0;
				WriteSet.fd_array[0] = sock;
				WriteSet.fd_count++;

				// 예외셋에 소켓 넣는다.
				FD_SET ExceptionSet;
				ExceptionSet.fd_count = 0;
				ExceptionSet.fd_array[0] = sock;
				ExceptionSet.fd_count++;
				timeval TimeVal = { 0, 500000 };

				// select로 timeval 값만큼만 연결을 기다린다.
				iResult = select(0, nullptr, &WriteSet, &ExceptionSet, &TimeVal);

				if ((iResult == 0 || ExceptionSet.fd_count == 1) && ConnectTry >= 5)
				{
					// 이 경우엔 connect 요청 후 500ms 지나서 select가 타임아웃이 되었거나 연결이 실패해서 예외셋에 소켓이 들어있는 것이다.
					// 5번까지는 재시도한다. 이후에는 연결 실패로 간주 
					closesocket(sock);
					sock = INVALID_SOCKET;
					return SOCKET_ERROR;
				}
				else if (WriteSet.fd_count == 1)
				{
					// 연결 성공 
					break;
				}
			}

		}

		// 논블락 소켓으로 바뀐 소켓을 다시 블락 소켓으로 바꾸어 준다		
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
		// HTTP를 전송하고자 하는 Path, Body를 인자로 전달받아 웹서버로 포스트한다.
		// 일단 무조건 POST만 할 것이다.			
		// PATH 정보를 복사한다. 	
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

		// 이 부분은 똑같은 포맷일 것이므로 하드코딩 		
		memcpy_s(pBuf, iReqBuf, " HTTP/1.1\r\nUser-Agent: GUMIN\r\nHost: ", 36);
		pBuf += 36;
		iRequestSize += 36;
		iReqBuf -= 36;

		// HOST IP 복사해줌
		iLength = (int)strlen(RemoteIP);
		memcpy_s(pBuf, iReqBuf, RemoteIP, iLength);
		pBuf += iLength;
		iRequestSize += iLength;
		iReqBuf -= iLength;

		// Content-Length의 수치전까지는 모두 똑같을 것이므로 이 부분도 하드코딩 		
		memcpy_s(pBuf, iReqBuf, "\r\nConnection: Close\r\nContent-Length: ", 37);
		pBuf += 37;
		iRequestSize += 37;
		iReqBuf -= 37;		

		int iBodyLen = 0;

		// 바디에 내용이 있는 경우라면 정수로 표현된 컨텐츠 길이를 정수로 표현
		if (Body != nullptr)
		{
			// Content-Length 구해서 복사
			iLength = (int)strlen(Body);
			iBodyLen = iLength;

			// 정수를 문자열로 바꾸어서 버퍼에 넣고 포인터 옮기기
			int nTemp = iLength;
			int iCount = 0;

			// 정수의 자릿수 구한다.
			while (nTemp != 0)
			{
				nTemp /= 10;
				iCount++;
			}

			iRequestSize += iCount;

			// 자릿수 만큼 돌면서 정수를 문자열로 바꾸어 버퍼에 넣기
			for (int i = iCount - 1; i >= 0; i--)
			{
				*(pBuf + i) = (iLength % 10) + '0';
				iLength /= 10;
			}

			// 마지막 \r\n 두번 때려넣기
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
			// 마지막 바디 때려넣기
			// 바디가 너무 크다면 memcpy 불가
			if (iBodyLen > iReqBuf)
				return SOCKET_ERROR;
			memcpy_s(pBuf, iReqBuf, Body, iBodyLen);
			iRequestSize += iBodyLen;
		}

		// 요청 보낸다.
		int iResult = send(m_Socket, Request, iRequestSize, 0);

		// 소켓에러가 났다면 끊는다.
		if (iResult == SOCKET_ERROR)
		{			
			closesocket(m_Socket);			
			return SOCKET_ERROR;
		}

		// Send가 성공했다면 리퀘스트 보낸 사이즈 반환해준다.
		return iRequestSize;
	}

	int CHttpPost::RecvHttpResponse()
	{	
		char *Response = m_UTF8Response;
		Response[8] = 'A'; // 결과코드 앞 공백을 구분하기 위해 초기값으로 'A'를 넣어둠
		Response[9] = 'A'; // 마찬가지로 결과코드 첫번째 자리가 '2'인지 보기 위해 'A'를 넣어둠	

		int iReceived = 0;		

		// HTTP 응답 받기 
		while (true)
		{
			// iRecvLimitation 바이트까지만 recv하고 그 이상은 받지 않는다.
			if (iReceived >= MAX_RESPONSE_LEN)
			{
				closesocket(m_Socket);
				break;
			}		
			
			int iResult = recv(m_Socket, Response, MAX_RESPONSE_LEN, 0);

			// 에러이거나 연결끊겼으면 나도 연결끊고 해당 내용 반환
			if (iResult == SOCKET_ERROR || iResult == 0)
			{				
				closesocket(m_Socket);
				return SOCKET_ERROR;
			}

			iReceived += iResult;

			// 최소 헤더 길이만큼 왔는지 확인 
			if (iReceived < HEADER_LENGTH)
				continue;

			// 응답 코드 찾기
			if (Response[8] == 0x20)
			{
				if (Response[9] != 'A')
				{
					char ResCode[4];
					ResCode[3] = '\0';
					int  iResCode;		

					// 텍스트로 된 응답코드 얻기
					ResCode[0] = Response[9];
					ResCode[1] = Response[10];
					ResCode[2] = Response[11];					

					// 응답코드 정수로 변환
					iResCode = atoi(ResCode);

					// 응답코드가 200이라면 이하의 로직 진행
					// 200이외의 어느 것도 쓸모가 없다. 
					if (iResCode == 200)
					{
						// 문자열 바디의 크기를 얻어오기 위해서 "Content-Length: "를 문자열로 검색
						char *ptr = strstr(Response + 12, "Content-Length: ");

						if (ptr == nullptr)
							continue;						

						// 실제 바디 길이가 있는 곳으로 이동							
						ptr += 16;
						char *pLength = ptr;

						// '\r'을 찾을 때까지 이동, '\r'이 나오면 그 앞 부분이 숫자의 끝
						ptr = strchr(ptr, '\r');

						if (ptr == nullptr)
							continue;
						
						// \r을 찾기전에 숫자의 시작 위치를 저장해 두었다.
						int iContentLen = atoi(pLength);

						// 바디가 없으면 그냥 리턴
						if (iContentLen == 0)
							return 0;

						// 바디가 있으면 이제 \r\n\r\n을 찾아서 그 다음에 있는 바디를 뽑아낸다.						
						ptr = strstr(ptr, "\r\n\r\n");

						if (ptr == nullptr)
							continue;

						// \r\n\r\n을 찾았으면 그 이후부터 ContentLen이 바디이다.
						ptr += 4;

						// 바디가 정상적으로 다 도착했는지 확인 
						int HeaderLength = (int)(ptr - Response);
						if (HeaderLength + iContentLen < iReceived)
							continue;

						// 바디를 얻는다, Response에는 UTF-8형태의 바디가 담긴다.														
						memcpy_s(Response, iContentLen, ptr, iContentLen);
						Response[iContentLen] = 0;

						//// UTF-8 형태의 바디를 UTF-16으로 변환한다.														
						//MultiByteToWideChar(CP_UTF8, MB_PRECOMPOSED, Response, iContentLen, m_UTF16Response, MAX_RESPONSE_LEN);
						//m_UTF16Response[iContentLen] = 0;
						
						// 바디를 성공적으로 복사했으면 연결끊고 200을 반환한다.
						closesocket(m_Socket);
						return iResCode;
												
					}
					else
					{						
						closesocket(m_Socket);
						return SOCKET_ERROR; // 응답코드가 200이 아니라면 에러
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