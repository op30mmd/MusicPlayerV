#include "audio.h"
#include "log.h"
#include <cstdio>
#include <cstdarg>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

#pragma comment(lib, "avcodec.lib")
#pragma comment(lib, "avformat.lib")
#pragma comment(lib, "avutil.lib")
#pragma comment(lib, "swresample.lib")

static bool FFmpegDecodeToPCM(const wchar_t* filePath, std::vector<BYTE>& outPcm, WAVEFORMATEX& wfx)
{
	char utf8Path[MAX_PATH * 2];
	WideCharToMultiByte(CP_UTF8, 0, filePath, -1, utf8Path, sizeof(utf8Path), nullptr, nullptr);

	AVFormatContext* fmtCtx = nullptr;
	if (avformat_open_input(&fmtCtx, utf8Path, nullptr, nullptr) != 0)
	{
		LogMessage(L"FFmpeg: avformat_open_input failed path=%s", filePath);
		return false;
	}

	if (avformat_find_stream_info(fmtCtx, nullptr) < 0)
	{
		LogMessage(L"FFmpeg: avformat_find_stream_info failed path=%s", filePath);
		avformat_close_input(&fmtCtx);
		return false;
	}

	int streamIdx = av_find_best_stream(fmtCtx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
	if (streamIdx < 0)
	{
		LogMessage(L"FFmpeg: no audio stream path=%s", filePath);
		avformat_close_input(&fmtCtx);
		return false;
	}

	AVCodecParameters* par = fmtCtx->streams[streamIdx]->codecpar;
	const AVCodec* codec = avcodec_find_decoder(par->codec_id);
	if (!codec)
	{
		LogMessage(L"FFmpeg: no decoder for codec id=%d path=%s", par->codec_id, filePath);
		avformat_close_input(&fmtCtx);
		return false;
	}

	AVCodecContext* codecCtx = avcodec_alloc_context3(codec);
	if (!codecCtx)
	{
		avformat_close_input(&fmtCtx);
		return false;
	}

	if (avcodec_parameters_to_context(codecCtx, par) < 0 || avcodec_open2(codecCtx, codec, nullptr) < 0)
	{
		LogMessage(L"FFmpeg: codec open failed path=%s", filePath);
		avcodec_free_context(&codecCtx);
		avformat_close_input(&fmtCtx);
		return false;
	}

	int outChannels = codecCtx->ch_layout.nb_channels;
	if (outChannels > 2) outChannels = 2;
	if (outChannels <= 0) outChannels = 2;
	int outRate = codecCtx->sample_rate > 0 ? codecCtx->sample_rate : 44100;

	AVChannelLayout outLayout = { 0 };
	av_channel_layout_default(&outLayout, outChannels);

	SwrContext* swr = nullptr;
	if (swr_alloc_set_opts2(&swr, &outLayout, AV_SAMPLE_FMT_S16, outRate,
		&codecCtx->ch_layout, codecCtx->sample_fmt, codecCtx->sample_rate, 0, nullptr) < 0 ||
		!swr || swr_init(swr) < 0)
	{
		LogMessage(L"FFmpeg: swresample init failed path=%s", filePath);
		av_channel_layout_uninit(&outLayout);
		avcodec_free_context(&codecCtx);
		avformat_close_input(&fmtCtx);
		return false;
	}
	av_channel_layout_uninit(&outLayout);

	AVPacket* pkt = av_packet_alloc();
	AVFrame* frame = av_frame_alloc();
	if (!pkt || !frame)
	{
		if (pkt) av_packet_free(&pkt);
		if (frame) av_frame_free(&frame);
		swr_free(&swr);
		avcodec_free_context(&codecCtx);
		avformat_close_input(&fmtCtx);
		return false;
	}

	outPcm.clear();
	uint8_t* outBuf = nullptr;

	while (av_read_frame(fmtCtx, pkt) >= 0)
	{
		if (pkt->stream_index == streamIdx)
		{
			int ret = avcodec_send_packet(codecCtx, pkt);
			while (ret >= 0)
			{
				ret = avcodec_receive_frame(codecCtx, frame);
				if (ret == 0)
				{
					int outSamples = av_samples_alloc(&outBuf, nullptr, outChannels,
						frame->nb_samples, AV_SAMPLE_FMT_S16, 0);
					if (outSamples >= 0)
					{
						int converted = swr_convert(swr, &outBuf, frame->nb_samples,
							(const uint8_t**)frame->extended_data, frame->nb_samples);
						if (converted > 0)
						{
							int byteCount = av_samples_get_buffer_size(nullptr, outChannels,
								converted, AV_SAMPLE_FMT_S16, 1);
							if (byteCount > 0)
							{
								size_t oldSize = outPcm.size();
								outPcm.resize(oldSize + byteCount);
								memcpy(outPcm.data() + oldSize, outBuf, byteCount);
							}
						}
						av_freep(&outBuf);
					}
					av_frame_unref(frame);
				}
			}
		}
		av_packet_unref(pkt);
	}

	av_freep(&outBuf);
	av_frame_free(&frame);
	av_packet_free(&pkt);
	swr_free(&swr);
	avcodec_free_context(&codecCtx);
	avformat_close_input(&fmtCtx);

	if (outPcm.empty())
	{
		LogMessage(L"FFmpeg: decoded no data path=%s", filePath);
		return false;
	}

	wfx.wFormatTag = WAVE_FORMAT_PCM;
	wfx.nChannels = (WORD)outChannels;
	wfx.nSamplesPerSec = outRate;
	wfx.wBitsPerSample = 16;
	wfx.nBlockAlign = (WORD)(outChannels * 2);
	wfx.nAvgBytesPerSec = outRate * outChannels * 2;
	wfx.cbSize = 0;

	LogMessage(L"FFmpeg: OK ch=%d rate=%d bytes=%zu path=%s", outChannels, outRate, outPcm.size(), filePath);
	return true;
}

static bool MFDecodeToPCM(const std::wstring& filePath, std::vector<BYTE>& outPcm, WAVEFORMATEX& outWfx)
{
	outPcm.clear();

	IMFByteStream* pByteStream = nullptr;
	HRESULT hr = MFCreateFile(MF_ACCESSMODE_READ, MF_OPENMODE_FAIL_IF_NOT_EXIST, MF_FILEFLAGS_NONE, filePath.c_str(), &pByteStream);
	if (FAILED(hr) || !pByteStream)
	{
		LogMessage(L"MF: MFCreateFile failed hr=0x%08X", hr);
		return false;
	}

	IMFAttributes* pAttributes = nullptr;
	MFCreateAttributes(&pAttributes, 1);

	IMFSourceReader* pReader = nullptr;
	hr = MFCreateSourceReaderFromByteStream(pByteStream, pAttributes, &pReader);
	pByteStream->Release();
	if (pAttributes) pAttributes->Release();
	if (FAILED(hr) || !pReader)
	{
		LogMessage(L"MF: MFCreateSourceReaderFromByteStream failed hr=0x%08X", hr);
		return false;
	}

	IMFMediaType* pPartialType = nullptr;
	MFCreateMediaType(&pPartialType);
	pPartialType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
	pPartialType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
	hr = pReader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, pPartialType);
	pPartialType->Release();
	if (FAILED(hr))
	{
		LogMessage(L"MF: SetCurrentMediaType failed hr=0x%08X", hr);
		pReader->Release();
		return false;
	}

	IMFMediaType* pCurrentType = nullptr;
	hr = pReader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, &pCurrentType);
	if (FAILED(hr))
	{
		LogMessage(L"MF: GetCurrentMediaType failed hr=0x%08X", hr);
		pReader->Release();
		return false;
	}

	WAVEFORMATEX* pWfx = nullptr;
	UINT32 cbFormat = 0;
	MFCreateWaveFormatExFromMFMediaType(pCurrentType, &pWfx, &cbFormat);
	pCurrentType->Release();
	if (!pWfx)
	{
		LogMessage(L"MF: MFCreateWaveFormatExFromMFMediaType returned NULL");
		pReader->Release();
		return false;
	}
	outWfx = *pWfx;
	CoTaskMemFree(pWfx);

	while (true)
	{
		IMFSample* pSample = nullptr;
		IMFMediaBuffer* pBuffer = nullptr;
		DWORD streamIndex = 0, flags = 0;
		LONGLONG llTimeStamp = 0;

		hr = pReader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, &streamIndex, &flags, &llTimeStamp, &pSample);
		if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM))
		{
			if (pSample) pSample->Release();
			break;
		}

		if (pSample)
		{
			hr = pSample->ConvertToContiguousBuffer(&pBuffer);
			if (SUCCEEDED(hr) && pBuffer)
			{
				BYTE* pAudioData = nullptr;
				DWORD cbBuffer = 0;
				hr = pBuffer->Lock(&pAudioData, nullptr, &cbBuffer);
				if (SUCCEEDED(hr) && pAudioData && cbBuffer > 0)
				{
					size_t oldSize = outPcm.size();
					outPcm.resize(oldSize + cbBuffer);
					memcpy(outPcm.data() + oldSize, pAudioData, cbBuffer);
				}
				pBuffer->Unlock();
				pBuffer->Release();
			}
			pSample->Release();
		}
	}

	pReader->Release();

	if (outPcm.empty())
	{
		LogMessage(L"MF: decoded no PCM data");
		return false;
	}

	LogMessage(L"MF: OK ch=%u rate=%u bytes=%zu", outWfx.nChannels, outWfx.nSamplesPerSec, outPcm.size());
	return true;
}

