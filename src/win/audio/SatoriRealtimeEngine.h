#pragma once

#include <array>
#include <atomic>
#include <string>

#include "engine/StringSynthEngine.h"
#include "synthesis/KarplusStrongString.h"
#include "win/audio/WASAPIAudioEngine.h"

namespace winaudio {

inline constexpr double kDefaultNoteDurationSeconds = 2.0;

class SatoriRealtimeEngine {
public:
    struct RealtimeMetrics {
        std::uint64_t callbackCount = 0;
        double callbackMsAvg = 0.0;
        double callbackMsMax = 0.0;
        std::uint32_t pendingParamMask = 0;
    };

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
    RealtimeMetrics metrics() const;

private:
    void handleRender(float* output, std::size_t frames);
    void applyPendingParams();

    AudioEngineConfig audioConfig_;
    synthesis::StringConfig synthConfig_;
    float masterGain_ = 1.0f;
    float ampReleaseSeconds_ = 0.35f;

    WASAPIAudioEngine audioEngine_;
    engine::StringSynthEngine synthEngine_;

    static constexpr std::size_t kParamCount =
        static_cast<std::size_t>(engine::ParamId::AmpRelease) + 1;
    std::array<std::atomic<float>, kParamCount> pendingParamValues_{};
    std::atomic<std::uint32_t> pendingParamMask_{0};

    std::atomic<std::uint64_t> callbackCount_{0};
    std::atomic<double> callbackMsMax_{0.0};
    std::atomic<double> callbackMsAvg_{0.0};
};

}  // namespace winaudio
