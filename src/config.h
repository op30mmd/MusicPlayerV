#pragma once

#include <windows.h>
#include <string>
#include <vector>

struct ModConfig
{
	wchar_t musicDirectory[MAX_PATH];
	int volume;
	bool shuffle;
	int repeat; // 0 = off, 1 = all, 2 = one
	bool showUI;

	ModConfig()
	{
		musicDirectory[0] = L'\0';
		volume = 50;
		shuffle = false;
		repeat = 1;
		showUI = true;
	}
};

bool LoadConfig(ModConfig* config, const wchar_t* iniPath);
bool SaveConfig(const ModConfig* config, const wchar_t* iniPath);
std::vector<std::wstring> ScanMusicFiles(const wchar_t* directory);
