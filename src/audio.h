#pragma once

#include <windows.h>
#include <xaudio2.h>
#include <mfidl.h>
#include <mfapi.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>

#pragma comment(lib, "xaudio2.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

enum DecodeState
{
	DECODE_IDLE = 0,
	DECODE_LOADING,
	DECODE_READY,
	DECODE_FAILED
};

class AudioEngine
{
public:
	AudioEngine();
	~AudioEngine();

	bool Initialize();
	void Shutdown();

	bool LoadFile(const std::wstring& filePath);
	void Play();
	void Pause();
	void Stop();
	void SetVolume(float volume);
	float GetVolume() const { return m_volume; }

	void Update();

	void NextTrack();
	void PreviousTrack();
	void PlayQueue();
	void PreloadNextTrack();
	void SetPlayQueue(std::wstring* tracks, int count);
	int GetCurrentIndex() const { return m_currentIndex; }
	void SetCurrentIndex(int idx) { m_currentIndex = idx; }

	std::wstring GetCurrentTrackName() const { return m_currentTrackName; }
	bool IsPlaying() const { return m_isPlaying; }
	bool HasEnded() const { return m_streamEnded && !m_isPlaying; }
	bool HasPendingTrackChange();
	size_t GetPlaybackPos() const { return m_playbackPos; }

	// Load a track outside the library queue through the background decode
	// thread (no main-thread freeze). If resumePos is set, playback starts
	// from that byte offset into the decoded PCM. Success/failure is detected
	// via GetCurrentTrackName() once HasExternalTrackPending() clears.
	void LoadFileAsync(const std::wstring& filePath, size_t resumePos = 0);
	bool HasExternalTrackPending();
	void CancelExternalLoad();
	// Decode into the ready buffer without swapping (used to cache the next
	// queued track ahead of time); LoadFileAsync then reuses that buffer.
	void PreloadPath(const std::wstring& path);
	bool IsReadyPath(const std::wstring& path);

private:
	void ApplyDecodedBuffer(std::vector<BYTE>&& pcm, const WAVEFORMATEX& wfx);
	void DecodeThreadMain();
	void StartLoad(const std::wstring& path, bool swapWhenReady);
	void StartLoadLocked(const std::wstring& path, bool swapWhenReady);
	void ProcessAsyncDecode();

	IXAudio2* m_xaudio2;
	IXAudio2MasteringVoice* m_masterVoice;
	IXAudio2SourceVoice* m_sourceVoice;

	WAVEFORMATEX* m_waveFormat;

	std::vector<BYTE> m_pcmData;
	size_t m_playbackPos;
	bool m_streamEnded;

	std::vector<std::wstring> m_playQueue;
	int m_currentIndex;
	std::wstring m_currentTrackName;

	float m_volume;
	bool m_isPlaying;
	bool m_isPaused;

	std::thread m_decodeThread;
	std::mutex m_decodeMutex;
	std::condition_variable m_decodeCv;
	bool m_shutdown;
	std::wstring m_pendingPath;
	int m_decodeState;
	bool m_swapRequested;
	int m_failCount;
	std::vector<BYTE> m_readyPcm;
	WAVEFORMATEX m_readyWfx;
	std::wstring m_readyPath;

	bool m_externalPending;
	std::wstring m_externalPath;
	size_t m_externalResumePos;

	static const UINT32 MAX_BUFFER_COUNT = 4;
	static const UINT32 CHUNK_BYTES = 65536;
};
