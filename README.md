# MusicPlayer - GTA V Script Hook V Mod

An in-game music player for GTA V. Decodes MP3, WAV, FLAC, OGG, WMA, AAC, M4A files with FFmpeg (Media Foundation fallback) and plays them through XAudio2. Decoding runs on a background thread with lookahead preloading, so track switches are instant.

## Features

- Full-screen-transparent menu UI with keyboard navigation (F8 to toggle)
- Instant track switching (next track pre-decoded in the background while the current one plays)
- Auto-advance to the next track on natural end; unsupported/corrupt tracks are skipped automatically
- **Repeat modes**: Off, Repeat All, Repeat One — persisted in `MusicPlayer.ini` (menu item cycles through them)
- **Playback queue for YouTube tracks**: queue tracks from browse mode, they download chained in the background and play back-to-back when the current track ends
- **No freezes**: queued/downloaded YouTube tracks are decoded on the background thread with preloading, just like the local library — the main queue resumes where it was interrupted when the queue is left (or skipped manually)
- **Content caching**: never re-downloads a YouTube URL you've played before (`MusicPlayerCache.txt` maps URL -> audio file); Download failures don't pollute the cache
- **Scraper caching**: playlist/album/mix and home feeds are cached in memory (10-30 min TTL), so re-opening a list is instant
- Vehicle radio is forced off automatically while music is playing (restored when playback stops)
- Phone / arrow-key game controls are blocked while the menu is visible
- Playback survives menu open/close; volume, shuffle, repeat and UI state persist in `MusicPlayer.ini`
- Play YouTube audio: copy a video URL, press F12 (or use the menu item) — the mod downloads the best audio stream via `yt-dlp.exe` and plays it through the normal pipeline

## Controls

| Key | Action |
|-----|--------|
| F5 | Reload music library / Initialize |
| F6 / Media Previous | Previous track |
| F7 / Media Next | Next track |
| F9 / Media Play-Pause | Play / Pause |
| F10 | Volume Down |
| F11 | Volume Up |
| F12 | Download & play a YouTube URL (from clipboard, or `MusicPlayer.url`) |
| F8 | Toggle UI display |
| Arrow Up / Down | Navigate menu (menu visible) |
| Enter / Space | Activate menu item (menu visible) |
| Arrow Left / Right | Volume down / up (menu visible) |
| E | Add the highlighted track to the YouTube queue (browse mode) |
| Backspace | Leave browse mode / go back to the main menu |

When the menu is hidden, all arrow/Enter/Space keys belong to the game.

## Configuration

Edit `MusicPlayer.ini` in your GTA V folder:

```ini
[MusicPlayer]
MusicDirectory=C:\Users\YourName\Music
Volume=50
Shuffle=0
Repeat=1
ShowUI=1
YtDlpJsRuntime=node
```

If `MusicDirectory` is empty, the Windows Music library folder is used.

`Repeat`: `0` = off, `1` = repeat all (default), `2` = repeat one. It can also be changed from the in-game menu (the "Repeat" item cycles Off -> All -> One).

## Playing YouTube audio

