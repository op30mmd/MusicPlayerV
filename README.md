# MusicPlayer - GTA V Script Hook V Mod

An in-game music player using XAudio2 for playback. Plays MP3, WAV, FLAC, OGG, WMA, AAC, M4A files.

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

## Configuration

Edit `MusicPlayer.ini` in your GTA V folder:

```ini
[MusicPlayer]
MusicDirectory=C:\Users\YourName\Music
Volume=50
Shuffle=0
ShowUI=1
```

## Building

1. Open `MusicPlayer.vcxproj` in Visual Studio 2022
2. Build in Release|x64
3. Copy `build/MusicPlayer.asi` to your GTA V directory

## Requirements

- [ScriptHookV](http://www.dev-c.com/gtav/scripthookv/) (copy `ScriptHookV.dll` to GTA V folder)
- Visual C++ Redistributable for VS 2015-2022