static bool DecodeFileToPCM(const std::wstring& filePath, std::vector<BYTE>& pcm, WAVEFORMATEX& wfx)
{
	if (FFmpegDecodeToPCM(filePath.c_str(), pcm, wfx)) return true;
	return MFDecodeToPCM(filePath, pcm, wfx);
}

AudioEngine::AudioEngine()
	: m_xaudio2(nullptr), m_masterVoice(nullptr), m_sourceVoice(nullptr),
	m_waveFormat(nullptr),
	m_playbackPos(0), m_streamEnded(false),
	m_currentIndex(-1), m_volume(0.5f), m_isPlaying(false), m_isPaused(false),
	m_shutdown(false), m_decodeState(DECODE_IDLE), m_swapRequested(false), m_failCount(0),
	m_externalPending(false), m_externalResumePos(0)
{
}

AudioEngine::~AudioEngine()
{
	Shutdown();
}

bool AudioEngine::Initialize()
{
	HRESULT hr;

	hr = MFStartup(MF_VERSION);
	if (FAILED(hr)) return false;

	hr = XAudio2Create(&m_xaudio2, 0, XAUDIO2_DEFAULT_PROCESSOR);
	if (FAILED(hr)) return false;

	hr = m_xaudio2->CreateMasteringVoice(&m_masterVoice);
	if (FAILED(hr)) return false;

	m_shutdown = false;
	m_decodeThread = std::thread(&AudioEngine::DecodeThreadMain, this);

	return true;
}

