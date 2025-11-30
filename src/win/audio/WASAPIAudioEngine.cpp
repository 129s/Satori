#include "win/audio/WASAPIAudioEngine.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <sstream>
#include <thread>

namespace winaudio {

namespace {

void LogHResult(const char* stage, HRESULT hr) {
    std::ostringstream oss;
    oss << "[WASAPI] " << stage << " failed: 0x" << std::hex << hr << std::dec << "\n";
    const auto message = oss.str();
    OutputDebugStringA(message.c_str());
    std::cerr << message;
}

class MixFormatHolder {
public:
    MixFormatHolder() = default;
    ~MixFormatHolder() {
        if (format_) {
            CoTaskMemFree(format_);
        }
    }
    WAVEFORMATEX** operator&() { return &format_; }
    WAVEFORMATEX* get() const { return format_; }
    WAVEFORMATEX* release() {
        auto* tmp = format_;
        format_ = nullptr;
        return tmp;
    }

private:
    WAVEFORMATEX* format_ = nullptr;
};

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
    if (!createDevice() || !createClient() || !createEventHandle()) {
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
        LogHResult("CoCreateInstance(MMDeviceEnumerator)", hr);
        return false;
    }
    hr = enumerator_->GetDefaultAudioEndpoint(eRender, eConsole, &device_);
    if (FAILED(hr)) {
        LogHResult("GetDefaultAudioEndpoint", hr);
        return false;
    }
    return true;
}

bool WASAPIAudioEngine::createClient() {
    if (!device_) {
        return false;
    }
    HRESULT hr = device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                   &audioClient_);
    if (FAILED(hr)) {
        LogHResult("Activate(IAudioClient)", hr);
        return false;
    }
    return true;
}

bool WASAPIAudioEngine::createRenderClient() {
    if (!audioClient_) {
        return false;
    }
    renderClient_.Reset();
    HRESULT hr = audioClient_->GetService(__uuidof(IAudioRenderClient),
                                          &renderClient_);
    if (FAILED(hr)) {
        LogHResult("GetService(IAudioRenderClient)", hr);
        return false;
    }
    return true;
}

bool WASAPIAudioEngine::createEventHandle() {
    audioEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!audioEvent_) {
        LogHResult("CreateEvent", HRESULT_FROM_WIN32(GetLastError()));
        return false;
    }
    return true;
}

bool WASAPIAudioEngine::configureEngine(RenderCallback callback) {
    if (!audioClient_ || !audioEvent_) {
        return false;
    }

    MixFormatHolder mixFormat;
    HRESULT hr = audioClient_->GetMixFormat(&mixFormat);
    if (FAILED(hr) || mixFormat.get() == nullptr) {
        LogHResult("IAudioClient::GetMixFormat", hr);
        return false;
    }
    config_.sampleRate = mixFormat.get()->nSamplesPerSec;
    config_.channels = mixFormat.get()->nChannels;

    REFERENCE_TIME bufferDuration = static_cast<REFERENCE_TIME>(
        10000000LL * config_.bufferFrames / config_.sampleRate);

    hr = audioClient_->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                  AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                  bufferDuration, 0, mixFormat.get(), nullptr);
    if (FAILED(hr)) {
        LogHResult("IAudioClient::Initialize", hr);
        return false;
    }
    if (!createRenderClient()) {
        return false;
    }
    hr = audioClient_->SetEventHandle(audioEvent_);
    if (FAILED(hr)) {
        LogHResult("IAudioClient::SetEventHandle", hr);
        return false;
    }
    UINT32 bufferFrameCount = 0;
    hr = audioClient_->GetBufferSize(&bufferFrameCount);
    if (FAILED(hr)) {
        LogHResult("IAudioClient::GetBufferSize", hr);
        return false;
    }
    config_.bufferFrames = bufferFrameCount;
    // 预填充静音
    BYTE* data = nullptr;
    hr = renderClient_->GetBuffer(bufferFrameCount, &data);
    if (FAILED(hr)) {
        LogHResult("IAudioRenderClient::GetBuffer (prime)", hr);
        return false;
    }
    std::fill_n(reinterpret_cast<float*>(data),
                bufferFrameCount * config_.channels, 0.0f);
    hr = renderClient_->ReleaseBuffer(bufferFrameCount, 0);
    if (FAILED(hr)) {
        LogHResult("IAudioRenderClient::ReleaseBuffer (prime)", hr);
        return false;
    }
    return true;
}

void WASAPIAudioEngine::renderLoop() {
    const HRESULT initHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool comInitialized = SUCCEEDED(initHr);

    HRESULT hr = audioClient_->Start();
    if (FAILED(hr)) {
        LogHResult("IAudioClient::Start", hr);
        running_ = false;
        if (comInitialized) {
            CoUninitialize();
        }
        return;
    }
    const UINT32 channels = config_.channels;
    const UINT32 bytesPerFrame = channels * sizeof(float);
    while (running_) {
        DWORD waitResult = WaitForSingleObject(audioEvent_, 2000);
        if (waitResult != WAIT_OBJECT_0) {
            LogHResult("WaitForSingleObject", HRESULT_FROM_WIN32(GetLastError()));
            running_ = false;
            break;
        }
        UINT32 padding = 0;
        hr = audioClient_->GetCurrentPadding(&padding);
        if (FAILED(hr)) {
            LogHResult("IAudioClient::GetCurrentPadding", hr);
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
            LogHResult("IAudioRenderClient::GetBuffer", hr);
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
            LogHResult("IAudioRenderClient::ReleaseBuffer", hr);
            running_ = false;
            break;
        }
    }
    audioClient_->Stop();
    if (comInitialized) {
        CoUninitialize();
    }
}

}  // namespace winaudio
