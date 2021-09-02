#pragma once
#include <tchar.h>
#include "Parser\Parser.h"

namespace GGM
{
	// 각종 컨피그 파일을 로드할 때 사용하는 베이스 클래스
	class CConfigLoader
	{
	public:

		CConfigLoader() = default;
		virtual ~CConfigLoader() = default;

		virtual bool LoadConfig(const TCHAR* ConfigFileName) = 0;		
	};
}