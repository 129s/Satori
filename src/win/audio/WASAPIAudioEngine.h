#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <windows.h>

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

namespace winaudio {

/// Audio render callback. Caller fills stereo/mono float buffer.
using RenderCallback = std::function<void(float* output, std::size_t frames)>;

struct AudioEngineConfig {
    uint32_t sampleRate = 44100;
    uint16_t channels = 1;
    uint32_t bufferFrames = 512;
};

class WASAPIAudioEngine {
public:
    explicit WASAPIAudioEngine(AudioEngineConfig config = {});
    ~WASAPIAudioEngine();

    bool initialize(RenderCallback callback);
    void shutdown();

    bool start();
    void stop();

    bool isRunning() const { return running_; }
    const AudioEngineConfig& config() const { return config_; }
    const std::string& lastError() const { return lastError_; }

private:
    bool createDevice();
    bool createClient();
    bool createRenderClient();
    bool createEventHandle();
    bool configureEngine(RenderCallback callback);
    void renderLoop();
    void setLastError(const std::string& message);

    AudioEngineConfig config_;
    RenderCallback renderCallback_;
    std::string lastError_;

    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator_;
    Microsoft::WRL::ComPtr<IMMDevice> device_;
    Microsoft::WRL::ComPtr<IAudioClient> audioClient_;
    Microsoft::WRL::ComPtr<IAudioRenderClient> renderClient_;
    HANDLE audioEvent_ = nullptr;

    std::unique_ptr<std::thread> renderThread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> initialized_{false};
};

}  // namespace winaudio
