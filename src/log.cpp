#include "log.h"
#include <cstdio>
#include <cstdarg>

void LogMessage(const wchar_t* fmt, ...)
{
	static wchar_t logPath[MAX_PATH] = L"";
	if (logPath[0] == L'\0')
	{
		GetModuleFileNameW(nullptr, logPath, MAX_PATH);
		wchar_t* lastSlash = wcsrchr(logPath, L'\\');
		if (lastSlash) *(lastSlash + 1) = L'\0';
		wcscat_s(logPath, L"MusicPlayer.log");
	}

	wchar_t buf[1024];
	va_list args;
	va_start(args, fmt);
	vswprintf_s(buf, fmt, args);
	va_end(args);

	FILE* f = nullptr;
	if (_wfopen_s(&f, logPath, L"a, ccs=UTF-8") == 0 && f)
	{
		fwprintf(f, L"%s\n", buf);
		fclose(f);
	}
}