void AudioEngine::Shutdown()
{
	{
		std::lock_guard<std::mutex> lock(m_decodeMutex);
		m_shutdown = true;
		m_pendingPath.clear();
	}
	m_decodeCv.notify_all();
	if (m_decodeThread.joinable()) m_decodeThread.join();

	Stop();

	if (m_sourceVoice)
	{
		m_sourceVoice->DestroyVoice();
		m_sourceVoice = nullptr;
	}

	if (m_waveFormat)
	{
		CoTaskMemFree(m_waveFormat);
		m_waveFormat = nullptr;
	}

	if (m_masterVoice)
	{
		m_masterVoice->DestroyVoice();
		m_masterVoice = nullptr;
	}

	if (m_xaudio2)
	{
		m_xaudio2->Release();
		m_xaudio2 = nullptr;
	}

	MFShutdown();
}

bool AudioEngine::LoadFile(const std::wstring& filePath)
{
	Stop();

	std::vector<BYTE> pcm;
	WAVEFORMATEX wfx = {};
	if (!DecodeFileToPCM(filePath, pcm, wfx)) return false;

	ApplyDecodedBuffer(std::move(pcm), wfx);
	if (!m_sourceVoice) return false;

	m_currentTrackName = filePath;
	m_isPlaying = false;
	m_isPaused = false;

	return true;
}