1. Copy a YouTube (or any yt-dlp-supported site) video URL to your clipboard
2. In-game, press F12 or select "Play YouTube URL" in the menu (F8)
3. The mod runs `yt-dlp` in the background, saves the audio to `MusicDirectory\YouTube\`, then plays it

Notes:

- Downloading YouTube audio violates YouTube's Terms of Service — use only for content you have the right to download.
- If the clipboard is empty, the mod falls back to the first line of `MusicPlayer.url` (placed next to the `.asi`).
- No ffmpeg.exe is needed: the mod requests the best audio-only stream (`-f ba*`), which the bundled FFmpeg DLLs decode directly.
- Downloaded files stay in the YouTube folder; press F5 to reload only local library files (the folder is not scanned automatically).

## Browsing YouTube Music (playlists, mixes, home feed)

Two entry points in the menu:

- **"Browse YouTube Playlist"** — put a playlist, album, mix or channel URL in `MusicPlayer.playlist` (next to the `.asi`), or press Enter on any `[Mix]` entry you encounter while browsing.
- **"Browse YT Music Home"** — scrapes your personal home feed when you're logged in (own playlists like "Liked Music" and "My Supermix" included, plus rotating sections like "Listen again" and "Forgotten favorites"). If the feed isn't personalized, it still returns the generic top music.

Track lists are scraped via `yt-dlp --flat-playlist` (with an InnerTube fallback script `ytmusic.mjs` for YT Music mix/album URLs, which yt-dlp can't read) and the menu switches into browse mode:

- **Up / Down** — move through the track list (scrolls for long feeds)
- **Enter** — download & play the selected track
- **E** — add the selected track to the **YouTube queue** (`[Mix]` entries must be opened first)
- **Backspace** — return to the main menu
- **<- Back to menu** — same as Backspace

The main menu shows a live `YouTube queue: N in line (M ready)` line, and the **"Clear YouTube queue"** item drops the queue and returns to the main queue immediately.

### How the queue behaves

- Queued tracks download **chained in the background** (no "already downloading" rejection, up to 64); each becomes playable as soon as it arrives.
- When the current track ends, the next ready queued track plays — decoded on the background thread with the **next one preloaded**, so switches are as instant as the main queue.
- When the queue runs out, the **main queue resumes exactly where it was interrupted** (track + position); with Repeat All the queue itself loops until you skip or clear it. A manual Next press always leaves the loop.
- Every track you've played/queued (e.g. same video via F12) is served from the **content cache** instead of downloading again — no network, no bloat.

## Caching

- **Content cache** (`MusicPlayerCache.txt`, next to the `.asi`): maps YouTube URLs to already-downloaded audio files. Requesting a URL that's in the cache and still on disk plays immediately — yt-dlp is never invoked. Entries with missing files are pruned automatically (kept at most 512).
- **Scraper cache** (in memory): track lists from playlist/mix scrapes (30 min) and the home feed (10 min) are served from RAM, so re-opening a list you already browsed is instant.
- **Preload cache**: the audio engine decodes the *next* queued or library track in the background while the current one plays, so every track switch (main queue or YouTube queue) is instant with no re-decoding.

Clearing `MusicPlayerCache.txt` (while GTA V is closed) or deleting downloaded files forces a fresh download.

## YouTube cookies (age-restricted / logged-in downloads)

If YouTube asks for verification, or a download fails with "Sign in to confirm you're not a bot", give yt-dlp your browser session:

1. Install the **"Get cookies.txt LOCALLY"** extension (Chrome/Edge Web Store, open-source: https://github.com/kairi003/Get-cookies.txt-Locally)
2. Log into YouTube in the browser, open any YouTube page, click the extension → download the cookies as `cookies.txt`
3. Rename it to exactly `cookies.txt` and place it in the GTA V folder next to `MusicPlayer.asi`
4. Re-export it whenever downloads stop working (cookies expire); treat the file like a password — never share it

The mod never lets yt-dlp touch your `cookies.txt`: it makes a private working copy (`cookies_work.txt`) and hands that to yt-dlp, because yt-dlp rewrites the `--cookies` file with its session's cookie jar (and can truncate it on a failed run). The working copy refreshes automatically whenever `cookies.txt` is re-exported (newer file timestamp).

Alternative (no file needed, and cookies never go stale): add `YtDlpBrowser=firefox` (or `edge`/`chrome`) under `[MusicPlayer]` in `MusicPlayer.ini` to use `--cookies-from-browser`. yt-dlp then reads your browser's live cookie store on every download — no re-exports, cookies auto-refresh as long as you stay logged into YouTube in that browser. Firefox works with the browser open or closed; for Edge/Chrome the browser must be **running** (their cookies are app-bound encrypted, issue yt-dlp#10927).

## YouTube in 2026: PO tokens (required when "Sign in to confirm you're not a bot" / "Requested format is not available")

Since 2025, YouTube often serves no playable formats to third-party clients unless yt-dlp can solve its "PO token" and "n" JavaScript challenges. Cookies alone are not enough. The working setup is:

1. **Install Node.js** (LTS from https://nodejs.org) — used to run the token server and the "n" challenge solver.
2. **Install the bgutil PO-token provider plugin.** From a command prompt:
   ```
   git clone https://github.com/Brainicism/bgutil-ytdlp-pot-provider
   cd bgutil-ytdlp-pot-provider\server
   npm ci
   npx tsc
   Compress-Archive -Path ..\bgutil-ytdlp-pot-provider -DestinationPath "$env:APPDATA\yt-dlp\plugins\bgutil-ytdlp-pot-provider.zip" -Force
   ```
   (the zip must contain a `bgutil-ytdlp-pot-provider\` folder; any yt-dlp run on this PC picks it up)
3. Set `YtDlpJsRuntime=node` in `MusicPlayer.ini` (already present in the file created by the mod's default config). This passes `--js-runtimes node` so yt-dlp can solve the "n" challenge.
4. Keep a fresh `cookies.txt` in the GTA V folder as described above.

`YtDlpArgs` under `[MusicPlayer]` passes any extra raw arguments to yt-dlp (e.g. `--extractor-args "youtube:player_client=mweb"`) if YouTube changes things again. Requires yt-dlp from 2025-05-22 or newer.

## Installation

Copy to your GTA V directory (next to `ScriptHookV.dll`):

- `MusicPlayer.asi`
- `avcodec-63.dll`, `avformat-63.dll`, `avutil-61.dll`, `swresample-7.dll` (FFmpeg shared libs, from `build\`)
- `yt-dlp.exe` (optional, for YouTube playback) from https://github.com/yt-dlp/yt-dlp — grab the `yt-dlp.exe` standalone Windows build; it is also searched for in `ytdlp\`, `bin\` subfolders and `PATH`

A log file (`MusicPlayer.log`) is written next to the `.asi` for debugging.

## Building

Prerequisites:

- Visual Studio 2022 (v143 toolset, Windows SDK)
- [ScriptHookV SDK](https://www.dev-c.com/gtav/scripthookv/) at `..\ScriptHookV_SDK` (relative to this repo) with `inc\` and `lib\ScriptHookV.lib`
- FFmpeg 9.0 shared build with dev files: download `ffmpeg-release-full-shared.7z` from https://www.gyan.dev/ffmpeg/builds/ and extract to `deps\ffmpeg\ffmpeg-9.0-full_build-shared\` (containing `bin\`, `include\`, `lib\`) — see the PostBuildEvent / vcxproj paths

Steps:

1. Open `MusicPlayer.vcxproj` in Visual Studio 2022 (or build from CLI with MSBuild `/p:Configuration=Release /p:Platform=x64`)
2. Build Release|x64
3. Copy `build\MusicPlayer.asi` and the FFmpeg DLLs (auto-copied to `build\` by the post-build step) to your GTA V directory

`deps\` and `build\` are gitignored.

## Requirements (runtime)

- [ScriptHookV](http://www.dev-c.com/gtav/scripthookv/) (copy `ScriptHookV.dll` to GTA V folder)
- Visual C++ Redistributable for VS 2015-2022
