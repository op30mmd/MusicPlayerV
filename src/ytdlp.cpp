#include "ytdlp.h"
#include "log.h"
#include <cstdio>

YouTubeManager::YouTubeManager()
{
}

YouTubeManager::~YouTubeManager()
{
	Shutdown();
}

static std::wstring Utf8ToWide(const std::string& utf8)
{
	if (utf8.empty()) return std::wstring();
	int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), nullptr, 0);
	if (len <= 0) return std::wstring();
	std::wstring out;
	out.resize(len);
	MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), &out[0], len);
	return out;
}

static std::wstring TrimQuotesWhitespace(const std::wstring& in)
{
	size_t start = 0;
	size_t end = in.size();
	while (start < end && (in[start] == L' ' || in[start] == L'\t' || in[start] == L'\r' || in[start] == L'\n' || in[start] == L'"' || in[start] == L'\''))
		start++;
	while (end > start && (in[end - 1] == L' ' || in[end - 1] == L'\t' || in[end - 1] == L'\r' || in[end - 1] == L'\n' || in[end - 1] == L'"' || in[end - 1] == L'\''))
		end--;
	return in.substr(start, end - start);
}

static std::string LastNonEmptyLine(const std::string& text)
{
	size_t end = text.size();
	while (end > 0 && (text[end - 1] == '\r' || text[end - 1] == '\n')) end--;
	size_t start = end;
	while (start > 0 && text[start - 1] != '\r' && text[start - 1] != '\n') start--;
	return text.substr(start, end - start);
}

static std::wstring NewestAudioFile(const std::wstring& dir, const FILETIME& afterTime)
{
	const wchar_t* exts[] = { L".m4a", L".webm", L".mp3", L".opus", L".aac", L".ogg", L".mp4" };
	std::wstring best;
	FILETIME bestTime = { 0 };

	WIN32_FIND_DATAW ffd;
	HANDLE h = FindFirstFileW((dir + L"\\*.*").c_str(), &ffd);
	if (h == INVALID_HANDLE_VALUE) return best;

	do
	{
		if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
		const wchar_t* ext = wcsrchr(ffd.cFileName, L'.');
		if (!ext) continue;
		bool match = false;
		for (int i = 0; i < 7 && !match; i++)
			match = _wcsicmp(ext, exts[i]) == 0;
		if (!match) continue;
		if (CompareFileTime(&ffd.ftLastWriteTime, &afterTime) <= 0) continue;
		if (best.empty() || CompareFileTime(&ffd.ftLastWriteTime, &bestTime) > 0)
		{
			best = dir + L"\\" + ffd.cFileName;
			bestTime = ffd.ftLastWriteTime;
		}
	} while (FindNextFileW(h, &ffd));

	FindClose(h);
	return best;
}

static void ParseTrackLines(const std::string& text, std::vector<YtTrack>& out)
{
	size_t pos = 0;
	while (pos < text.size())
	{
		size_t nl = text.find('\n', pos);
		if (nl == std::string::npos) nl = text.size();
		std::string line = text.substr(pos, nl - pos);
		pos = nl + 1;
		if (!line.empty() && line[line.size() - 1] == '\r') line.pop_back();
		if (line.empty()) continue;
		size_t t1 = line.find('\t');
		if (t1 == std::string::npos) continue;
		size_t t2 = line.find('\t', t1 + 1);
		if (t2 == std::string::npos) continue;
		size_t t3 = line.rfind('\t');
		if (t3 == t2) t3 = std::string::npos; // 3-column yt-dlp output (no type)
		YtTrack tr;
		tr.id = Utf8ToWide(line.substr(0, t1));
		tr.title = Utf8ToWide(line.substr(t1 + 1, t2 - t1 - 1));
		for (wchar_t& c : tr.title)
			if (c == L'\r' || c == L'\n' || c == L'\t') c = L' ';
		std::wstring type;
		if (t3 != std::string::npos)
		{
			tr.url = Utf8ToWide(line.substr(t2 + 1, t3 - t2 - 1));
			type = Utf8ToWide(line.substr(t3 + 1));
		}
		else
		{
			tr.url = Utf8ToWide(line.substr(t2 + 1));
		}
		tr.type = (type == L"playlist") ? 1 : 0;
		if (tr.id.empty() || tr.url.empty()) continue;
		out.push_back(std::move(tr));
	}
}

