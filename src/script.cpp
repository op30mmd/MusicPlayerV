#define WIN32_LEAN_AND_MEAN
#include "script.h"
#include "ui.h"
#include "log.h"
#include "ytdlp.h"
#include <algorithm>
#include <ctime>
#include <shlobj.h>

AudioEngine g_audioEngine;
ModConfig g_modConfig;
std::vector<std::wstring> g_trackList;
bool g_initialized = false;
bool g_showUI = true;

YouTubeManager g_youtube;

static float g_volume = 0.5f;
static std::wstring g_currentTrackName;
static int g_frameCounter = 0;
static bool g_loopLogged = false;
static int g_menuIndex = 0;

enum MenuAction
{
	MENU_PLAYPAUSE,
	MENU_NEXT,
	MENU_PREV,
	MENU_VOL_DOWN,
	MENU_VOL_UP,
	MENU_SHUFFLE,
	MENU_RELOAD,
	MENU_PLAYURL,
	MENU_HIDE,
	MENU_COUNT
};

static std::wstring TrimUrl(const std::wstring& in)
{
	size_t start = 0;
	size_t end = in.size();
	while (start < end && (in[start] == L' ' || in[start] == L'\t' || in[start] == L'\r' || in[start] == L'\n'))
		start++;
	while (end > start && (in[end - 1] == L' ' || in[end - 1] == L'\t' || in[end - 1] == L'\r' || in[end - 1] == L'\n'))
		end--;
	return in.substr(start, end - start);
}

static std::wstring ReadUrlFile()
{
	wchar_t exePath[MAX_PATH];
	GetModuleFileNameW(nullptr, exePath, MAX_PATH);
	wchar_t* lastSlash = wcsrchr(exePath, L'\\');
	if (lastSlash) *(lastSlash + 1) = L'\0';

	std::wstring path = std::wstring(exePath) + L"MusicPlayer.url";
	FILE* f = nullptr;
	if (_wfopen_s(&f, path.c_str(), L"r, ccs=UNICODE") != 0 || !f) return std::wstring();

	wchar_t buf[2048] = { 0 };
	if (!fgetws(buf, 2048, f))
	{
		fclose(f);
		return std::wstring();
	}
	fclose(f);
	return TrimUrl(buf);
}

void StartYouTubeFromClipboard()
{
	if (!g_youtube.IsAvailable())
	{
		LogMessage(L"YouTube: start requested but yt-dlp.exe not found");
		UI::DrawNotification(L"MusicPlayer: yt-dlp.exe not found! Copy it next to MusicPlayer.asi");
		return;
	}

	std::wstring url = YouTubeManager::GetClipboardUrl();
	if (url.empty())
	{
		url = ReadUrlFile();
		LogMessage(L"YouTube: clipboard empty, checked MusicPlayer.url fallback (%d)", url.empty() ? 0 : 1);
	}
	if (url.empty())
	{
		UI::DrawNotification(L"MusicPlayer: No URL in clipboard or MusicPlayer.url");
		return;
	}

	if (!g_youtube.StartDownload(url))
	{
		UI::DrawNotification(L"MusicPlayer: Download already in progress");
		return;
	}

	LogMessage(L"YouTube: queued %s", url.c_str());
	UI::DrawNotification(L"MusicPlayer: Downloading YouTube audio...");
}

void PollYouTubeDownloads()
{
	YtDownload dl;
	while (g_youtube.PopResult(dl))
	{
		if (!dl.ok)
		{
			std::wstring msg = L"MusicPlayer: YouTube download failed: " + dl.error;
			if (msg.size() > 120) msg = msg.substr(0, 120);
			UI::DrawNotification(msg.c_str());
			LogMessage(L"YouTube: failed notification: %s", msg.c_str());
			continue;
		}

		g_trackList.push_back(dl.filePath);
		g_audioEngine.SetPlayQueue(g_trackList.data(), (int)g_trackList.size());
		g_audioEngine.SetCurrentIndex((int)g_trackList.size() - 1);
		g_audioEngine.PlayQueue();

		g_currentTrackName = dl.filePath;
		size_t pos = g_currentTrackName.find_last_of(L"\\/");
		if (pos != std::wstring::npos)
			g_currentTrackName = g_currentTrackName.substr(pos + 1);
		LogMessage(L"YouTube: added to library and playing %s", dl.filePath.c_str());
		UI::DrawNotification(L"MusicPlayer: Now playing downloaded track");
	}
}

