#include "synthesis/KarplusStrongString.h"

#include <algorithm>
#include <cmath>
#include <complex>
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

double firstOrderAllPassPhaseDelaySamples(double coefficient, double omega);

float thiranFractionalDelayCoefficient(double fractionalDelay) {
    const double d = std::clamp(fractionalDelay, 0.0, 1.0);
    if (d < 1e-6) {
        return 0.0f;
    }
    const double a = (1.0 - d) / (1.0 + d);  // Thiran 1st-order allpass.
    const double aClamped = std::min(a, 0.9995);  // Avoid coefficient=1 edge case.
    return static_cast<float>(-aClamped);  // Filter implementation uses opposite sign.
}

float allPassCoefficientForPhaseDelay(double phaseDelaySamples, double omega) {
    const double desired = std::clamp(phaseDelaySamples, 0.0, 1.999);
    if (desired < 1e-6 || omega <= 0.0) {
        return 0.0f;
    }

    double lo = -0.9995;
    double hi = 0.9995;
    for (int i = 0; i < 60; ++i) {
        const double mid = 0.5 * (lo + hi);
        const double d = firstOrderAllPassPhaseDelaySamples(mid, omega);
        if (d < desired) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    const double coeff = 0.5 * (lo + hi);
    return static_cast<float>(coeff);
}

double firstOrderAllPassPhaseDelaySamples(double coefficient, double omega) {
    if (omega <= 0.0) {
        return 0.0;
    }
    if (omega < 1e-8) {
        const double c = std::clamp(static_cast<double>(coefficient), -0.999999, 0.999999);
        return (1.0 + c) / (1.0 - c);
    }

    const std::complex<double> z = std::polar(1.0, -omega);  // e^{-jω}
    const std::complex<double> num = -static_cast<double>(coefficient) + z;
    const std::complex<double> den = 1.0 - static_cast<double>(coefficient) * z;
    const std::complex<double> h = num / den;
    const double phase = std::atan2(h.imag(), h.real());
    return -phase / omega;
}

double onePoleLowPassPhaseDelaySamples(double alpha, double omega) {
    if (omega <= 0.0) {
        return 0.0;
    }
    const double a = 1.0 - std::clamp(alpha, 0.0, 1.0);
    if (a <= 0.0) {
        return 0.0;
    }
    if (omega < 1e-8) {
        const double denom = 1.0 - a;
        if (denom <= 1e-12) {
            return 0.0;
        }
        return a / denom;
    }

    const std::complex<double> z = std::polar(1.0, -omega);  // e^{-jω}
    const std::complex<double> den = 1.0 - a * z;
    const double phase = -std::atan2(den.imag(), den.real());
    return -phase / omega;
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
    if (excitationBuffer_.empty()) {
        return {};
    }
    std::vector<float> preview = excitationBuffer_;
    if (maxSamples > 0 && preview.size() > maxSamples) {
        preview.resize(maxSamples);
    }
    return preview;
}

void KarplusStrongString::fillExcitationNoise() {
    if (excitationBuffer_.empty()) {
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
    const std::size_t n = excitationBuffer_.size();

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
        excitationBuffer_[i] = v;
        mean += v;
    }
    // Keep legacy behavior for pure-noise excitation to avoid unintended energy shifts.
    if (mix < 0.999f) {
        mean /= static_cast<float>(n);
        for (auto& sample : excitationBuffer_) {
            sample -= mean;
        }
    }
}

void KarplusStrongString::applyPickPositionShape() {
    if (excitationBuffer_.size() < 3) {
        return;
    }
    const float pickPos = clampPick(currentPickPosition_);
    const auto pickIndex = static_cast<std::size_t>(
        pickPos * static_cast<float>(excitationBuffer_.size() - 1));

    if (pickIndex == 0 || pickIndex >= excitationBuffer_.size() - 1) {
        return;
    }

    for (std::size_t i = 0; i <= pickIndex; ++i) {
        const float gain = static_cast<float>(i) / static_cast<float>(pickIndex);
        excitationBuffer_[i] *= gain;
    }

    const std::size_t rightCount = excitationBuffer_.size() - 1 - pickIndex;
    for (std::size_t i = pickIndex + 1; i < excitationBuffer_.size(); ++i) {
        const float gain =
            static_cast<float>(excitationBuffer_.size() - 1 - i) / static_cast<float>(rightCount);
        excitationBuffer_[i] *= gain;
    }
}

void KarplusStrongString::applyExcitationColor() {
    if (excitationBuffer_.empty()) {
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
    for (auto& sample : excitationBuffer_) {
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
    const bool needTuningAllpass = std::abs(tuningAllpassCoefficient_) > 1e-8f;

    if (!needDispersion && !needLowpass && !needTuningAllpass) {
        filterChain_.reset();
        return;
    }

    if (!filterChain_) {
        filterChain_ = std::make_unique<dsp::FilterChain>();
    } else {
        filterChain_->clear();
    }

    if (needTuningAllpass) {
        auto ap = std::make_unique<dsp::FirstOrderAllPass>(tuningAllpassCoefficient_);
        filterChain_->addFilter(std::move(ap));
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

void KarplusStrongString::initializeWaveguideFromExcitation() {
    if (excitationBuffer_.empty() || waveToBridge_.empty() || waveToNut_.empty()) {
        return;
    }
    const std::size_t n = waveToBridge_.size();
    if (waveToNut_.size() != n) {
        return;
    }

    const std::size_t count = std::min(n, excitationBuffer_.size());
    for (std::size_t i = 0; i < count; ++i) {
        const float value = 0.5f * excitationBuffer_[i];
        waveToNut_[(nutIndex_ + i) % n] = value;
        waveToBridge_[(bridgeIndex_ + (count - 1 - i)) % n] = value;
    }
}

void KarplusStrongString::injectAtPosition(float position, float value) {
    if (waveToBridge_.empty() || waveToNut_.empty()) {
        return;
    }
    const std::size_t n = waveToBridge_.size();
    if (waveToNut_.size() != n) {
        return;
    }

    const float p = clampPick(position);
    const std::size_t toNut =
        static_cast<std::size_t>(std::lround(p * static_cast<float>(n - 1)));
    const std::size_t toBridge = (n - 1) - toNut;

    const float half = 0.5f * value;
    waveToBridge_[(bridgeIndex_ + toBridge) % n] += half;
    waveToNut_[(nutIndex_ + toNut) % n] += half;
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

    const double targetRoundTripDelay = config_.sampleRate / frequency;
    const double omega =
        std::clamp(6.283185307179586 * frequency / config_.sampleRate, 1e-9, 3.141592653589793);

    double loopFilterDelay = 0.0;
    for (float coeff : dispersionCoefficients()) {
        loopFilterDelay += firstOrderAllPassPhaseDelaySamples(coeff, omega);
    }
    if (config_.enableLowpass) {
        loopFilterDelay += onePoleLowPassPhaseDelaySamples(clamp01(config_.brightness), omega);
    }

    const double propagationDelay =
        std::max(4.0, targetRoundTripDelay - loopFilterDelay);
    const double baseOneWayDelay = std::floor(propagationDelay * 0.5);
    const auto period =
        static_cast<std::size_t>(std::max(2.0, baseOneWayDelay));
    const double tuningDelay =
        std::clamp(propagationDelay - 2.0 * static_cast<double>(period), 0.0, 1.999);
    tuningAllpassCoefficient_ = allPassCoefficientForPhaseDelay(tuningDelay, omega);
    configureFilters();

    waveToBridge_.assign(period, 0.0f);
    waveToNut_.assign(period, 0.0f);
    bridgeIndex_ = 0;
    nutIndex_ = 0;
    decayFactor_ = clamp01(config_.decay);
    lastOutput_ = 0.0f;
    hammerSampleIndex_ = 0;
    hammerSamplesTotal_ = 0;
    hammerLowpassState_ = 0.0f;

    if (config_.excitationType == ExcitationType::Hammer) {
        hammerRng_.seed(rngSeed_);
        const bool randomMode =
            config_.excitationMode == ExcitationMode::RandomNoisePick;

        const float hardness = clamp01(currentExcitationColor_);
        const double contactSeconds = 0.0015 + (1.0 - hardness) * 0.0045;
        hammerSamplesTotal_ = static_cast<std::size_t>(std::lround(
            std::clamp(contactSeconds * config_.sampleRate, 2.0, 4096.0)));
        excitationBuffer_.assign(hammerSamplesTotal_, 0.0f);

        constexpr double kPi = 3.141592653589793;
        const float mix = clamp01(config_.excitationMix);
        const float lpAlpha = std::clamp(0.05f + 0.9f * hardness, 0.01f, 0.98f);
        float state = 0.0f;
        std::uniform_real_distribution<float> uniform(-1.0f, 1.0f);
        std::bernoulli_distribution binary(0.5);

        for (std::size_t i = 0; i < hammerSamplesTotal_; ++i) {
            const double phase =
                (hammerSamplesTotal_ > 1)
                    ? (kPi * static_cast<double>(i) /
                       static_cast<double>(hammerSamplesTotal_ - 1))
                    : 0.0;
            const float env = static_cast<float>(std::sin(phase));
            float noiseSample = 0.0f;
            switch (config_.noiseType) {
                case NoiseType::Binary:
                    noiseSample = binary(hammerRng_) ? 1.0f : -1.0f;
                    break;
                case NoiseType::White:
                default:
                    noiseSample = uniform(hammerRng_);
                    break;
            }
            const float pulse = env;
            const float combined = (1.0f - mix) * pulse + mix * noiseSample;
            state = lpAlpha * combined + (1.0f - lpAlpha) * state;
            excitationBuffer_[i] = env * state;
        }

        if (randomMode) {
            rngSeed_ = hammerRng_();
        }
    } else {
        excitationBuffer_.assign(period, 0.0f);
        fillExcitationNoise();
        applyPickPositionShape();
        applyExcitationColor();
        initializeWaveguideFromExcitation();
    }

    if (filterChain_) {
        filterChain_->reset();
    }

    active_ = true;
}

float KarplusStrongString::processSample() {
    if (!active_ || waveToBridge_.empty() || waveToNut_.empty()) {
        return 0.0f;
    }

    if (config_.excitationType == ExcitationType::Hammer &&
        hammerSampleIndex_ < excitationBuffer_.size()) {
        const float injection = excitationBuffer_[hammerSampleIndex_++];
        injectAtPosition(currentPickPosition_, 0.25f * injection);
    }

    const float toBridge = waveToBridge_[bridgeIndex_];
    const float toNut = waveToNut_[nutIndex_];

    float filtered = toBridge;
    if (filterChain_ && !filterChain_->empty()) {
        filtered = filterChain_->process(toBridge);
    }

    const float fromBridge = -decayFactor_ * filtered;
    const float fromNut = -toNut;

    waveToNut_[bridgeIndex_] = fromBridge;
    waveToBridge_[nutIndex_] = fromNut;

    bridgeIndex_ = (bridgeIndex_ + 1) % waveToBridge_.size();
    nutIndex_ = (nutIndex_ + 1) % waveToNut_.size();

    lastOutput_ = (toBridge - fromBridge);
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