static std::string StripAnsi(const std::string& in)
{
	std::string out;
	out.reserve(in.size());
	for (size_t i = 0; i < in.size(); i++)
	{
		if (in[i] == '\033')
		{
			while (i < in.size() && in[i] != 'm') i++;
			continue;
		}
		out += in[i];
	}
	return out;
}

static std::wstring ExtractError(const std::string& text)
{
	std::string clean = StripAnsi(text);
	size_t pos = clean.rfind("ERROR:");
	if (pos == std::string::npos) return std::wstring();
	std::string line = clean.substr(pos + 6);
	size_t end = line.find_first_of("\r\n");
	if (end != std::string::npos) line = line.substr(0, end);
	while (!line.empty() && (line[0] == ' ' || line[0] == '\t')) line.erase(0, 1);
	while (!line.empty() && (line[line.size() - 1] == ' ' || line[line.size() - 1] == '\t')) line.pop_back();
	if (line.size() > 200) line = line.substr(0, 200);
	return Utf8ToWide(line);
}

// A dumped cookie jar is only useful if it actually contains YouTube cookies;
// yt-dlp can leave a header-only file behind when cookie extraction fails
// (e.g. Firefox profile locked by a running browser). The full jar must be
// scanned: jars sort cookies by domain and youtube.com entries can sit far
// past the first 64 KB.
static bool CookiesFileUsable(const std::wstring& path)
{
	HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE) return false;
	std::string content;
	char buf[65536];
	DWORD read = 0;
	while (content.size() < 8 * 1024 * 1024 &&
		ReadFile(h, buf, sizeof(buf), &read, nullptr) && read > 0)
		content.append(buf, read);
	CloseHandle(h);
	return content.find("youtube.com") != std::string::npos;
}

static void CleanupPartialFiles(const std::wstring& dir, const FILETIME& afterTime)
{
	WIN32_FIND_DATAW ffd;
	HANDLE h = FindFirstFileW((dir + L"\\*.*").c_str(), &ffd);
	if (h == INVALID_HANDLE_VALUE) return;
	do
	{
		if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
		const wchar_t* ext = wcsrchr(ffd.cFileName, L'.');
		if (!ext) continue;
		bool match = _wcsicmp(ext, L".part") == 0 || _wcsicmp(ext, L".ytdl") == 0;
		if (!match) continue;
		if (CompareFileTime(&ffd.ftLastWriteTime, &afterTime) < 0) continue;
		std::wstring full = dir + L"\\" + ffd.cFileName;
		DeleteFileW(full.c_str());
		LogMessage(L"YouTube: removed partial file %s", full.c_str());
	} while (FindNextFileW(h, &ffd));
	FindClose(h);
}

bool YouTubeManager::Initialize(const std::wstring& exeDir, const std::wstring& downloadDir)
{
	m_exeDir = exeDir;
	m_downloadDir = downloadDir;
	if (!m_downloadDir.empty())
		CreateDirectoryW(m_downloadDir.c_str(), nullptr);

	if (GetFileAttributesW((exeDir + L"cookies.txt").c_str()) != INVALID_FILE_ATTRIBUTES)
		m_cookiesPath = exeDir + L"cookies.txt";

	if (!FindYtDlpExe(exeDir))
	{
		LogMessage(L"YouTube: yt-dlp.exe NOT found (looked next to .asi and in PATH). Put yt-dlp.exe next to MusicPlayer.asi");
		return false;
	}

	RefreshSettings();
	LogMessage(L"YouTube: yt-dlp at %s", m_ytdlpPath.c_str());
	if (!m_cookiesPath.empty()) LogMessage(L"YouTube: using cookies file %s", m_cookiesPath.c_str());
	if (!m_browserName.empty()) LogMessage(L"YouTube: using live browser cookies (%s) - no export needed, refresh handled automatically", m_browserName.c_str());
	if (!m_jsRuntime.empty()) LogMessage(L"YouTube: js runtime: %s", m_jsRuntime.c_str());
	if (!m_extraArgs.empty()) LogMessage(L"YouTube: extra args: %s", m_extraArgs.c_str());
	LogMessage(L"YouTube: max audio bitrate=%d kbps max size=%d MB timeout=%d s", m_bitrate, m_maxSizeMb, m_timeoutSec);
	m_shutdown = false;
	m_thread = std::thread(&YouTubeManager::ThreadMain, this);
	return true;
}

