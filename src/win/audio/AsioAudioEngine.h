#pragma once

#include <string>
#include <memory>
#include <vector>

#include "win/audio/AudioEngineTypes.h"

namespace winaudio {

// ASIO backend. The functional implementation requires building with
// `SATORI_ENABLE_ASIO` and providing the Steinberg ASIO SDK.
class AsioAudioEngine {
public:
    explicit AsioAudioEngine(AudioEngineConfig config = {});
    ~AsioAudioEngine();

    bool initialize(RenderCallback callback);
    bool reinitialize(AudioEngineConfig config, RenderCallback callback);
    void shutdown();

    bool start();
    void stop();

    bool isRunning() const { return running_; }
    const AudioEngineConfig& config() const { return config_; }
    const std::string& lastError() const { return lastError_; }

    static std::vector<AudioDeviceInfo> EnumerateAsioDevices();

private:
    struct Impl;

    void setLastError(const std::string& message);

    AudioEngineConfig config_;
    RenderCallback renderCallback_;
    std::string lastError_;
    bool running_ = false;
    bool initialized_ = false;
    std::unique_ptr<Impl> impl_;
};

}  // namespace winaudio
