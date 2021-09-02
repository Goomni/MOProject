#ifndef TEXT_PARSER_H
#define TEXT_PARSER_H
#define WORD_BUF 4096

#include <Windows.h>
#include <tchar.h>

namespace GGM
{

	class CParser
	{
	private:
	
		TCHAR*	pBuf; // 파싱할 텍스트 파일의 크기를 알지 못하므로, 추후에 파일의 크기를 계산하여 버퍼를 동적할당
		TCHAR*	pStrSearch; // 버퍼를 돌아다니며 문자 각각을 체크하기 위해 사용하는 포인터
		TCHAR*	pStrOut; // 가리키는 단어를 실제로 저장할 때 쓰는 포인터	
		TCHAR*  pSpaceEntry; // 구역의 시작 스코프를 기억하기 위해 사용
		TCHAR	szWordBuf[WORD_BUF]; // 찾은 문자열을 담아서 목표 문자열과 비교할 때 사용
		int		iLength; // 각 단어의 길이	
		bool	bInComment = false;	
		bool	bIsSpaceSet = false;
	
		bool	SkipWhiteSpace(); // 화이트 스페이스 넘기기
		bool	SkipComment(); // 주석 넘기기
		bool	GetNextWord(); // 다음 단어 ( 숫자 , 문자 ) 얻기
		bool	GetNextString(); // 다음 문자열을 얻기
		bool	PointerReset(); // 검색 포인터 초기화(매 검색시마다 초기화가 필요하다.)
	
	public:
	
		       CParser();
		bool   LoadFile(const TCHAR *FileName); // 파일 이름을 인자로 전달받아 버퍼에 내용 저장
		bool   SetSpace(const TCHAR *SpaceName); // 구역 이름을 인자로 전달받아 문자열을 검색할 구역 설정	
	
		bool   GetValue(const TCHAR *DataName, bool *iValue);
		bool   GetValue(const TCHAR *DataName, char *iValue);
		bool   GetValue(const TCHAR *DataName, short *iValue);
		bool   GetValue(const TCHAR *DataName, int *iValue);
		bool   GetValue(const TCHAR *DataName, double *dValue);			
		bool   GetValue(const TCHAR *DataName, TCHAR szValue[]);		
		       ~CParser();	
	};

}




#endif // !TEXT_PARSER_H