void YouTubeManager::RefreshSettings()
{
	wchar_t iniPath[MAX_PATH];
	wsprintfW(iniPath, L"%sMusicPlayer.ini", m_exeDir.c_str());

	wchar_t browser[64] = { 0 };
	GetPrivateProfileStringW(L"MusicPlayer", L"YtDlpBrowser", L"", browser, 64, iniPath);
	m_browserName = browser[0] ? browser : std::wstring();

	wchar_t jsRuntime[64] = { 0 };
	GetPrivateProfileStringW(L"MusicPlayer", L"YtDlpJsRuntime", L"", jsRuntime, 64, iniPath);
	m_jsRuntime = jsRuntime[0] ? jsRuntime : std::wstring();

	wchar_t extraArgs[1024] = { 0 };
	GetPrivateProfileStringW(L"MusicPlayer", L"YtDlpArgs", L"", extraArgs, 1024, iniPath);
	m_extraArgs = extraArgs[0] ? extraArgs : std::wstring();

	m_bitrate = GetPrivateProfileIntW(L"MusicPlayer", L"YtDlpBitrate", 192, iniPath);
	m_maxSizeMb = GetPrivateProfileIntW(L"MusicPlayer", L"YtDlpMaxSize", 100, iniPath);
	m_timeoutSec = GetPrivateProfileIntW(L"MusicPlayer", L"YtDlpTimeout", 1800, iniPath);
}

void YouTubeManager::Shutdown()
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_shutdown = true;
	}
	m_cv.notify_all();
	if (m_thread.joinable()) m_thread.join();

	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_results.clear();
	}
}

bool YouTubeManager::FindYtDlpExe(const std::wstring& exeDir)
{
	const wchar_t* candidates[] = {
		L"yt-dlp.exe",
		L"ytdlp\\yt-dlp.exe",
		L"bin\\yt-dlp.exe",
	};
	for (int i = 0; i < 3; i++)
	{
		std::wstring path = exeDir + candidates[i];
		if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
		{
			m_ytdlpPath = path;
			return true;
		}
	}

	wchar_t pathBuf[MAX_PATH];
	DWORD len = SearchPathW(nullptr, L"yt-dlp.exe", nullptr, MAX_PATH, pathBuf, nullptr);
	if (len > 0 && len < MAX_PATH)
	{
		m_ytdlpPath = pathBuf;
		return true;
	}
	return false;
}

bool YouTubeManager::StartDownload(const std::wstring& url)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (m_busy || !m_thread.joinable()) return false;
	m_pendingUrl = url;
	m_busy = true;
	SetStatusLocked(L"Downloading...");
	m_cv.notify_all();
	return true;
}

bool YouTubeManager::ScrapePlaylist(const std::wstring& url)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (m_scrapePending || m_homePending || !m_thread.joinable()) return false;
	m_scrapeUrl = url;
	m_scrapePending = true;
	SetStatusLocked(L"Scraping playlist...");
	m_cv.notify_all();
	return true;
}

bool YouTubeManager::ScrapeHome()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (m_scrapePending || m_homePending || !m_thread.joinable()) return false;
	m_homePending = true;
	SetStatusLocked(L"Scraping YT Music home...");
	m_cv.notify_all();
	return true;
}

bool YouTubeManager::IsBusy() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_busy;
}

void YouTubeManager::SetStatusLocked(const wchar_t* text)
{
	m_status = text;
}

std::wstring YouTubeManager::GetStatusText() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_status;
}

bool YouTubeManager::PopResult(YtDownload& out)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (m_results.empty()) return false;
	out = m_results.front();
	m_results.erase(m_results.begin());
	return true;
}

bool YouTubeManager::PopScrapeResult(std::vector<YtTrack>& out, std::wstring& error)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (!m_scrapeResultReady) return false;
	m_scrapeResultReady = false;
	out = std::move(m_browseTracks);
	error = m_scrapeError;
	return true;
}

