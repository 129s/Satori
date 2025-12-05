#pragma once

#include <string>

#include "engine/StringSynthEngine.h"
#include "synthesis/KarplusStrongString.h"
#include "win/audio/WASAPIAudioEngine.h"

namespace winaudio {

inline constexpr double kDefaultNoteDurationSeconds = 2.0;

class SatoriRealtimeEngine {
public:
    SatoriRealtimeEngine();
    ~SatoriRealtimeEngine();

    bool initialize();
    void shutdown();

    bool start();
    void stop();

    void triggerNote(double frequency,
                     double durationSeconds = kDefaultNoteDurationSeconds);
    void noteOn(int midiNote, double frequency, float velocity = 1.0f);
    void noteOff(int midiNote);
    void setSynthConfig(const synthesis::StringConfig& config);
    void setParam(engine::ParamId id, float value);
    float getParam(engine::ParamId id) const;
    void setMasterGain(float value);
    float masterGain() const;
    const synthesis::StringConfig& synthConfig() const { return synthConfig_; }
    const std::string& lastError() const { return audioEngine_.lastError(); }

private:
    void handleRender(float* output, std::size_t frames);

    AudioEngineConfig audioConfig_;
    synthesis::StringConfig synthConfig_;
    float masterGain_ = 1.0f;

    WASAPIAudioEngine audioEngine_;
    engine::StringSynthEngine synthEngine_;
};

}  // namespace winaudio
