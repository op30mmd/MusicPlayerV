#include "config.h"
#include <shlobj.h>
#include <shlwapi.h>

#pragma comment(lib, "shlwapi.lib")

static const wchar_t* SUPPORTED_EXTENSIONS[] = {
	L".mp3", L".wav", L".flac", L".ogg", L".wma", L".aac", L".m4a"
};
static const int NUM_EXTENSIONS = 7;

bool LoadConfig(ModConfig* config, const wchar_t* iniPath)
{
	if (!config || !iniPath) return false;

	GetPrivateProfileStringW(L"MusicPlayer", L"MusicDirectory", L"", config->musicDirectory, MAX_PATH, iniPath);
	config->volume = GetPrivateProfileIntW(L"MusicPlayer", L"Volume", 50, iniPath);
	config->shuffle = GetPrivateProfileIntW(L"MusicPlayer", L"Shuffle", 0, iniPath) != 0;
	config->repeat = GetPrivateProfileIntW(L"MusicPlayer", L"Repeat", 1, iniPath);
	config->showUI = GetPrivateProfileIntW(L"MusicPlayer", L"ShowUI", 1, iniPath) != 0;

	if (config->volume < 0) config->volume = 0;
	if (config->volume > 100) config->volume = 100;
	if (config->repeat < 0) config->repeat = 0;
	if (config->repeat > 2) config->repeat = 2;

	return true;
}

bool SaveConfig(const ModConfig* config, const wchar_t* iniPath)
{
	if (!config || !iniPath) return false;

	WritePrivateProfileStringW(L"MusicPlayer", L"MusicDirectory", config->musicDirectory, iniPath);
	wchar_t buf[16];
	wsprintfW(buf, L"%d", config->volume);
	WritePrivateProfileStringW(L"MusicPlayer", L"Volume", buf, iniPath);
	WritePrivateProfileStringW(L"MusicPlayer", L"Shuffle", config->shuffle ? L"1" : L"0", iniPath);
	wsprintfW(buf, L"%d", config->repeat);
	WritePrivateProfileStringW(L"MusicPlayer", L"Repeat", buf, iniPath);
	WritePrivateProfileStringW(L"MusicPlayer", L"ShowUI", config->showUI ? L"1" : L"0", iniPath);

	return true;
}

static bool IsMusicFile(const wchar_t* fileName)
{
	const wchar_t* ext = PathFindExtensionW(fileName);
	if (!ext || ext[0] == L'\0') return false;

	for (int i = 0; i < NUM_EXTENSIONS; i++)
	{
		if (_wcsicmp(ext, SUPPORTED_EXTENSIONS[i]) == 0) return true;
	}
	return false;
}

std::vector<std::wstring> ScanMusicFiles(const wchar_t* directory)
{
	std::vector<std::wstring> files;
	if (!directory || directory[0] == L'\0') return files;

	wchar_t searchPath[MAX_PATH];
	wsprintfW(searchPath, L"%s\\*.*", directory);

	WIN32_FIND_DATAW ffd;
	HANDLE hFind = FindFirstFileW(searchPath, &ffd);

	if (hFind == INVALID_HANDLE_VALUE) return files;

	do
	{
		if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
		if (IsMusicFile(ffd.cFileName))
		{
			wchar_t fullPath[MAX_PATH];
			wsprintfW(fullPath, L"%s\\%s", directory, ffd.cFileName);
			files.push_back(fullPath);
		}
	} while (FindNextFileW(hFind, &ffd));

	FindClose(hFind);
	return files;
}