void InitMusicPlayer()
{
	LogMessage(L"InitMusicPlayer: start");

	wchar_t exePath[MAX_PATH];
	GetModuleFileNameW(nullptr, exePath, MAX_PATH);

	wchar_t* lastSlash = wcsrchr(exePath, L'\\');
	if (lastSlash) *(lastSlash + 1) = L'\0';

	wchar_t iniPath[MAX_PATH];
	wsprintfW(iniPath, L"%s\\MusicPlayer.ini", exePath);

	LoadConfig(&g_modConfig, iniPath);
	LogMessage(L"InitMusicPlayer: ini=%s dir=%s vol=%d shuffle=%d showUI=%d",
		iniPath, g_modConfig.musicDirectory, g_modConfig.volume, g_modConfig.shuffle, g_modConfig.showUI);

	g_volume = g_modConfig.volume / 100.0f;
	g_showUI = g_modConfig.showUI;

	if (!g_modConfig.musicDirectory[0])
	{
		PWSTR path = nullptr;
		if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Music, 0, nullptr, &path)))
		{
			wcscpy_s(g_modConfig.musicDirectory, path);
			CoTaskMemFree(path);
		}
		LogMessage(L"InitMusicPlayer: using default Music dir: %s", g_modConfig.musicDirectory);
	}

	g_trackList = ScanMusicFiles(g_modConfig.musicDirectory);
	LogMessage(L"InitMusicPlayer: found %d tracks", (int)g_trackList.size());

	std::wstring ytDir = std::wstring(g_modConfig.musicDirectory) + L"\\YouTube";
	g_youtube.Initialize(exePath, ytDir);
	LogMessage(L"InitMusicPlayer: youtube available=%d dir=%s", g_youtube.IsAvailable() ? 1 : 0, ytDir.c_str());

	if (!g_audioEngine.Initialize())
	{
		LogMessage(L"InitMusicPlayer: audio engine init FAILED");
		UI::DrawNotification(L"MusicPlayer: Failed to initialize audio engine!");
		return;
	}
	LogMessage(L"InitMusicPlayer: audio engine OK");
	g_audioEngine.SetVolume(g_volume);

	if (g_trackList.empty())
	{
		LogMessage(L"InitMusicPlayer: no local tracks, YouTube mode only");
		UI::DrawNotification(L"MusicPlayer: No music files found! Copy a YouTube URL and press F12");
		g_initialized = true;
		return;
	}

	if (g_modConfig.shuffle)
	{
		srand((unsigned int)time(nullptr));
		std::random_shuffle(g_trackList.begin(), g_trackList.end());
		LogMessage(L"InitMusicPlayer: shuffled");
	}

	g_audioEngine.SetPlayQueue(g_trackList.data(), (int)g_trackList.size());

	int firstOk = -1;
	for (int i = 0; i < (int)g_trackList.size(); i++)
	{
		if (g_audioEngine.LoadFile(g_trackList[i]))
		{
			firstOk = i;
			break;
		}
	}

	if (firstOk == -1)
	{
		LogMessage(L"InitMusicPlayer: no loadable track found");
		UI::DrawNotification(L"MusicPlayer: No supported tracks found! Copy a YouTube URL and press F12");
		g_initialized = true;
		return;
	}

	LogMessage(L"InitMusicPlayer: first playable track index=%d", firstOk);

	g_audioEngine.SetCurrentIndex(firstOk);

	g_currentTrackName = g_trackList[firstOk];
	size_t pos = g_currentTrackName.find_last_of(L"\\/");
	if (pos != std::wstring::npos)
		g_currentTrackName = g_currentTrackName.substr(pos + 1);

	g_audioEngine.Play();

	wchar_t msg[256];
	wsprintfW(msg, L"MusicPlayer: %d tracks loaded", (int)g_trackList.size());
	UI::DrawNotification(msg);

	g_initialized = true;
	LogMessage(L"InitMusicPlayer: done, initialized=%d", g_initialized);
}

void ShutdownMusicPlayer()
{
	g_youtube.Shutdown();
	g_audioEngine.Shutdown();
}

void NextTrack()
{
	if (g_trackList.empty()) return;
	g_audioEngine.NextTrack();
	g_currentTrackName = g_audioEngine.GetCurrentTrackName();
	size_t pos = g_currentTrackName.find_last_of(L"\\/");
	if (pos != std::wstring::npos)
		g_currentTrackName = g_currentTrackName.substr(pos + 1);
	LogMessage(L"NextTrack: now playing %s", g_currentTrackName.c_str());
}

