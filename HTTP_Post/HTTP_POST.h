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
		// ó�� ����� ���ڷ� ���޵Ǵ� ���� IP �ּ�����, ȣ��Ʈ�������� �����ϴ� ������
		IP_ADDR = 0,
		DOMAIN_NAME = 1
	};

	class CHttpPost
	{	
	public:

		SOCKET      m_Socket = INVALID_SOCKET;
		char		m_Request[MAX_REQUEST_LEN] = "POST http://"; // �ϴ� ������ POST�� �� ���̴�.			
		char        m_UTF8Response[MAX_RESPONSE_LEN];
		TCHAR       m_UTF16Response[MAX_RESPONSE_LEN];
		SOCKADDR_IN m_RemoteAddr; // ���� �ּ� ���� �ּ� ����ü
		char		m_RemoteIP[IP_V4_LEN]; // HTTP ����� ������ UTF-8 ���� ���� IP �ּ�				

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