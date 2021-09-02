#include <cstdlib>
#include <cstdio>
#include <tchar.h>
#include <Windows.h>
#include "Parser.h"

using namespace std;

namespace GGM 
{

CParser::CParser()
	: pBuf(nullptr), pStrSearch(nullptr), pStrOut(nullptr), pSpaceEntry(nullptr)
{
	memset(szWordBuf, '\0', WORD_BUF);
}

bool CParser::LoadFile(const TCHAR *FileName)
{
	FILE*		pTextFile;
	DWORD	    dwFileSize;	

	_tfopen_s(&pTextFile, FileName, _T("rt, ccs=UNICODE"));
	
	if (pTextFile == nullptr) // 파일 오픈 실패시 false 반환	
	{
		_fputts(_T("FILE OPEN FAILED!!\n"), stdout);
		return false;
	}	

	fseek(pTextFile, 0, SEEK_END);// 파일 사이즈 구하기
	dwFileSize = ftell(pTextFile);
	fseek(pTextFile, 2, SEEK_SET); // UTF16LE BOM (0xFEFF) 건너뛰기 위해 시작점 +2부터 파일 읽기

	pBuf = new TCHAR[dwFileSize/sizeof(TCHAR)]; // 버퍼 동적 할당
	
	if (pBuf == nullptr)
	{
		_fputts(_T("MEMORY ALLOCATION FAILED!!\n"), stdout);
		return false;
	}

	memset(pBuf, '\0', dwFileSize); // 복사 하기전 널문자로 밀어주기

	fread_s(pBuf, dwFileSize, dwFileSize, 1, pTextFile);	

	pStrSearch = pBuf; // 문자열 검색을 위한 포인터를 셋팅해준다.	
	pStrOut = pStrSearch;

	fclose(pTextFile);
	
	return true; // 파일의 복사가 완료되었으면 트루 반환
}
bool CParser::SetSpace(const TCHAR * SpaceName)
{	
	pStrSearch = pBuf;	

	if (bIsSpaceSet) // 이미 다른 구역이 설정되어 있다면, 구역 설정 해제
		bIsSpaceSet = false;

	while(GetNextWord())
	{
		if (_tcscmp(SpaceName, szWordBuf) == 0) // 버퍼에 담긴 현재 구역이름이 인자로 전달된 구역이름이 맞는지 확인 
		{
			while (*(pStrSearch) != '{') // 포인터를 스코프의 첫위치로 이동
				pStrSearch++;	

			pSpaceEntry = pStrSearch; // 스코프의 첫 위치를 기억
			pStrOut = pStrSearch;

			bIsSpaceSet = true;			

			return true;
		}
	}

	return false;
}
 
 bool CParser::SkipWhiteSpace()
 {
 	if (   *(pStrSearch) == _T('\0')
 		|| *(pStrSearch) != _T(' ')
 		&& *(pStrSearch) != _T('\r')
 		&& *(pStrSearch) != _T('\n')
 		&& *(pStrSearch) != _T('\t')
 		&& *(pStrSearch) != _T('\b')) // 널을 만나거나, (스페이스, 개행, 탭)이 아니면 FALSE
 		return false;
 
 	while( *(pStrSearch) == _T(' ')  // 스페이스, 개행, 탭을 만나면 포인터를 1바이트씩 옮긴다.
 		|| *(pStrSearch) == _T('\r')
 		|| *(pStrSearch) == _T('\n')
 		|| *(pStrSearch) == _T('\t')
 		|| *(pStrSearch) == _T('\b'))
 	{
 		pStrSearch++;
 	}
 
 	pStrOut = pStrSearch; // 불필요한 부분을 다 스킵한 후 현재 포인터의 위치를 OUT용 포인터에 전달해준다. 	
 
 	return true;
 }// 화이트 스페이스 넘기기
 //
 bool CParser::SkipComment()
 {
 	if (*(pStrSearch) == _T('\0'))
 		return false;
 	else if (*(pStrSearch) == _T('/'))
 		bInComment = true; 
 	else if (bInComment == false)
 		return false;
 			
 	if (*(pStrSearch) == _T('/') && *(pStrSearch+1) == _T('*')) // 블럭 주석일 경우
 	{
 		while (true)
 		{
 			pStrSearch++;
 			if (*(pStrSearch) == '*' && *(pStrSearch+1) == '/')
			{
				pStrSearch+=2;
 				break;
			}
 		}
 	}
 	
 	while(*(pStrSearch) !='\n' || *(pStrSearch+1) == '/') // 한 라인 주석일경우
 		pStrSearch++;	
 
 	pStrOut = pStrSearch;	
 
 	bInComment = false;
 
 	return true;
 
 } // 주석넘기기