void PreviousTrack()
{
	if (g_trackList.empty()) return;
	g_audioEngine.PreviousTrack();
	g_currentTrackName = g_audioEngine.GetCurrentTrackName();
	size_t pos = g_currentTrackName.find_last_of(L"\\/");
	if (pos != std::wstring::npos)
		g_currentTrackName = g_currentTrackName.substr(pos + 1);
	LogMessage(L"PreviousTrack: now playing %s", g_currentTrackName.c_str());
}

void TogglePlayPause()
{
	if (g_trackList.empty()) return;
	if (g_audioEngine.IsPlaying())
	{
		g_audioEngine.Pause();
		LogMessage(L"TogglePlayPause: paused");
	}
	else
	{
	g_audioEngine.Play();
	g_audioEngine.PreloadNextTrack();
		LogMessage(L"TogglePlayPause: playing");
	}
}

void VolumeUp()
{
	g_volume = min(1.0f, g_volume + 0.05f);
	g_modConfig.volume = (int)(g_volume * 100);
	g_audioEngine.SetVolume(g_volume);
	LogMessage(L"VolumeUp: %d%%", g_modConfig.volume);
}

void VolumeDown()
{
	g_volume = max(0.0f, g_volume - 0.05f);
	g_modConfig.volume = (int)(g_volume * 100);
	g_audioEngine.SetVolume(g_volume);
	LogMessage(L"VolumeDown: %d%%", g_modConfig.volume);
}

void ReloadLibrary()
{
	LogMessage(L"ReloadLibrary: rescanning music directory");
	g_trackList = ScanMusicFiles(g_modConfig.musicDirectory);
	g_audioEngine.SetPlayQueue(g_trackList.data(), (int)g_trackList.size());
	if (!g_trackList.empty())
	{
		g_audioEngine.PlayQueue();
		g_currentTrackName = g_audioEngine.GetCurrentTrackName();
	}
}

void ActivateMenuAction(int action)
{
	switch (action)
	{
	case MENU_PLAYPAUSE: TogglePlayPause(); break;
	case MENU_NEXT: NextTrack(); break;
	case MENU_PREV: PreviousTrack(); break;
	case MENU_VOL_DOWN: VolumeDown(); break;
	case MENU_VOL_UP: VolumeUp(); break;
	case MENU_SHUFFLE:
		g_modConfig.shuffle = !g_modConfig.shuffle;
		LogMessage(L"Menu: shuffle=%d", g_modConfig.shuffle);
		UI::DrawNotification(g_modConfig.shuffle ? L"MusicPlayer: Shuffle ON (applies on reload)" : L"MusicPlayer: Shuffle OFF");
		break;
	case MENU_RELOAD: ReloadLibrary(); break;
	case MENU_PLAYURL: StartYouTubeFromClipboard(); break;
	case MENU_HIDE:
		g_showUI = false;
		g_modConfig.showUI = g_showUI;
		LogMessage(L"Menu: UI hidden");
		break;
	}
}

void ProcessControls()
{
	if (!g_initialized)
	{
		if (IsKeyJustUp(VK_F5)) InitMusicPlayer();
		return;
	}

	if (IsKeyJustUp(VK_F8))
	{
		g_showUI = !g_showUI;
		g_modConfig.showUI = g_showUI;
		LogMessage(L"F8: showUI=%d", g_showUI);
	}

	if (IsKeyJustUp(VK_F5))
	{
		LogMessage(L"F5 pressed");
		ReloadLibrary();
	}

	if (IsKeyJustUp(VK_MEDIA_NEXT_TRACK) || IsKeyJustUp(VK_F7))
	{
		LogMessage(L"F7/MEDIA_NEXT pressed");
		NextTrack();
	}

	if (IsKeyJustUp(VK_MEDIA_PREV_TRACK) || IsKeyJustUp(VK_F6))
	{
		LogMessage(L"F6/MEDIA_PREV pressed");
		PreviousTrack();
	}

	if (IsKeyJustUp(VK_MEDIA_PLAY_PAUSE) || IsKeyJustUp(VK_F9))
	{
		LogMessage(L"F9/MEDIA_PLAYPAUSE pressed");
		TogglePlayPause();
	}

	if (IsKeyJustUp(VK_F10))
	{
		LogMessage(L"F10 pressed");
		VolumeDown();
	}

	if (IsKeyJustUp(VK_F11))
	{
		LogMessage(L"F11 pressed");
		VolumeUp();
	}

	if (IsKeyJustUp(VK_F12))
	{
		LogMessage(L"F12 pressed");
		StartYouTubeFromClipboard();
	}

	if (g_showUI)
	{
		if (IsKeyJustUp(VK_UP))
		{
			g_menuIndex = (g_menuIndex - 1 + MENU_COUNT) % MENU_COUNT;
			LogMessage(L"Menu: up -> %d", g_menuIndex);
		}

		if (IsKeyJustUp(VK_DOWN))
		{
			g_menuIndex = (g_menuIndex + 1) % MENU_COUNT;
			LogMessage(L"Menu: down -> %d", g_menuIndex);
		}

		if (IsKeyJustUp(VK_RETURN) || IsKeyJustUp(VK_SPACE))
		{
			LogMessage(L"Menu: activate item %d", g_menuIndex);
			ActivateMenuAction(g_menuIndex);
		}

		if (IsKeyJustUp(VK_LEFT))
		{
			VolumeDown();
		}

		if (IsKeyJustUp(VK_RIGHT))
		{
			VolumeUp();
		}
	}

	if (g_audioEngine.HasEnded() && !g_audioEngine.HasPendingTrackChange())
	{
		LogMessage(L"AutoAdvance: track ended");
		NextTrack();
	}
}

