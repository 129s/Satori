#pragma once

#include <array>
#include <atomic>
#include <string>
#include <vector>

#include "engine/StringSynthEngine.h"
#include "synthesis/KarplusStrongString.h"
#include "win/audio/AudioEngineTypes.h"
#include "win/audio/UnifiedAudioEngine.h"

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
    const AudioEngineConfig& audioConfig() const { return audioConfig_; }

    // Stops/reinitializes the audio device. In shared-mode WASAPI, the device's
    // mix sample rate is not user-controlled; the synth may run at a different
    // internal rate and be resampled to the device rate.
    bool reconfigureAudio(const AudioEngineConfig& config);
    const std::string& lastError() const { return audioEngine_.lastError(); }
    RealtimeMetrics metrics() const;

private:
    void handleRender(float* output, std::size_t frames);
    void applyPendingParams();
    void resetResampler();
    void ensureResampleInputFrames(std::size_t frames);
    void popResampleInputFrame(std::vector<float>& frame);

    AudioEngineConfig audioConfig_;
    synthesis::StringConfig synthConfig_;
    float masterGain_ = 1.0f;
    float ampReleaseSeconds_ = 0.35f;

    UnifiedAudioEngine audioEngine_;
    engine::StringSynthEngine synthEngine_;

    static constexpr std::size_t kParamCount =
        static_cast<std::size_t>(engine::ParamId::AmpRelease) + 1;
    std::array<std::atomic<float>, kParamCount> pendingParamValues_{};
    std::atomic<std::uint32_t> pendingParamMask_{0};

    std::atomic<std::uint64_t> callbackCount_{0};
    std::atomic<double> callbackMsMax_{0.0};
    std::atomic<double> callbackMsAvg_{0.0};

    double resampleInRate_ = 0.0;
    double resampleOutRate_ = 0.0;
    std::size_t resampleChannels_ = 0;
    double resampleStep_ = 1.0;
    double resamplePhase_ = 0.0;
    std::vector<float> resampleFifo_;
    std::size_t resampleReadSample_ = 0;
    std::vector<float> resampleFrame0_;
    std::vector<float> resampleFrame1_;
    bool resampleReady_ = false;
};

}  // namespace winaudio
