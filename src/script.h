#pragma once

#include "..\..\ScriptHookV_SDK\inc\main.h"
#include "keyboard.h"
#include "audio.h"
#include "config.h"
#include "ytdlp.h"
#include <vector>
#include <string>
#include <ctime>

void ScriptMain();

extern AudioEngine g_audioEngine;
extern ModConfig g_modConfig;
extern std::vector<std::wstring> g_trackList;
extern bool g_initialized;
extern YouTubeManager g_youtube;