void DrawMenuLine(float menuLeft, float lineTop, float lineHeight, const wchar_t* text, bool active, int r, int g, int b)
{
	if (active)
	{
		UI::DrawRect(menuLeft, lineTop, 0.3f, lineHeight, 218, 242, 216, 255);
		UI::DrawText(menuLeft + 0.01f, lineTop + 0.004f, text, 0.28f, 0, 0, 0);
	}
	else
	{
		UI::DrawText(menuLeft + 0.01f, lineTop + 0.004f, text, 0.28f, r, g, b);
	}
}

void DrawUI()
{
	if (!g_showUI || !g_initialized) return;

	g_frameCounter++;
	if (g_frameCounter % 180 == 0)
	{
		LogMessage(L"DrawUI: tick frames=%d playing=%d track=%s", g_frameCounter, g_audioEngine.IsPlaying(), g_currentTrackName.c_str());
	}

	const float menuLeft = 0.015f;
	const float menuTop = 0.05f;
	const float lineHeight = 0.028f;

	std::wstring ytStatus = g_youtube.GetStatusText();

	// Dynamic height: top pad + 3 info lines + optional yt line + items + hint + bottom pad
	float bgHeight = lineHeight * 0.72f
		+ lineHeight * (1.15f + 1.15f + 1.2f)
		+ (ytStatus.empty() ? 0.0f : lineHeight * 1.2f)
		+ lineHeight * (float)MENU_COUNT
		+ lineHeight * 1.3f
		+ lineHeight * 0.3f;
	UI::DrawRect(menuLeft, menuTop, 0.3f, bgHeight, 0, 0, 0, 220);

	float lineTop = menuTop + lineHeight * 0.72f;

	UI::DrawText(menuLeft + 0.01f, lineTop + 0.004f, L"[MusicPlayer]", 0.3f, 0, 200, 255);
	lineTop += lineHeight * 1.15f;

	wchar_t trackText[256] = L"No track loaded";
	if (!g_currentTrackName.empty())
	{
		wsprintfW(trackText, L"%s", g_currentTrackName.c_str());
	}
	UI::DrawText(menuLeft + 0.01f, lineTop + 0.004f, trackText, 0.22f, 200, 200, 200);
	lineTop += lineHeight * 1.15f;

	wchar_t statusText[64];
	wsprintfW(statusText, L"%s | Vol: %d%%", g_audioEngine.IsPlaying() ? L"Playing" : L"Paused", (int)(g_volume * 100));
	UI::DrawText(menuLeft + 0.01f, lineTop + 0.004f, statusText, 0.25f,
		g_audioEngine.IsPlaying() ? 0 : 180, g_audioEngine.IsPlaying() ? 255 : 180, 0);
	lineTop += lineHeight * 1.2f;

	if (!ytStatus.empty())
	{
		UI::DrawText(menuLeft + 0.01f, lineTop + 0.004f, ytStatus.c_str(), 0.22f, 255, 200, 100);
		lineTop += lineHeight * 1.2f;
	}

	wchar_t itemText[64];
	for (int i = 0; i < MENU_COUNT; i++)
	{
		bool active = (i == g_menuIndex);
		switch (i)
		{
		case MENU_PLAYPAUSE:
			wsprintfW(itemText, L"Play / Pause (%s)", g_audioEngine.IsPlaying() ? L"playing" : L"paused");
			break;
		case MENU_NEXT:
			wsprintfW(itemText, L"Next Track");
			break;
		case MENU_PREV:
			wsprintfW(itemText, L"Previous Track");
			break;
		case MENU_VOL_DOWN:
			wsprintfW(itemText, L"Volume -");
			break;
		case MENU_VOL_UP:
			wsprintfW(itemText, L"Volume +");
			break;
		case MENU_SHUFFLE:
			wsprintfW(itemText, L"Shuffle: %s", g_modConfig.shuffle ? L"On" : L"Off");
			break;
		case MENU_RELOAD:
			wsprintfW(itemText, L"Reload Library");
			break;
		case MENU_PLAYURL:
			wsprintfW(itemText, L"Play YouTube URL (F12)");
			break;
		case MENU_HIDE:
			wsprintfW(itemText, L"Hide UI (F8)");
			break;
		}
		DrawMenuLine(menuLeft, lineTop, lineHeight, itemText, active, 255, 255, 255);
		lineTop += lineHeight;
	}

	lineTop += lineHeight * 0.3f;
	UI::DrawText(menuLeft + 0.01f, lineTop + 0.004f, L"Up/Down: move | Enter: select | L/R: volume", 0.2f, 150, 150, 150);
}

