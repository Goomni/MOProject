#ifndef HTTP_POST
#define HTTP_POST

#define  WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <ws2tcpip.h>
#include <winsock2.h>
#include <tchar.h>
#pragma comment(lib, "Ws2_32")

namespace GGM
{
	constexpr auto MAX_REQUEST_LEN = 2048;
	constexpr auto HEADER_LENGTH = 176;
	constexpr auto MAX_RESPONSE_LEN = 2048;
	constexpr auto WEB_SERVER_PORT = 80;
	constexpr auto IP_V4_LEN = 16;
	constexpr auto TRANSMIT_TIMEOUT = 5000;

	enum class HOST
	{
		// 처음 연결시 인자로 전달되는 것인 IP 주소인지, 호스트네임인지 결정하는 열거형
		IP_ADDR = 0,
		DOMAIN_NAME = 1
	};

	class CHttpPost
	{	
	public:

		SOCKET      m_Socket = INVALID_SOCKET;
		char		m_Request[MAX_REQUEST_LEN] = "POST http://"; // 일단 무조건 POST만 할 것이다.			
		char        m_UTF8Response[MAX_RESPONSE_LEN];
		TCHAR       m_UTF16Response[MAX_RESPONSE_LEN];
		SOCKADDR_IN m_RemoteAddr; // 서버 주소 담을 주소 구조체
		char		m_RemoteIP[IP_V4_LEN]; // HTTP 헤더에 포함할 UTF-8 형식 서버 IP 주소				

	public:

		         CHttpPost(const TCHAR *RemoteHost, HOST addr = HOST::DOMAIN_NAME);
				 CHttpPost() = default;
		virtual ~CHttpPost() = default;
		int      ConnectToWebServer(SOCKADDR_IN *pAddr);
		int      SendHttpRequest(char *RemoteIP, IN const char *Path, IN const char *Body = nullptr);
		int      RecvHttpResponse();
		char*    GetResponseUTF8() const;
		TCHAR*   GetResponseUTF16() const;

	};

}

#endif // !1