 bool CParser::GetNextWord() 
 {
	 if (*(pStrSearch) == _T('\0'))
		 return false;
	 else if (bIsSpaceSet && *(pStrOut) == _T('}')) // 구역 설정 후 검색 시 스코프의 끝에 도달하면 검색 종료
		 return false;
 		
 	SkipWhiteSpace(); 	
	SkipComment();
 
 	iLength = 0; 
 
 	while(true) // 문자열의 끝을 만날때까지 단어의 길이를 계산하는 반복문
 	{
 		if (*(pStrSearch) == _T(' ')  // 이하의 문자들이 등장하면 단어의 끝으로 간주한다.
 			|| *(pStrSearch) == _T('\n') 			
 			|| *(pStrSearch) == _T('\b')
 			|| *(pStrSearch) == _T('\t')
 			|| *(pStrSearch) == _T('\r')
 			|| *(pStrSearch) == _T('\0'))
 		{
 			break;
 		} 

		iLength++; // 해당하는 단어의 길이계산
		pStrSearch++;
 	}
 
 	memset(szWordBuf, 0, WORD_BUF);
 
 	for (int i = 0; i < iLength; i++)
 		szWordBuf[i] = *(pStrOut+i);

 	return true;
 }// 다음 문자로 포인터 옮기기 
 
 bool CParser::GetNextString()
{
 	if (*(pStrSearch) == _T('\0') || pStrSearch == nullptr)
 		return false;
	else if (bIsSpaceSet && *(pStrOut) == _T('}')) // 구역 설정 시 스코프의 끝에 도달하면 검색 종료
		return false;
 
 	SkipWhiteSpace();
 	SkipComment();
 
 	iLength = 0;
 
 	if(*(pStrSearch) == _T('\"')) // 문자열의 시작 큰 따옴표는 건너뛰고 이후부터 시작
 	{
 		pStrSearch++;
 		pStrOut = pStrSearch;
 	}
 
 	while (true) // 문자열의 끝을 만날때까지 단어의 길이를 계산하는 반복문
 	{
		//*(pStrSearch) == _T(' ')||
 		if (*(pStrSearch) == _T('\"'))
 		{
 			break;
 		}
 
 		iLength++; // 해당하는 단어의 길이계산
 		pStrSearch++;
 	}
 
 	memset(szWordBuf, 0, WORD_BUF);
 
 	for (int i = 0; i < iLength; i++)
 		szWordBuf[i] = *(pStrOut + i);
 
 	return true;
}
 bool CParser::PointerReset()
 {
	 if (bIsSpaceSet) 
	 {
		 pStrSearch = pSpaceEntry; // 구역 설정 되어 있으면 구역 스코프 처음으로
		 pStrOut = pStrSearch;
		 return true;
	 }
	 else
	 {
		 pStrSearch = pBuf; // 구역 설정 안되어 있으면 버퍼 맨 처음으로
		 pStrOut = pStrSearch;
		 return true;
	 }

	 return false;
 } 