void AudioEngine::LoadFileAsync(const std::wstring& filePath, size_t resumePos)
{
	std::lock_guard<std::mutex> lock(m_decodeMutex);
	m_externalPath = filePath;
	m_externalResumePos = resumePos;
	m_externalPending = true;
	m_failCount = 0;
	if (m_decodeState == DECODE_READY && m_readyPath == filePath)
	{
		// A preloaded buffer is already cached: swap to it without decoding
		m_swapRequested = true;
	}
	else
	{
		StartLoadLocked(filePath, true);
	}
}

bool AudioEngine::HasExternalTrackPending()
{
	std::lock_guard<std::mutex> lock(m_decodeMutex);
	return m_externalPending;
}

void AudioEngine::CancelExternalLoad()
{
	std::lock_guard<std::mutex> lock(m_decodeMutex);
	if (m_externalPending)
	{
		m_externalPending = false;
		m_swapRequested = false;
	}
}

void AudioEngine::PreloadPath(const std::wstring& path)
{
	std::lock_guard<std::mutex> lock(m_decodeMutex);
	StartLoadLocked(path, false);
}

bool AudioEngine::IsReadyPath(const std::wstring& path)
{
	std::lock_guard<std::mutex> lock(m_decodeMutex);
	return m_decodeState == DECODE_READY && m_readyPath == path;
}

void AudioEngine::ApplyDecodedBuffer(std::vector<BYTE>&& pcm, const WAVEFORMATEX& wfx)
{
	if (m_sourceVoice)
	{
		m_sourceVoice->Stop(0);
		m_sourceVoice->FlushSourceBuffers();
		m_sourceVoice->DestroyVoice();
		m_sourceVoice = nullptr;
	}

	if (m_waveFormat)
	{
		CoTaskMemFree(m_waveFormat);
		m_waveFormat = nullptr;
	}

	m_pcmData = std::move(pcm);
	m_playbackPos = 0;
	m_streamEnded = false;
	m_isPlaying = false;
	m_isPaused = false;

	m_waveFormat = (WAVEFORMATEX*)CoTaskMemAlloc(sizeof(WAVEFORMATEX));
	if (!m_waveFormat) return;
	memcpy(m_waveFormat, &wfx, sizeof(WAVEFORMATEX));

	HRESULT hr = m_xaudio2->CreateSourceVoice(&m_sourceVoice, m_waveFormat, 0, XAUDIO2_DEFAULT_FREQ_RATIO);
	if (FAILED(hr))
	{
		LogMessage(L"ApplyDecodedBuffer: CreateSourceVoice failed hr=0x%08X", hr);
		m_sourceVoice = nullptr;
	}
	else
	{
		// a new voice always starts at full volume; keep the user's setting
		m_sourceVoice->SetVolume(m_volume);
	}
}

void AudioEngine::Play()
{
	if (!m_sourceVoice) return;

	if (m_isPaused)
	{
		m_sourceVoice->Start(0);
		m_isPaused = false;
		m_isPlaying = true;
		return;
	}

	if (m_isPlaying) return;

	if (m_streamEnded)
	{
		// track already finished: restart it from the beginning
		m_sourceVoice->Stop(0);
		m_sourceVoice->FlushSourceBuffers();
		m_playbackPos = 0;
		m_streamEnded = false;

		size_t chunk = min((size_t)CHUNK_BYTES, m_pcmData.size());
		if (chunk > 0)
		{
			XAUDIO2_BUFFER buf = {};
			buf.AudioBytes = (UINT32)chunk;
			buf.pAudioData = m_pcmData.data();
			m_sourceVoice->SubmitSourceBuffer(&buf);
			m_playbackPos = chunk;
		}

		m_sourceVoice->Start(0);
		m_isPlaying = true;
		return;
	}

	m_sourceVoice->Start(0);
	m_isPlaying = true;
}

void AudioEngine::Pause()
{
	if (m_sourceVoice && m_isPlaying)
	{
		m_sourceVoice->Stop(0);
		m_isPaused = true;
		m_isPlaying = false;
	}
}

