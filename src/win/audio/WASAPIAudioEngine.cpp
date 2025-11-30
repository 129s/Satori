#include "win/audio/WASAPIAudioEngine.h"

#include <algorithm>
#include <chrono>
#include <thread>

namespace winaudio {

namespace {
WAVEFORMATEX BuildWaveFormat(const AudioEngineConfig& config) {
    WAVEFORMATEX fmt{};
    fmt.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    fmt.nChannels = config.channels;
    fmt.nSamplesPerSec = config.sampleRate;
    fmt.wBitsPerSample = 32;
    fmt.nBlockAlign = (fmt.wBitsPerSample / 8) * fmt.nChannels;
    fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;
    return fmt;
}

}  // namespace

WASAPIAudioEngine::WASAPIAudioEngine(AudioEngineConfig config)
    : config_(config) {}

WASAPIAudioEngine::~WASAPIAudioEngine() {
    shutdown();
}

bool WASAPIAudioEngine::initialize(RenderCallback callback) {
    if (initialized_) {
        return true;
    }
    renderCallback_ = std::move(callback);
    if (!createDevice() || !createClient() || !createRenderClient() ||
        !createEventHandle()) {
        shutdown();
        return false;
    }
    initialized_ = configureEngine(renderCallback_);
    return initialized_;
}

void WASAPIAudioEngine::shutdown() {
    stop();
    if (audioEvent_) {
        CloseHandle(audioEvent_);
        audioEvent_ = nullptr;
    }
    renderClient_.Reset();
    audioClient_.Reset();
    device_.Reset();
    enumerator_.Reset();
    initialized_ = false;
}

bool WASAPIAudioEngine::start() {
    if (!initialized_ || running_) {
        return false;
    }
    running_ = true;
    renderThread_ = std::make_unique<std::thread>(&WASAPIAudioEngine::renderLoop, this);
    return true;
}

void WASAPIAudioEngine::stop() {
    if (!running_) {
        return;
    }
    running_ = false;
    if (renderThread_ && renderThread_->joinable()) {
        renderThread_->join();
    }
    renderThread_.reset();
    if (audioClient_) {
        audioClient_->Stop();
    }
}

bool WASAPIAudioEngine::createDevice() {
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  IID_PPV_ARGS(&enumerator_));
    if (FAILED(hr)) {
        return false;
    }
    hr = enumerator_->GetDefaultAudioEndpoint(eRender, eConsole, &device_);
    return SUCCEEDED(hr);
}

bool WASAPIAudioEngine::createClient() {
    if (!device_) {
        return false;
    }
    HRESULT hr = device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                   &audioClient_);
    return SUCCEEDED(hr);
}

bool WASAPIAudioEngine::createRenderClient() {
    if (!audioClient_) {
        return false;
    }
    HRESULT hr = audioClient_->GetService(__uuidof(IAudioRenderClient),
                                          &renderClient_);
    return SUCCEEDED(hr);
}

bool WASAPIAudioEngine::createEventHandle() {
    audioEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    return audioEvent_ != nullptr;
}

bool WASAPIAudioEngine::configureEngine(RenderCallback callback) {
    if (!audioClient_ || !renderClient_ || !audioEvent_) {
        return false;
    }
    const auto waveFormat = BuildWaveFormat(config_);
    REFERENCE_TIME bufferDuration = static_cast<REFERENCE_TIME>(
        10000000LL * config_.bufferFrames / config_.sampleRate);

    HRESULT hr = audioClient_->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                          AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                          bufferDuration, 0, &waveFormat, nullptr);
    if (FAILED(hr)) {
        return false;
    }
    hr = audioClient_->SetEventHandle(audioEvent_);
    if (FAILED(hr)) {
        return false;
    }
    UINT32 bufferFrameCount = 0;
    hr = audioClient_->GetBufferSize(&bufferFrameCount);
    if (FAILED(hr)) {
        return false;
    }
    config_.bufferFrames = bufferFrameCount;
    // 预填充静音
    BYTE* data = nullptr;
    hr = renderClient_->GetBuffer(bufferFrameCount, &data);
    if (FAILED(hr)) {
        return false;
    }
    std::fill_n(reinterpret_cast<float*>(data),
                bufferFrameCount * config_.channels, 0.0f);
    renderClient_->ReleaseBuffer(bufferFrameCount, 0);
    return true;
}

void WASAPIAudioEngine::renderLoop() {
    HRESULT hr = audioClient_->Start();
    if (FAILED(hr)) {
        running_ = false;
        return;
    }
    const UINT32 channels = config_.channels;
    const UINT32 bytesPerFrame = channels * sizeof(float);
    while (running_) {
        DWORD waitResult = WaitForSingleObject(audioEvent_, 2000);
        if (waitResult != WAIT_OBJECT_0) {
            running_ = false;
            break;
        }
        UINT32 padding = 0;
        hr = audioClient_->GetCurrentPadding(&padding);
        if (FAILED(hr)) {
            running_ = false;
            break;
        }
        const UINT32 framesAvailable = config_.bufferFrames - padding;
        if (framesAvailable == 0) {
            continue;
        }
        BYTE* data = nullptr;
        hr = renderClient_->GetBuffer(framesAvailable, &data);
        if (FAILED(hr)) {
            running_ = false;
            break;
        }
        float* samples = reinterpret_cast<float*>(data);
        if (renderCallback_) {
            renderCallback_(samples, framesAvailable);
        } else {
            std::fill_n(samples, framesAvailable * channels, 0.0f);
        }
        hr = renderClient_->ReleaseBuffer(framesAvailable, 0);
        if (FAILED(hr)) {
            running_ = false;
            break;
        }
    }
    audioClient_->Stop();
}

}  // namespace winaudio