std::wstring YouTubeManager::GetClipboardUrl()
{
	std::wstring url;
	if (OpenClipboard(nullptr))
	{
		HANDLE h = GetClipboardData(CF_UNICODETEXT);
		if (h)
		{
			const wchar_t* p = (const wchar_t*)GlobalLock(h);
			if (p)
			{
				url = p;
				GlobalUnlock(h);
			}
		}
		CloseClipboard();
	}
	return TrimQuotesWhitespace(url);
}

bool YouTubeManager::RunProcess(const std::wstring& cmdLine, const std::wstring& workDir, std::string& stdoutText, DWORD& exitCode)
{
	exitCode = 0;
	wchar_t tempFile[MAX_PATH];
	{
		wchar_t tempPath[MAX_PATH];
		GetTempPathW(MAX_PATH, tempPath);
		GetTempFileNameW(tempPath, L"ytdlp", 0, tempFile);
	}

	SECURITY_ATTRIBUTES sa = {};
	sa.nLength = sizeof(sa);
	sa.bInheritHandle = TRUE;

	HANDLE hOut = CreateFileW(tempFile, GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

	STARTUPINFOW si = {};
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
	si.hStdOutput = hOut ? hOut : GetStdHandle(STD_OUTPUT_HANDLE);
	si.hStdError = si.hStdOutput;
	si.hStdInput = nullptr;
	si.wShowWindow = SW_HIDE;

	PROCESS_INFORMATION pi = {};
	std::wstring cmdCopy = cmdLine;
	BOOL ok = CreateProcessW(nullptr, &cmdCopy[0], nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
		nullptr, workDir.empty() ? nullptr : workDir.c_str(), &si, &pi);

	if (hOut) CloseHandle(hOut);

	if (!ok)
	{
		LogMessage(L"YouTube: CreateProcess failed err=%u cmd=%s", GetLastError(), cmdLine.c_str());
		DeleteFileW(tempFile);
		return false;
	}

	CloseHandle(pi.hThread);

	ULONGLONG startedMs = GetTickCount64();
	while (true)
	{
		DWORD wait = WaitForSingleObject(pi.hProcess, 200);
		if (wait == WAIT_OBJECT_0)
		{
			DWORD code = 1;
			GetExitCodeProcess(pi.hProcess, &code);
			exitCode = code;
			break;
		}
		bool shutting = false;
		bool timedOut = false;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			shutting = m_shutdown;
		}
		if (m_timeoutSec > 0 && GetTickCount64() - startedMs > (ULONGLONG)m_timeoutSec * 1000ULL)
			timedOut = true;
		if (shutting || timedOut)
		{
			TerminateProcess(pi.hProcess, 1);
			LogMessage(L"YouTube: download terminated (%s)", shutting ? L"shutdown" : L"timeout");
			exitCode = 1;
			break;
		}
	}
	CloseHandle(pi.hProcess);

	FILE* f = nullptr;
	if (_wfopen_s(&f, tempFile, L"rb") != 0) f = nullptr;
	if (f)
	{
		fseek(f, 0, SEEK_END);
		long size = ftell(f);
		fseek(f, 0, SEEK_SET);
		stdoutText.resize(size > 0 ? (size_t)size : 0);
		if (size > 0) fread(&stdoutText[0], 1, (size_t)size, f);
		fclose(f);
	}
	DeleteFileW(tempFile);
	return true;
}

