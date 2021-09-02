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
	
		TCHAR*	pBuf; // �Ľ��� �ؽ�Ʈ ������ ũ�⸦ ���� ���ϹǷ�, ���Ŀ� ������ ũ�⸦ ����Ͽ� ���۸� �����Ҵ�
		TCHAR*	pStrSearch; // ���۸� ���ƴٴϸ� ���� ������ üũ�ϱ� ���� ����ϴ� ������
		TCHAR*	pStrOut; // ����Ű�� �ܾ ������ ������ �� ���� ������	
		TCHAR*  pSpaceEntry; // ������ ���� �������� ����ϱ� ���� ���
		TCHAR	szWordBuf[WORD_BUF]; // ã�� ���ڿ��� ��Ƽ� ��ǥ ���ڿ��� ���� �� ���
		int		iLength; // �� �ܾ��� ����	
		bool	bInComment = false;	
		bool	bIsSpaceSet = false;
	
		bool	SkipWhiteSpace(); // ȭ��Ʈ �����̽� �ѱ��
		bool	SkipComment(); // �ּ� �ѱ��
		bool	GetNextWord(); // ���� �ܾ� ( ���� , ���� ) ���
		bool	GetNextString(); // ���� ���ڿ��� ���
		bool	PointerReset(); // �˻� ������ �ʱ�ȭ(�� �˻��ø��� �ʱ�ȭ�� �ʿ��ϴ�.)
	
	public:
	
		       CParser();
		bool   LoadFile(const TCHAR *FileName); // ���� �̸��� ���ڷ� ���޹޾� ���ۿ� ���� ����
		bool   SetSpace(const TCHAR *SpaceName); // ���� �̸��� ���ڷ� ���޹޾� ���ڿ��� �˻��� ���� ����	
	
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