void AudioEngine::Stop()
{
	if (m_sourceVoice)
	{
		m_sourceVoice->Stop(0);
		m_sourceVoice->FlushSourceBuffers();
	}
	m_playbackPos = 0;
	m_isPlaying = false;
	m_isPaused = false;
	m_streamEnded = false;
}

bool AudioEngine::HasPendingTrackChange()
{
	std::lock_guard<std::mutex> lock(m_decodeMutex);
	return m_swapRequested || m_decodeState == DECODE_LOADING;
}

void AudioEngine::SetVolume(float volume)
{
	m_volume = max(0.0f, min(1.0f, volume));
	if (m_sourceVoice)
	{
		m_sourceVoice->SetVolume(m_volume);
	}
}

void AudioEngine::Update()
{
	ProcessAsyncDecode();

	if (!m_isPlaying || !m_sourceVoice) return;

	if (m_playbackPos >= m_pcmData.size())
	{
		XAUDIO2_VOICE_STATE state;
		m_sourceVoice->GetState(&state);
		if (state.BuffersQueued == 0)
		{
			m_isPlaying = false;
			m_streamEnded = true;
		}
		return;
	}

	XAUDIO2_VOICE_STATE state;
	m_sourceVoice->GetState(&state);

	while (state.BuffersQueued < MAX_BUFFER_COUNT && m_playbackPos < m_pcmData.size())
	{
		size_t chunk = min((size_t)CHUNK_BYTES, m_pcmData.size() - m_playbackPos);

		XAUDIO2_BUFFER buf = {};
		buf.AudioBytes = (UINT32)chunk;
		buf.pAudioData = m_pcmData.data() + m_playbackPos;
		m_sourceVoice->SubmitSourceBuffer(&buf);
		m_playbackPos += chunk;

		m_sourceVoice->GetState(&state);
	}
}

void AudioEngine::StartLoadLocked(const std::wstring& path, bool swapWhenReady)
{
	if (m_decodeState == DECODE_READY)
	{
		m_readyPcm.clear();
		m_readyPath.clear();
	}
	m_pendingPath = path;
	m_swapRequested = swapWhenReady;
	if (m_decodeState != DECODE_LOADING)
	{
		m_decodeState = DECODE_IDLE;
		m_decodeCv.notify_all();
	}
}

void AudioEngine::StartLoad(const std::wstring& path, bool swapWhenReady)
{
	std::lock_guard<std::mutex> lock(m_decodeMutex);
	StartLoadLocked(path, swapWhenReady);
}

void AudioEngine::NextTrack()
{
	if (m_playQueue.empty()) return;

	m_currentIndex = (m_currentIndex + 1) % (int)m_playQueue.size();
	const std::wstring& path = m_playQueue[m_currentIndex];

	std::lock_guard<std::mutex> lock(m_decodeMutex);
	m_failCount = 0;
	if (m_decodeState == DECODE_READY && m_readyPath == path)
	{
		m_swapRequested = true;
	}
	else
	{
		StartLoadLocked(path, true);
	}
	m_currentTrackName = path;
}

void AudioEngine::PreviousTrack()
{
	if (m_playQueue.empty()) return;

	m_currentIndex = (m_currentIndex - 1 + (int)m_playQueue.size()) % (int)m_playQueue.size();
	const std::wstring& path = m_playQueue[m_currentIndex];

	std::lock_guard<std::mutex> lock(m_decodeMutex);
	m_failCount = 0;
	if (m_decodeState == DECODE_READY && m_readyPath == path)
	{
		m_swapRequested = true;
	}
	else
	{
		StartLoadLocked(path, true);
	}
	m_currentTrackName = path;
}

void AudioEngine::PlayQueue()
{
	if (m_playQueue.empty()) return;

	const std::wstring& path = m_playQueue[m_currentIndex];
	std::lock_guard<std::mutex> lock(m_decodeMutex);
	m_failCount = 0;
	StartLoadLocked(path, true);
	m_currentTrackName = path;
}

void AudioEngine::PreloadNextTrack()
{
	if (m_playQueue.empty()) return;

	int next = (m_currentIndex + 1) % (int)m_playQueue.size();
	StartLoad(m_playQueue[next], false);
}

