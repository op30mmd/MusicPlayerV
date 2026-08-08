#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>

struct YtDownload
{
	bool ok = false;
	std::wstring filePath;
	std::wstring title;
	std::wstring error;
};

struct YtTrack
{
	std::wstring id;
	std::wstring title;
	std::wstring url;
	int type = 0; // 0 = video, 1 = playlist
};

class YouTubeManager
{
public:
	YouTubeManager();
	~YouTubeManager();

	bool Initialize(const std::wstring& exeDir, const std::wstring& downloadDir);
	void Shutdown();

	bool IsAvailable() const { return !m_ytdlpPath.empty(); }
	bool StartDownload(const std::wstring& url);
	bool ScrapePlaylist(const std::wstring& url);
	bool ScrapeHome();
	bool IsBusy() const;
	std::wstring GetStatusText() const;
	bool PopResult(YtDownload& out);
	bool PopScrapeResult(std::vector<YtTrack>& out, std::wstring& error);

	static std::wstring GetClipboardUrl();

private:
	void ThreadMain();
	void RefreshSettings();
	bool FindYtDlpExe(const std::wstring& exeDir);
	bool RunProcess(const std::wstring& cmdLine, const std::wstring& workDir, std::string& stdoutText, DWORD& exitCode);
	void SetStatusLocked(const wchar_t* text);

	std::wstring m_exeDir;
	std::wstring m_downloadDir;
	std::wstring m_ytdlpPath;
	std::wstring m_browserName;
	std::wstring m_cookiesPath;
	std::wstring m_jsRuntime;
	std::wstring m_extraArgs;
	int m_bitrate = 192;
	int m_maxSizeMb = 100;
	int m_timeoutSec = 1800;

	std::thread m_thread;
	mutable std::mutex m_mutex;
	std::condition_variable m_cv;
	bool m_shutdown = false;
	bool m_busy = false;
	std::wstring m_pendingUrl;
	std::wstring m_status;
	std::vector<YtDownload> m_results;

	std::wstring m_scrapeUrl;
	bool m_scrapePending = false;
	bool m_homePending = false;
	bool m_scrapeResultReady = false;
	std::vector<YtTrack> m_browseTracks;
	std::wstring m_scrapeError;
	std::wstring m_nodePath;
	std::wstring m_homeScriptPath;
};
