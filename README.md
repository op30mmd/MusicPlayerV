# MusicPlayer - GTA V Script Hook V Mod

An in-game music player for GTA V. Decodes MP3, WAV, FLAC, OGG, WMA, AAC, M4A files with FFmpeg (Media Foundation fallback) and plays them through XAudio2. Decoding runs on a background thread with lookahead preloading, so track switches are instant.

## Features

- Full-screen-transparent menu UI with keyboard navigation (F8 to toggle)
- Instant track switching (next track pre-decoded in the background while the current one plays)
- Auto-advance to the next track on natural end; unsupported/corrupt tracks are skipped automatically
- Vehicle radio is forced off automatically while music is playing (restored when playback stops)
- Phone / arrow-key game controls are blocked while the menu is visible
- Playback survives menu open/close; volume, shuffle and UI state persist in `MusicPlayer.ini`

## Controls

| Key | Action |
|-----|--------|
| F5 | Reload music library / Initialize |
| F6 / Media Previous | Previous track |
| F7 / Media Next | Next track |
| F9 / Media Play-Pause | Play / Pause |
| F10 | Volume Down |
| F11 | Volume Up |
| F8 | Toggle UI display |
| Arrow Up / Down | Navigate menu (menu visible) |
| Enter / Space | Activate menu item (menu visible) |
| Arrow Left / Right | Volume down / up (menu visible) |

When the menu is hidden, all arrow/Enter/Space keys belong to the game.

## Configuration

Edit `MusicPlayer.ini` in your GTA V folder:

```ini
[MusicPlayer]
MusicDirectory=C:\Users\YourName\Music
Volume=50
Shuffle=0
ShowUI=1
```

If `MusicDirectory` is empty, the Windows Music library folder is used.

## Installation

Copy to your GTA V directory (next to `ScriptHookV.dll`):

- `MusicPlayer.asi`
- `avcodec-63.dll`, `avformat-63.dll`, `avutil-61.dll`, `swresample-7.dll` (FFmpeg shared libs, from `build\`)

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
