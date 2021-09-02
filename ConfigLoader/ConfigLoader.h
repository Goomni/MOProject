#pragma once
#include <tchar.h>
#include "Parser\Parser.h"

namespace GGM
{
	// ���� ���Ǳ� ������ �ε��� �� ����ϴ� ���̽� Ŭ����
	class CConfigLoader
	{
	public:

		CConfigLoader() = default;
		virtual ~CConfigLoader() = default;

		virtual bool LoadConfig(const TCHAR* ConfigFileName) = 0;		
	};
}