 bool CParser::GetValue(const TCHAR * DataName, bool * iValue)
 {
	 while (GetNextWord())
	 {
		 if (_tcscmp(szWordBuf, DataName) == 0)
		 {
			 if (GetNextWord() && _tcscmp(szWordBuf, _T("=")) == 0)
			 {
				 if (GetNextWord())
					 *iValue = (bool)_ttoi(szWordBuf);

				 PointerReset(); // 해당하는 값을 찾았으면 아웃변수에 저장하고, 포인터 리셋

				 return true;
			 }
		 }
	 }

	 PointerReset(); // 해당하는 값을 못찾았어도 다음 검색을 위해, 포인터 리셋

	 return false;
 }

 bool CParser::GetValue(const TCHAR * DataName, char * iValue)
 {
	 while (GetNextWord())
	 {
		 if (_tcscmp(szWordBuf, DataName) == 0)
		 {
			 if (GetNextWord() && _tcscmp(szWordBuf, _T("=")) == 0)
			 {
				 if (GetNextWord())
					 *iValue = (char)_ttoi(szWordBuf);

				 PointerReset(); // 해당하는 값을 찾았으면 아웃변수에 저장하고, 포인터 리셋

				 return true;
			 }
		 }
	 }

	 PointerReset(); // 해당하는 값을 못찾았어도 다음 검색을 위해, 포인터 리셋

	 return false;
 }

 bool CParser::GetValue(const TCHAR * DataName, short * iValue)
 {
	 while (GetNextWord())
	 {
		 if (_tcscmp(szWordBuf, DataName) == 0)
		 {
			 if (GetNextWord() && _tcscmp(szWordBuf, _T("=")) == 0)
			 {
				 if (GetNextWord())
					 *iValue = (short)_ttoi(szWordBuf);

				 PointerReset(); // 해당하는 값을 찾았으면 아웃변수에 저장하고, 포인터 리셋

				 return true;
			 }
		 }
	 }

	 PointerReset(); // 해당하는 값을 못찾았어도 다음 검색을 위해, 포인터 리셋

	 return false;
 }
 
 bool CParser::GetValue(const TCHAR *DataName, int *iValue)
 {
 	while (GetNextWord())
 	{	
 		if (_tcscmp(szWordBuf, DataName) == 0)
 		{
 			if (GetNextWord() && _tcscmp(szWordBuf, _T("=")) == 0)
 			{
 				if (GetNextWord())
 					*iValue = _ttoi(szWordBuf);

				PointerReset(); // 해당하는 값을 찾았으면 아웃변수에 저장하고, 포인터 리셋

				return true;				
 			}
 		}
 	}

	PointerReset(); // 해당하는 값을 못찾았어도 다음 검색을 위해, 포인터 리셋
 
 	return false;	
 }

 bool CParser::GetValue(const TCHAR * DataName, double * dValue)
 {
	 while (GetNextWord())
	 {
		 if (_tcscmp(szWordBuf, DataName) == 0)
		 {
			 if (GetNextWord() && _tcscmp(szWordBuf, _T("=")) == 0)
			 {
				 if (GetNextWord())
					 *dValue = _ttof(szWordBuf);

				 PointerReset(); // 해당하는 값을 찾았으면 아웃변수에 저장하고, 포인터 리셋

				 return true;
			 }
		 }
	 }

	 PointerReset(); // 해당하는 값을 못찾았어도 다음 검색을 위해, 포인터 리셋

	 return false;
 }
 
 bool CParser::GetValue(const TCHAR *DataName, TCHAR szValue[])
 { 
 	while (GetNextWord())
 	{ 
 		if (_tcscmp(szWordBuf, DataName) == 0)
 		{
 			if (GetNextWord() && _tcscmp(szWordBuf, _T("=")) == 0)
 			{
 				if (GetNextString())
				{					
					_tcscpy_s(szValue, iLength + 1, szWordBuf);
				}
 
				PointerReset(); // 해당하는 값을 찾았으면 아웃변수에 저장하고, 포인터 리셋

				return true;
 			}
 		}
 	}

	PointerReset(); // 해당하는 값을 못찾았어도 다음 검색을 위해, 포인터 리셋
 
 	return false;
 }
 
 CParser::~CParser()
 {
	 if (pBuf != nullptr)
		 delete[]pBuf;	 
 }
 
 
 }