void DisableGameControlsForMenu()
{
	// DISABLE_CONTROL_ACTION: disable phone-related controls while the menu is open
	static const int phoneControls[] = { 27, 172, 173, 174, 175, 176, 177, 178, 179 };
	for (int i = 0; i < 9; i++)
	{
		nativeInit(0xFE99B66D079CF6BC);
		nativePush64(0);
		nativePush64((UINT64)phoneControls[i]);
		nativePush64(1);
		nativeCall();
	}
}

void HandleVehicleRadio()
{
	if (!g_initialized) return;

	bool playing = g_audioEngine.IsPlaying();

	// Force the global radio station off while music plays (both name and index forms)
	if (playing)
	{
		nativeInit(0xC69EDA28699D5107); // SET_RADIO_TO_STATION_NAME
		nativePush64((UINT64)"OFF");
		nativeCall();

		nativeInit(0xA619B168B8A8570F); // SET_RADIO_TO_STATION_INDEX
		nativePush64((UINT64)-1);
		nativeCall();

		static bool s_radioOffLogged = false;
		if (!s_radioOffLogged)
		{
			s_radioOffLogged = true;
			LogMessage(L"Radio: vehicle radio forced off (music playing)");
		}
	}

	// Mute the radio in the player's current vehicle (re-enable when music stops)
	nativeInit(0x4F8644AF03D0E0D6); // PLAYER_ID
	UINT64 playerId = *nativeCall();

	nativeInit(0x43A66C31C68491C0); // GET_PLAYER_PED
	nativePush64(playerId);
	UINT64 playerPed = *nativeCall();

	nativeInit(0x997ABD671D25CA0B); // IS_PED_IN_ANY_VEHICLE
	nativePush64(playerPed);
	nativePush64(0);
	UINT64 inVehicle = *nativeCall();

	if (inVehicle)
	{
		nativeInit(0x9A9112A0FE9A4713); // GET_VEHICLE_PED_IS_IN
		nativePush64(playerPed);
		nativePush64(0);
		UINT64 veh = *nativeCall();

		nativeInit(0x3B988190C0AA6C0B); // SET_VEHICLE_RADIO_ENABLED
		nativePush64(veh);
		nativePush64(playing ? 0 : 1);
		nativeCall();
	}
}

void ProcessMusicPlayer()
{
	ProcessControls();

	if (g_initialized && g_showUI)
	{
		DisableGameControlsForMenu();
	}

	HandleVehicleRadio();

	g_audioEngine.Update();
	PollYouTubeDownloads();
	DrawUI();
}

void ScriptMain()
{
	LogMessage(L"ScriptMain: entered");
	InitMusicPlayer();

	while (true)
	{
		__try
		{
			if (!g_loopLogged)
			{
				g_loopLogged = true;
				LogMessage(L"ScriptMain: loop tick 1, initialized=%d", g_initialized);
			}
			ProcessMusicPlayer();
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogMessage(L"ScriptMain: EXCEPTION in loop, code=0x%08X", GetExceptionCode());
			WAIT(1000);
		}
		WAIT(0);
	}

	ShutdownMusicPlayer();
}
