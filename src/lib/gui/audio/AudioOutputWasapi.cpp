/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "AudioOutput.h"

#include "AudioProtocol.h"

#include <windows.h>

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <mmsystem.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstring>

namespace deskflow::gui::audio {

namespace {

using Microsoft::WRL::ComPtr;

constexpr REFERENCE_TIME kReferenceTimePerSecond = 10'000'000;

class ComApartment
{
public:
  ComApartment() : m_initialized(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
  {
  }

  ~ComApartment()
  {
    if (m_initialized)
      CoUninitialize();
  }

  ComApartment(const ComApartment &) = delete;
  ComApartment &operator=(const ComApartment &) = delete;

private:
  bool m_initialized;
};

class WasapiAudioOutput : public AudioOutput
{
public:
  ~WasapiAudioOutput() override
  {
    if (m_client)
      m_client->Stop();
  }

  bool open(const QAudioDevice &device, qsizetype capacity)
  {
    const auto id = QString::fromUtf8(device.id()).toStdWString();
    const WAVEFORMATEX format{
        .wFormatTag = WAVE_FORMAT_PCM,
        .nChannels = kChannels,
        .nSamplesPerSec = kSampleRate,
        .nAvgBytesPerSec = kSampleRate * kBytesPerFrame,
        .nBlockAlign = kBytesPerFrame,
        .wBitsPerSample = 16,
        .cbSize = 0
    };
    const auto duration = REFERENCE_TIME(capacity / kBytesPerFrame) * kReferenceTimePerSecond / kSampleRate;

    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(
            CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(enumerator.GetAddressOf()))
        ))
      return false;
    ComPtr<IMMDevice> endpoint;
    if (FAILED(enumerator->GetDevice(id.c_str(), endpoint.GetAddressOf())))
      return false;
    ComPtr<IAudioClient> client;
    if (FAILED(endpoint->Activate(
            __uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void **>(client.GetAddressOf())
        )))
      return false;
    // Polled, so the buffer holds only audio we wrote and new audio never waits behind silence.
    const DWORD flags = AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
    if (FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, duration, 0, &format, nullptr)) ||
        FAILED(client->GetBufferSize(&m_bufferFrames)) ||
        FAILED(client->GetService(IID_PPV_ARGS(m_render.GetAddressOf()))) || FAILED(client->Start()))
      return false;
    m_client = std::move(client);
    return true;
  }

  qsizetype queuedBytes() const override
  {
    UINT32 padding = 0;
    // A lost device reads as full, so the engine drops audio and reopens it after a pause, like QAudioSink.
    if (FAILED(m_client->GetCurrentPadding(&padding)))
      return qsizetype(m_bufferFrames) * kBytesPerFrame;
    return qsizetype(padding) * kBytesPerFrame;
  }

  qsizetype write(QByteArrayView pcm) override
  {
    UINT32 padding = 0;
    if (FAILED(m_client->GetCurrentPadding(&padding)))
      return 0;
    const auto frames = UINT32(std::min<qsizetype>(m_bufferFrames - padding, pcm.size() / kBytesPerFrame));
    if (frames == 0)
      return 0;
    BYTE *buffer = nullptr;
    if (FAILED(m_render->GetBuffer(frames, &buffer)))
      return 0;
    const auto bytes = qsizetype(frames) * kBytesPerFrame;
    std::memcpy(buffer, pcm.data(), bytes);
    if (FAILED(m_render->ReleaseBuffer(frames, 0)))
      return 0;
    return bytes;
  }

private:
  ComApartment m_com;
  ComPtr<IAudioClient> m_client;
  ComPtr<IAudioRenderClient> m_render;
  UINT32 m_bufferFrames = 0;
};

} // namespace

std::unique_ptr<AudioOutput> openAudioOutput(const QAudioDevice &device, qsizetype capacity)
{
  auto output = std::make_unique<WasapiAudioOutput>();
  if (!output->open(device, capacity))
    return nullptr;
  return output;
}

} // namespace deskflow::gui::audio
