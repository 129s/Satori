#pragma once

#include <memory>
#include <string>
#include <vector>

#include "win/audio/AudioEngineTypes.h"
#include "win/audio/AsioAudioEngine.h"
#include "win/audio/WASAPIAudioEngine.h"

namespace winaudio {

// Unified backend selector for WASAPI(shared) and ASIO.
class UnifiedAudioEngine {
public:
    explicit UnifiedAudioEngine(AudioEngineConfig config = {});
    ~UnifiedAudioEngine();

    bool initialize(RenderCallback callback);
    bool reinitialize(AudioEngineConfig config, RenderCallback callback);
    void shutdown();

    bool start();
    void stop();

    bool isRunning() const;
    const AudioEngineConfig& config() const;
    const std::string& lastError() const;

    static std::vector<AudioDeviceInfo> EnumerateDevices();

private:
    AudioBackendType backend_ = AudioBackendType::WasapiShared;
    std::unique_ptr<WASAPIAudioEngine> wasapi_;
    std::unique_ptr<AsioAudioEngine> asio_;
};

}  // namespace winaudio