void AudioEngine::DecodeThreadMain()
{
	std::unique_lock<std::mutex> lock(m_decodeMutex);

	while (true)
	{
		m_decodeCv.wait(lock, [this] { return m_shutdown || !m_pendingPath.empty(); });
		if (m_shutdown) return;

		std::wstring path = m_pendingPath;
		m_decodeState = DECODE_LOADING;
		lock.unlock();

		std::vector<BYTE> pcm;
		WAVEFORMATEX wfx = {};
		bool ok = DecodeFileToPCM(path, pcm, wfx);

		lock.lock();
		if (m_pendingPath == path)
		{
			m_pendingPath.clear();
			if (ok)
			{
				m_readyPcm = std::move(pcm);
				m_readyWfx = wfx;
				m_readyPath = path;
				m_decodeState = DECODE_READY;
			}
			else
			{
				m_decodeState = DECODE_FAILED;
			}
		}
		else
		{
			m_decodeState = DECODE_IDLE;
		}
	}
}

void AudioEngine::ProcessAsyncDecode()
{
	std::unique_lock<std::mutex> lock(m_decodeMutex);

	if (m_decodeState == DECODE_READY && m_swapRequested)
	{
		std::vector<BYTE> pcm = std::move(m_readyPcm);
		WAVEFORMATEX wfx = m_readyWfx;
		std::wstring path = m_readyPath;
		m_readyPath.clear();
		m_swapRequested = false;
		m_decodeState = DECODE_IDLE;
		m_failCount = 0;
		bool resumePlayback = !m_isPaused;
		bool isExternal = m_externalPending && path == m_externalPath;
		if (isExternal) m_externalPending = false;
		lock.unlock();

		LogMessage(L"Audio: applying decoded track %s", path.c_str());
		ApplyDecodedBuffer(std::move(pcm), wfx);
		m_currentTrackName = path;

		if (isExternal)
		{
			size_t pos = m_externalResumePos;
			m_externalResumePos = 0;
			if (m_sourceVoice && pos > 0 && pos < m_pcmData.size())
			{
				m_playbackPos = pos;
				size_t chunk = min((size_t)CHUNK_BYTES, m_pcmData.size() - m_playbackPos);
				XAUDIO2_BUFFER buf = {};
				buf.AudioBytes = (UINT32)chunk;
				buf.pAudioData = m_pcmData.data() + m_playbackPos;
				m_sourceVoice->SubmitSourceBuffer(&buf);
				m_playbackPos += chunk;
			}
			if (resumePlayback) Play();
			return;
		}

		if (resumePlayback) Play();
		PreloadNextTrack();
		return;
	}

	if (m_decodeState == DECODE_FAILED && m_swapRequested)
	{
		if (m_externalPending)
		{
			// A queue/resume load failed: keep whatever is playing and let
			// the script skip to the next entry. Never touch the library
			// queue for external loads.
			m_externalPending = false;
			m_swapRequested = false;
			m_decodeState = DECODE_IDLE;
			m_failCount = 0;
			lock.unlock();
			LogMessage(L"Audio: external track load failed, keeping current track");
			return;
		}

		if (m_isPaused)
		{
			lock.unlock();
			return;
		}

		m_failCount++;
		m_decodeState = DECODE_IDLE;
		if (m_failCount >= (int)m_playQueue.size())
		{
			m_swapRequested = false;
			lock.unlock();
			Stop();
			LogMessage(L"Audio: no playable track found after %d attempts", m_failCount);
			return;
		}

		m_currentIndex = (m_currentIndex + 1) % (int)m_playQueue.size();
		std::wstring path = m_playQueue[m_currentIndex];
		StartLoadLocked(path, true);
		m_currentTrackName = path;
		lock.unlock();
		LogMessage(L"Audio: decode failed, trying next %s", path.c_str());
	}
}

void AudioEngine::SetPlayQueue(std::wstring* tracks, int count)
{
	m_playQueue.clear();
	for (int i = 0; i < count; i++) m_playQueue.push_back(tracks[i]);
	m_currentIndex = 0;
}