void YouTubeManager::ThreadMain()
{
	std::unique_lock<std::mutex> lock(m_mutex);

	while (true)
	{
		m_cv.wait(lock, [this] { return m_shutdown || m_busy || m_scrapePending || m_homePending; });
		if (m_shutdown) return;

		bool doScrape = m_scrapePending;
		bool doHome = m_homePending;
		if (doHome) m_homePending = false;
		std::wstring url = doScrape ? m_scrapeUrl : m_pendingUrl;
		if (doScrape) m_scrapePending = false; else m_pendingUrl.clear();
		std::wstring outDir = m_downloadDir;
		lock.unlock();

		if (doHome) LogMessage(L"YouTube: scraping YT Music home");
		else LogMessage(L"YouTube: %s %s", doScrape ? L"scraping" : L"downloading", url.c_str());
		RefreshSettings();

		std::wstring cookiesPath;
		if (m_cookiesPath.empty() &&
			GetFileAttributesW((m_exeDir + L"cookies.txt").c_str()) != INVALID_FILE_ATTRIBUTES)
		{
			m_cookiesPath = m_exeDir + L"cookies.txt";
			LogMessage(L"YouTube: cookies file detected: %s", m_cookiesPath.c_str());
		}

		if (!m_cookiesPath.empty())
		{
			// yt-dlp rewrites the --cookies file (cookie jar dump), so give it a
			// working copy and keep the user's exported cookies.txt pristine.
			std::wstring work = m_exeDir + L"cookies_work.txt";
			WIN32_FILE_ATTRIBUTE_DATA masterData, workData;
			bool masterOk = GetFileAttributesExW(m_cookiesPath.c_str(), GetFileExInfoStandard, &masterData);
			bool workOk = GetFileAttributesExW(work.c_str(), GetFileExInfoStandard, &workData);
			if (masterOk && (!workOk || CompareFileTime(&masterData.ftLastWriteTime, &workData.ftLastWriteTime) > 0))
				CopyFileW(m_cookiesPath.c_str(), work.c_str(), FALSE);
			if (GetFileAttributesW(work.c_str()) != INVALID_FILE_ATTRIBUTES)
				cookiesPath = work;
			else
				LogMessage(L"YouTube: WARNING could not create cookies work copy, using master");
		}

		auto runNodeScrape = [&](const std::wstring& targetUrl, std::vector<YtTrack>& tracks, std::wstring& err) -> bool
		{
			if (m_nodePath.empty())
			{
				wchar_t pathBuf[MAX_PATH];
				DWORD len = SearchPathW(nullptr, L"node.exe", nullptr, MAX_PATH, pathBuf, nullptr);
				if (len > 0 && len < MAX_PATH) m_nodePath = pathBuf;
				else { err = L"Node.js not found on PATH (needed for YT Music)"; return false; }
			}
			m_homeScriptPath = m_exeDir + L"ytmusic.mjs";
			if (GetFileAttributesW(m_homeScriptPath.c_str()) == INVALID_FILE_ATTRIBUTES)
			{
				err = L"ytmusic.mjs not found next to MusicPlayer.asi";
				return false;
			}

			std::wstring cookiesFile;
			if (!m_browserName.empty())
			{
				// Refresh a Netscape-format cookie dump from the live browser
				wchar_t tempFile[MAX_PATH];
				{
					wchar_t tempPath[MAX_PATH];
					GetTempPathW(MAX_PATH, tempPath);
					GetTempFileNameW(tempPath, L"ytcook", 0, tempFile);
				}
				// GetTempFileNameW leaves a zero-byte placeholder behind; yt-dlp
				// refuses to write into an existing non-Netscape file.
				DeleteFileW(tempFile);
				std::wstring dumpCmd = L"\"" + m_ytdlpPath + L"\" -s --flat-playlist --no-warnings "
					L"--cookies-from-browser " + m_browserName + L" --cookies \"" + tempFile + L"\" "
					L"\"https://www.youtube.com/@RickAstleyYT/videos\"";
				std::string dumpOut;
				DWORD dumpCode = 1;
				RunProcess(dumpCmd, outDir, dumpOut, dumpCode);
				// Cookies are written to the jar at extraction time, BEFORE the
				// channel scrape runs; the scrape may exit non-zero (e.g. bot-check
				// 429) while the jar is still perfectly fresh. Only the jar content
				// decides usability, the exit code is just diagnostic.
				bool dumpUsable = GetFileAttributesW(tempFile) != INVALID_FILE_ATTRIBUTES
					&& CookiesFileUsable(tempFile);
				if (dumpUsable)
					cookiesFile = tempFile;
				else
					DeleteFileW(tempFile);
				if (dumpCode != 0 || !dumpUsable)
					LogMessage(L"YouTube: cookie dump code=%lu usable=%d error=%s", dumpCode, dumpUsable ? 1 : 0,
						ExtractError(dumpOut).c_str());
			}
			if (cookiesFile.empty() && !cookiesPath.empty() && CookiesFileUsable(cookiesPath))
			{
				cookiesFile = cookiesPath;
				LogMessage(L"YouTube: using fallback cookies file (browser dump failed)");
			}
			if (cookiesFile.empty())
			{
				err = L"no usable YouTube cookies (open Firefox or re-export cookies.txt)";
				return false;
			}

			std::wstring nodeCmd = L"\"" + m_nodePath + L"\" \"" + m_homeScriptPath + L"\" \"" + cookiesFile + L"\"";
			if (!targetUrl.empty())
				nodeCmd += L" \"" + targetUrl + L"\"";
			std::string stdoutText;
			DWORD exitCode = 1;
			bool procOk = RunProcess(nodeCmd, outDir, stdoutText, exitCode);
			if (cookiesFile != cookiesPath)
				DeleteFileW(cookiesFile.c_str());
			if (procOk && exitCode == 0)
			{
				ParseTrackLines(stdoutText, tracks);
				if (tracks.empty()) { err = L"no entries found in YT Music response"; return false; }
				bool loggedIn = stdoutText.find("logged_in=1") != std::string::npos;
				LogMessage(L"YouTube: node scrape: %d entries, session %s", (int)tracks.size(),
					loggedIn ? L"authenticated" : L"anonymous");
				return true;
			}
			else if (procOk)
			{
				err = ExtractError(stdoutText);
				if (err.empty())
				{
					wchar_t buf[64];
					wsprintfW(buf, L"node script exited with code %lu", exitCode);
					err = buf;
				}
			}
			else
			{
				err = L"could not launch node.exe";
			}
			return false;
		};

		if (doHome)
		{
			std::wstring err;
			std::vector<YtTrack> tracks;
			runNodeScrape(L"", tracks, err);

			lock.lock();
			m_scrapeResultReady = true;
			m_browseTracks = std::move(tracks);
			m_scrapeError = err;
			if (!m_browseTracks.empty())
			{
				wchar_t buf[64];
				wsprintfW(buf, L"Ready: %d tracks", (int)m_browseTracks.size());
				SetStatusLocked(buf);
				LogMessage(L"YouTube: YT Music home OK: %d tracks", (int)m_browseTracks.size());
			}
			else
			{
				SetStatusLocked(L"Scrape failed");
				LogMessage(L"YouTube: YT Music home FAILED error=%s", err.c_str());
			}
			continue;
		}

		if (doScrape)
		{
			std::wstring scrapeCmd = L"\"" + m_ytdlpPath + L"\" --flat-playlist --no-warnings "
				L"--playlist-items 1-500 --print \"%(id)s\t%(title)s\t%(url)s\" ";
			if (!m_browserName.empty())
				scrapeCmd += L"--cookies-from-browser " + m_browserName + L" ";
			else if (!cookiesPath.empty())
				scrapeCmd += L"--cookies \"" + cookiesPath + L"\" ";
			scrapeCmd += L"\"" + url + L"\"";

			std::string stdoutText;
			DWORD exitCode = 1;
			bool procOk = RunProcess(scrapeCmd, outDir, stdoutText, exitCode);

			std::vector<YtTrack> tracks;
			std::wstring err;
			if (procOk && exitCode == 0)
			{
				ParseTrackLines(stdoutText, tracks);
				if (tracks.empty()) err = L"no tracks found in playlist";
			}
			else if (procOk)
			{
				err = ExtractError(stdoutText);
				if (err.empty())
				{
					wchar_t buf[64];
					wsprintfW(buf, L"yt-dlp exited with code %lu", exitCode);
					err = buf;
				}
			}
			else
			{
				err = L"could not launch yt-dlp";
			}

			if (tracks.empty())
			{
				// yt-dlp cannot scrape YT Music mix/album playlists (authcheck 400);
				// fall back to the InnerTube node script for those URLs
				std::wstring nodeErr;
				std::vector<YtTrack> nodeTracks;
				if (runNodeScrape(url, nodeTracks, nodeErr))
				{
					tracks = std::move(nodeTracks);
					err.clear();
				}
				else if (err.empty())
				{
					err = nodeErr;
				}
				else
				{
					err += L" | " + nodeErr;
				}
			}

			lock.lock();
			m_scrapeResultReady = true;
			m_browseTracks = std::move(tracks);
			m_scrapeError = err;
			if (!m_browseTracks.empty())
			{
				wchar_t buf[64];
				wsprintfW(buf, L"Ready: %d tracks", (int)m_browseTracks.size());
				SetStatusLocked(buf);
				LogMessage(L"YouTube: playlist scrape OK: %d tracks", (int)m_browseTracks.size());
			}
			else
			{
				SetStatusLocked(L"Scrape failed");
				LogMessage(L"YouTube: playlist scrape FAILED url=%s error=%s", url.c_str(), err.c_str());
			}
			continue;
		}

		std::wstring fmt = L"ba*";
		if (m_bitrate > 0)
		{
			wchar_t buf[64];
			wsprintfW(buf, L"ba*[abr<=%d]/ba*", m_bitrate);
			fmt = buf;
		}
		std::wstring cmd = L"\"" + m_ytdlpPath + L"\" --no-playlist -q --no-warnings "
			L"--print after_move:filepath -f \"" + fmt + L"\" ";
		if (!m_jsRuntime.empty())
			cmd += L"--js-runtimes " + m_jsRuntime + L" ";
		if (!m_extraArgs.empty())
			cmd += m_extraArgs + L" ";
		if (m_maxSizeMb > 0)
		{
			wchar_t buf[32];
			wsprintfW(buf, L"--max-filesize %dM ", m_maxSizeMb);
			cmd += buf;
		}
		if (!m_browserName.empty())
		{
			cmd += L"--cookies-from-browser " + m_browserName + L" ";
			LogMessage(L"YouTube: using live browser cookies (%s)", m_browserName.c_str());
		}
		else if (!cookiesPath.empty())
		{
			cmd += L"--cookies \"" + cookiesPath + L"\" ";
			LogMessage(L"YouTube: using cookies file %s", cookiesPath.c_str());
		}
		cmd += L"-o \"" + outDir + L"\\%(title)s.%(ext)s\" \"" + url + L"\"";

		std::string stdoutText;
		FILETIME started;
		GetSystemTimeAsFileTime(&started);
		DWORD exitCode = 1;
		bool procOk = RunProcess(cmd, outDir, stdoutText, exitCode);

		YtDownload res;
		if (procOk && exitCode == 0)
		{
			std::wstring printed = TrimQuotesWhitespace(Utf8ToWide(LastNonEmptyLine(stdoutText)));
			if (!printed.empty() &&
				GetFileAttributesW(printed.c_str()) != INVALID_FILE_ATTRIBUTES)
			{
				res.filePath = printed;
				res.ok = true;
			}
			else
			{
				if (!printed.empty())
					LogMessage(L"YouTube: printed path not valid: %s", printed.c_str());
				LogMessage(L"YouTube: falling back to newest file scan");
				res.filePath = NewestAudioFile(outDir, started);
				res.ok = !res.filePath.empty();
			}
		}
		else
		{
			if (procOk)
			{
				res.error = ExtractError(stdoutText);
				if (res.error.empty())
				{
					wchar_t buf[64];
					wsprintfW(buf, L"yt-dlp exited with code %lu", exitCode);
					res.error = buf;
				}
				LogMessage(L"YouTube: download FAILED url=%s error=%s", url.c_str(), res.error.c_str());
				CleanupPartialFiles(outDir, started);
			}
			else
			{
				res.error = L"could not launch yt-dlp";
				LogMessage(L"YouTube: could not launch yt-dlp url=%s", url.c_str());
			}
			res.ok = false;
		}

		lock.lock();
		m_busy = false;
		if (res.ok)
		{
			size_t pos = res.filePath.find_last_of(L"\\/");
			res.title = (pos == std::wstring::npos) ? res.filePath : res.filePath.substr(pos + 1);
			SetStatusLocked(L"Ready: ");
			m_status += res.title;
			LogMessage(L"YouTube: download OK %s", res.filePath.c_str());
		}
		else
		{
			SetStatusLocked(L"Failed: ");
			m_status += res.error;
			LogMessage(L"YouTube: result FAILED url=%s", url.c_str());
		}
		m_results.push_back(res);
	}
}
