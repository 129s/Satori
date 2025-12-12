#include "synthesis/KarplusStrongString.h"

#include <algorithm>
#include <cmath>
#include <random>

#include "dsp/Filter.h"

namespace synthesis {

namespace {
float clamp01(float value) {
    return std::max(0.0f, std::min(1.0f, value));
}

float clampPick(float value) {
    return std::max(0.001f, std::min(0.999f, value));
}
}  // namespace

KarplusStrongString::KarplusStrongString(StringConfig config)
    : config_(config), rngSeed_(config.seed) {
    if (rngSeed_ == 0) {
        std::random_device rd;
        rngSeed_ = rd();
    }
    configureFilters();
}

void KarplusStrongString::updateConfig(const StringConfig& config) {
    config_ = config;
    if (config_.seed != 0) {
        rngSeed_ = config_.seed;
    } else if (rngSeed_ == 0) {
        std::random_device rd;
        rngSeed_ = rd();
    }
    configureFilters();
}

KarplusStrongString::~KarplusStrongString() = default;

KarplusStrongString::KarplusStrongString(KarplusStrongString&&) noexcept = default;
KarplusStrongString& KarplusStrongString::operator=(
    KarplusStrongString&&) noexcept = default;

std::vector<float> KarplusStrongString::pluck(double frequency,
                                              double durationSeconds,
                                              float velocity) {
    if (frequency <= 0.0 || durationSeconds <= 0.0 ||
        config_.sampleRate <= 0.0) {
        return {};
    }

    const auto totalSamples = static_cast<std::size_t>(
        std::max(0.0, std::floor(durationSeconds * config_.sampleRate)));
    if (totalSamples == 0) {
        return {};
    }

    start(frequency, velocity);
    if (!active_) {
        return {};
    }

    outputBuffer_.assign(totalSamples, 0.0f);
    for (std::size_t i = 0; i < totalSamples; ++i) {
        outputBuffer_[i] = processSample();
    }

    active_ = false;
    lastOutput_ = 0.0f;
    return outputBuffer_;
}

std::vector<float> KarplusStrongString::excitationBufferPreview(
    std::size_t maxSamples) const {
    if (delayBuffer_.empty()) {
        return {};
    }
    std::vector<float> preview = delayBuffer_;
    if (maxSamples > 0 && preview.size() > maxSamples) {
        preview.resize(maxSamples);
    }
    return preview;
}

void KarplusStrongString::fillExcitationNoise() {
    if (delayBuffer_.empty()) {
        return;
    }

    std::mt19937 rng(rngSeed_);
    const bool randomMode =
        config_.excitationMode == ExcitationMode::RandomNoisePick;
    if (randomMode) {
        rngSeed_ = rng();  // Update seed so next pluck gets a new noise burst.
    }

    std::uniform_real_distribution<float> uniform(-1.0f, 1.0f);
    std::bernoulli_distribution binary(0.5);

    const float mix = clamp01(config_.excitationMix);
    const std::size_t n = delayBuffer_.size();

    // 1) Generate noise excitation.
    std::vector<float> noise(n, 0.0f);
    for (auto& sample : noise) {
        switch (config_.noiseType) {
            case NoiseType::Binary:
                sample = binary(rng) ? 1.0f : -1.0f;
                break;
            case NoiseType::White:
            default:
                sample = uniform(rng);
                break;
        }
    }

    // 2) Generate a plectrum-shaped impulse (short Hann bump) centered at pick position.
    std::vector<float> impulse(n, 0.0f);
    constexpr double kImpulseDurationSeconds = 0.005;  // ~5ms pluck transient.
    const double sr = config_.sampleRate > 0.0 ? config_.sampleRate : 44100.0;
    std::size_t windowLen = static_cast<std::size_t>(
        std::round(sr * kImpulseDurationSeconds));
    windowLen = std::max<std::size_t>(2, std::min(windowLen, n));

    const float pickPos = clampPick(currentPickPosition_);
    const std::size_t pickIndex = static_cast<std::size_t>(
        pickPos * static_cast<float>(n - 1));

    std::size_t start =
        (pickIndex > windowLen / 2) ? pickIndex - windowLen / 2 : 0;
    if (start + windowLen > n) {
        start = (n > windowLen) ? (n - windowLen) : 0;
    }

    constexpr double kTwoPi = 6.283185307179586;
    for (std::size_t i = 0; i < windowLen; ++i) {
        const double phase =
            (windowLen > 1)
                ? (kTwoPi * static_cast<double>(i) /
                   static_cast<double>(windowLen - 1))
                : 0.0;
        const float w =
            static_cast<float>(0.5 - 0.5 * std::cos(phase));  // Hann window
        impulse[start + i] = w;
    }

    // 3) Mix Noise/Impulse and remove DC.
    float mean = 0.0f;
    for (std::size_t i = 0; i < n; ++i) {
        const float v = mix * noise[i] + (1.0f - mix) * impulse[i];
        delayBuffer_[i] = v;
        mean += v;
    }
    // Keep legacy behavior for pure-noise excitation to avoid unintended energy shifts.
    if (mix < 0.999f) {
        mean /= static_cast<float>(n);
        for (auto& sample : delayBuffer_) {
            sample -= mean;
        }
    }
}

void KarplusStrongString::applyPickPositionShape() {
    if (delayBuffer_.size() < 3) {
        return;
    }
    const float pickPos = clampPick(currentPickPosition_);
    const auto pickIndex = static_cast<std::size_t>(
        pickPos * static_cast<float>(delayBuffer_.size() - 1));

    if (pickIndex == 0 || pickIndex >= delayBuffer_.size() - 1) {
        return;
    }

    for (std::size_t i = 0; i <= pickIndex; ++i) {
        const float gain = static_cast<float>(i) / static_cast<float>(pickIndex);
        delayBuffer_[i] *= gain;
    }

    const std::size_t rightCount = delayBuffer_.size() - 1 - pickIndex;
    for (std::size_t i = pickIndex + 1; i < delayBuffer_.size(); ++i) {
        const float gain =
            static_cast<float>(delayBuffer_.size() - 1 - i) / static_cast<float>(rightCount);
        delayBuffer_[i] *= gain;
    }
}

void KarplusStrongString::applyExcitationColor() {
    if (delayBuffer_.empty()) {
        return;
    }
    const float color = clamp01(currentExcitationColor_);
    if (color <= 0.01f) {
        return;
    }
    // Use a 1-pole lowpass to split low/high and tilt the spectrum by color.
    const float targetAlpha =
        std::clamp(0.05f + 0.4f * color, 0.01f, 0.95f);
    float state = 0.0f;
    for (auto& sample : delayBuffer_) {
        state = targetAlpha * sample + (1.0f - targetAlpha) * state;
        const float high = sample - state;
        const float tilt = (color - 0.5f) * 1.2f;  // Negative = darker, positive = brighter.
        const float lowGain = 1.0f - 0.4f * tilt;
        const float highGain = 1.0f + 0.6f * tilt;
        sample = state * lowGain + high * highGain;
    }
}

float KarplusStrongString::computeEffectivePickPosition() const {
    const float sensitivity = clamp01(config_.excitationVelocity);
    const float offset = (0.5f - currentVelocity_) * 0.25f * sensitivity;
    return clampPick(config_.pickPosition + offset);
}

float KarplusStrongString::computeExcitationColor() const {
    const float sensitivity = clamp01(config_.excitationVelocity);
    const float base = clamp01(config_.excitationBrightness);
    const float delta = (currentVelocity_ - 0.5f) * 0.6f * sensitivity;
    return clamp01(base + delta);
}

void KarplusStrongString::configureFilters() {
    const auto dispersion = dispersionCoefficients();
    const bool needDispersion = !dispersion.empty();
    const bool needLowpass = config_.enableLowpass;

    if (!needDispersion && !needLowpass) {
        filterChain_.reset();
        return;
    }

    if (!filterChain_) {
        filterChain_ = std::make_unique<dsp::FilterChain>();
    } else {
        filterChain_->clear();
    }

    if (needDispersion) {
        for (float coeff : dispersion) {
            auto ap = std::make_unique<dsp::FirstOrderAllPass>(coeff);
            filterChain_->addFilter(std::move(ap));
        }
    }
    if (needLowpass) {
        auto lowpass = std::make_unique<dsp::OnePoleLowPass>(clamp01(config_.brightness));
        filterChain_->addFilter(std::move(lowpass));
    }
}

void KarplusStrongString::start(double frequency, float velocity) {
    if (frequency <= 0.0 || config_.sampleRate <= 0.0) {
        active_ = false;
        return;
    }

    currentFrequency_ = frequency;
    currentVelocity_ = clamp01(velocity);
    currentPickPosition_ = computeEffectivePickPosition();
    currentExcitationColor_ = computeExcitationColor();
    configureFilters();

    const auto period = static_cast<std::size_t>(
        std::max(2.0, std::round(config_.sampleRate / frequency)));
    delayBuffer_.assign(period, 0.0f);
    readIndex_ = 0;
    decayFactor_ = clamp01(config_.decay);
    lastOutput_ = 0.0f;

    fillExcitationNoise();
    applyPickPositionShape();
    applyExcitationColor();

    if (filterChain_) {
        filterChain_->reset();
    }

    active_ = true;
}

float KarplusStrongString::processSample() {
    if (!active_ || delayBuffer_.empty()) {
        return 0.0f;
    }

    const float current = delayBuffer_[readIndex_];
    const float next = delayBuffer_[(readIndex_ + 1) % delayBuffer_.size()];
    const float averaged = 0.5f * (current + next);
    float filtered = averaged;
    if (filterChain_ && !filterChain_->empty()) {
        filtered = filterChain_->process(averaged);
    }
    delayBuffer_[readIndex_] = decayFactor_ * filtered;

    readIndex_ = (readIndex_ + 1) % delayBuffer_.size();
    lastOutput_ = current;
    return lastOutput_;
}

std::vector<float> KarplusStrongString::dispersionCoefficients() const {
    const float amount = clamp01(config_.dispersionAmount);
    if (amount <= 0.0001f || config_.sampleRate <= 0.0) {
        return {};
    }
    const double nyquist = config_.sampleRate * 0.5;
    const double freq = std::clamp(currentFrequency_, 10.0, nyquist);
    const float normFreq = static_cast<float>(freq / nyquist);
    const float scaled = amount * 0.7f;
    const float coeff1 = std::clamp(scaled * (0.35f + 0.65f * normFreq), -0.85f, 0.85f);
    const float coeff2 = std::clamp(scaled * 0.6f * (0.4f + 0.6f * normFreq), -0.8f, 0.8f);
    return {coeff1, coeff2};
}

}  // namespace synthesis
