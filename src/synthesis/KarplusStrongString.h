#pragma once

#include <cstddef>
#include <memory>
#include <vector>

namespace dsp {
class FilterChain;
}

namespace synthesis {

enum class NoiseType { White, Binary };
enum class ExcitationMode { RandomNoisePick, FixedNoisePick };

struct StringConfig {
    double sampleRate = 44100.0;
    float decay = 0.996f;        // Base energy decay per period.
    float brightness = 0.5f;     // Low-pass strength (0=dark, 1=bright).
    float excitationBrightness = 0.6f;  // Excitation hardness / brightness.
    float excitationVelocity = 0.5f;    // Velocity modulation sensitivity.
    float excitationMix = 1.0f;   // 0 = Impulse (pick), 1 = Noise
    float pickPosition = 0.5f;   // Pick position along string (0-1).
    float dispersionAmount = 0.12f;  // Dispersion amount (0 disables).
    float bodyTone = 0.5f;       // Body tone color.
    float bodySize = 0.5f;       // Body size scaling.
    float roomAmount = 0.0f;     // Room/wet amount.
    int roomIrIndex = 0;         // Built-in IR selection (index into IR library).
    NoiseType noiseType = NoiseType::White;
    bool enableLowpass = true;
    unsigned int seed = 0;  // Noise RNG seed (0 uses random_device).
    ExcitationMode excitationMode = ExcitationMode::RandomNoisePick;
};

class KarplusStrongString {
public:
    explicit KarplusStrongString(StringConfig config = {});
    ~KarplusStrongString();
    KarplusStrongString(const KarplusStrongString&) = delete;
    KarplusStrongString& operator=(const KarplusStrongString&) = delete;
    KarplusStrongString(KarplusStrongString&&) noexcept;
    KarplusStrongString& operator=(KarplusStrongString&&) noexcept;

    // Offline render for a whole note.
    std::vector<float> pluck(double frequency, double durationSeconds,
                             float velocity = 1.0f);
    // Start a real-time pluck.
    void start(double frequency, float velocity = 1.0f);
    // Pull one sample; returns 0 if inactive.
    float processSample();
    bool active() const { return active_; }
    float lastOutput() const { return lastOutput_; }

    // Preview current excitation buffer (delayBuffer_). For visualization/analysis.
    // Typically read after start() and before processSample(). maxSamples=0 = no truncation.
    std::vector<float> excitationBufferPreview(std::size_t maxSamples = 0) const;

    const StringConfig& config() const { return config_; }
    void updateConfig(const StringConfig& config);

private:
    void fillExcitationNoise();
    void applyPickPositionShape();
    void applyExcitationColor();
    float computeEffectivePickPosition() const;
    float computeExcitationColor() const;
    void configureFilters();
    std::vector<float> dispersionCoefficients() const;

    StringConfig config_;
    std::vector<float> delayBuffer_;
    std::vector<float> outputBuffer_;
    std::size_t readIndex_ = 0;
    float decayFactor_ = 1.0f;
    bool active_ = false;
    float lastOutput_ = 0.0f;
    unsigned int rngSeed_;
    std::unique_ptr<dsp::FilterChain> filterChain_;
    double currentFrequency_ = 440.0;
    float currentVelocity_ = 1.0f;
    float currentPickPosition_ = 0.5f;
    float currentExcitationColor_ = 0.6f;
};

}  // namespace synthesis
