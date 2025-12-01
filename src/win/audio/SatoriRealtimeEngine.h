#pragma once

#include <mutex>
#include <string>

#include "synthesis/KarplusStrongString.h"
#include "win/audio/RealtimeSynthRenderer.h"
#include "win/audio/WASAPIAudioEngine.h"

namespace winaudio {

class SatoriRealtimeEngine {
public:
    SatoriRealtimeEngine();
    ~SatoriRealtimeEngine();

    bool initialize();
    void shutdown();

    bool start();
    void stop();

    void triggerNote(double frequency, double durationSeconds);
    void setSynthConfig(const synthesis::StringConfig& config);
    const synthesis::StringConfig& synthConfig() const { return synthConfig_; }
    const std::string& lastError() const { return audioEngine_.lastError(); }

private:
    void handleRender(float* output, std::size_t frames);

    AudioEngineConfig audioConfig_;
    synthesis::StringConfig synthConfig_;

    WASAPIAudioEngine audioEngine_;
    RealtimeSynthRenderer renderer_;
};

}  // namespace winaudio
