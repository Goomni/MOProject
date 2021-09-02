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
	
	if (pTextFile == nullptr) // ���� ���� ���н� false ��ȯ	
	{
		_fputts(_T("FILE OPEN FAILED!!\n"), stdout);
		return false;
	}	

	fseek(pTextFile, 0, SEEK_END);// ���� ������ ���ϱ�
	dwFileSize = ftell(pTextFile);
	fseek(pTextFile, 2, SEEK_SET); // UTF16LE BOM (0xFEFF) �ǳʶٱ� ���� ������ +2���� ���� �б�

	pBuf = new TCHAR[dwFileSize/sizeof(TCHAR)]; // ���� ���� �Ҵ�
	
	if (pBuf == nullptr)
	{
		_fputts(_T("MEMORY ALLOCATION FAILED!!\n"), stdout);
		return false;
	}

	memset(pBuf, '\0', dwFileSize); // ���� �ϱ��� �ι��ڷ� �о��ֱ�

	fread_s(pBuf, dwFileSize, dwFileSize, 1, pTextFile);	

	pStrSearch = pBuf; // ���ڿ� �˻��� ���� �����͸� �������ش�.	
	pStrOut = pStrSearch;

	fclose(pTextFile);
	
	return true; // ������ ���簡 �Ϸ�Ǿ����� Ʈ�� ��ȯ
}
bool CParser::SetSpace(const TCHAR * SpaceName)
{	
	pStrSearch = pBuf;	

	if (bIsSpaceSet) // �̹� �ٸ� ������ �����Ǿ� �ִٸ�, ���� ���� ����
		bIsSpaceSet = false;

	while(GetNextWord())
	{
		if (_tcscmp(SpaceName, szWordBuf) == 0) // ���ۿ� ��� ���� �����̸��� ���ڷ� ���޵� �����̸��� �´��� Ȯ�� 
		{
			while (*(pStrSearch) != '{') // �����͸� �������� ù��ġ�� �̵�
				pStrSearch++;	

			pSpaceEntry = pStrSearch; // �������� ù ��ġ�� ���
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
 		&& *(pStrSearch) != _T('\b')) // ���� �����ų�, (�����̽�, ����, ��)�� �ƴϸ� FALSE
 		return false;
 
 	while( *(pStrSearch) == _T(' ')  // �����̽�, ����, ���� ������ �����͸� 1����Ʈ�� �ű��.
 		|| *(pStrSearch) == _T('\r')
 		|| *(pStrSearch) == _T('\n')
 		|| *(pStrSearch) == _T('\t')
 		|| *(pStrSearch) == _T('\b'))
 	{
 		pStrSearch++;
 	}
 
 	pStrOut = pStrSearch; // ���ʿ��� �κ��� �� ��ŵ�� �� ���� �������� ��ġ�� OUT�� �����Ϳ� �������ش�. 	
 
 	return true;
 }// ȭ��Ʈ �����̽� �ѱ��
 //
 bool CParser::SkipComment()
 {
 	if (*(pStrSearch) == _T('\0'))
 		return false;
 	else if (*(pStrSearch) == _T('/'))
 		bInComment = true; 
 	else if (bInComment == false)
 		return false;
 			
 	if (*(pStrSearch) == _T('/') && *(pStrSearch+1) == _T('*')) // �� �ּ��� ���
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
 	
 	while(*(pStrSearch) !='\n' || *(pStrSearch+1) == '/') // �� ���� �ּ��ϰ��
 		pStrSearch++;	
 
 	pStrOut = pStrSearch;	
 
 	bInComment = false;
 
 	return true;
 
 } // �ּ��ѱ��

 bool CParser::GetNextWord() 
 {
	 if (*(pStrSearch) == _T('\0'))
		 return false;
	 else if (bIsSpaceSet && *(pStrOut) == _T('}')) // ���� ���� �� �˻� �� �������� ���� �����ϸ� �˻� ����
		 return false;
 		
 	SkipWhiteSpace(); 	
	SkipComment();
 
 	iLength = 0; 
 
 	while(true) // ���ڿ��� ���� ���������� �ܾ��� ���̸� ����ϴ� �ݺ���
 	{
 		if (*(pStrSearch) == _T(' ')  // ������ ���ڵ��� �����ϸ� �ܾ��� ������ �����Ѵ�.
 			|| *(pStrSearch) == _T('\n') 			
 			|| *(pStrSearch) == _T('\b')
 			|| *(pStrSearch) == _T('\t')
 			|| *(pStrSearch) == _T('\r')
 			|| *(pStrSearch) == _T('\0'))
 		{
 			break;
 		} 

		iLength++; // �ش��ϴ� �ܾ��� ���̰��
		pStrSearch++;
 	}
 
 	memset(szWordBuf, 0, WORD_BUF);
 
 	for (int i = 0; i < iLength; i++)
 		szWordBuf[i] = *(pStrOut+i);

 	return true;
 }// ���� ���ڷ� ������ �ű�� 
 
 bool CParser::GetNextString()
{
 	if (*(pStrSearch) == _T('\0') || pStrSearch == nullptr)
 		return false;
	else if (bIsSpaceSet && *(pStrOut) == _T('}')) // ���� ���� �� �������� ���� �����ϸ� �˻� ����
		return false;
 
 	SkipWhiteSpace();
 	SkipComment();
 
 	iLength = 0;
 
 	if(*(pStrSearch) == _T('\"')) // ���ڿ��� ���� ū ����ǥ�� �ǳʶٰ� ���ĺ��� ����
 	{
 		pStrSearch++;
 		pStrOut = pStrSearch;
 	}
 
 	while (true) // ���ڿ��� ���� ���������� �ܾ��� ���̸� ����ϴ� �ݺ���
 	{
		//*(pStrSearch) == _T(' ')||
 		if (*(pStrSearch) == _T('\"'))
 		{
 			break;
 		}
 
 		iLength++; // �ش��ϴ� �ܾ��� ���̰��
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
		 pStrSearch = pSpaceEntry; // ���� ���� �Ǿ� ������ ���� ������ ó������
		 pStrOut = pStrSearch;
		 return true;
	 }
	 else
	 {
		 pStrSearch = pBuf; // ���� ���� �ȵǾ� ������ ���� �� ó������
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

				 PointerReset(); // �ش��ϴ� ���� ã������ �ƿ������� �����ϰ�, ������ ����

				 return true;
			 }
		 }
	 }

	 PointerReset(); // �ش��ϴ� ���� ��ã�Ҿ ���� �˻��� ����, ������ ����

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

				 PointerReset(); // �ش��ϴ� ���� ã������ �ƿ������� �����ϰ�, ������ ����

				 return true;
			 }
		 }
	 }

	 PointerReset(); // �ش��ϴ� ���� ��ã�Ҿ ���� �˻��� ����, ������ ����

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

				 PointerReset(); // �ش��ϴ� ���� ã������ �ƿ������� �����ϰ�, ������ ����

				 return true;
			 }
		 }
	 }

	 PointerReset(); // �ش��ϴ� ���� ��ã�Ҿ ���� �˻��� ����, ������ ����

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

				PointerReset(); // �ش��ϴ� ���� ã������ �ƿ������� �����ϰ�, ������ ����

				return true;				
 			}
 		}
 	}

	PointerReset(); // �ش��ϴ� ���� ��ã�Ҿ ���� �˻��� ����, ������ ����
 
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

				 PointerReset(); // �ش��ϴ� ���� ã������ �ƿ������� �����ϰ�, ������ ����

				 return true;
			 }
		 }
	 }

	 PointerReset(); // �ش��ϴ� ���� ��ã�Ҿ ���� �˻��� ����, ������ ����

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
 
				PointerReset(); // �ش��ϴ� ���� ã������ �ƿ������� �����ϰ�, ������ ����

				return true;
 			}
 		}
 	}

	PointerReset(); // �ش��ϴ� ���� ��ã�Ҿ ���� �˻��� ����, ������ ����
 
 	return false;
 }
 
 CParser::~CParser()
 {
	 if (pBuf != nullptr)
		 delete[]pBuf;	 
 }
 
 
 }