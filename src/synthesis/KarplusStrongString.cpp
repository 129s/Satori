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

float clampDuty(float value) { return std::clamp(value, 0.01f, 0.99f); }

float warpPhase(float phase01, float duty01) {
    const float phase = std::clamp(phase01, 0.0f, 1.0f);
    const float duty = clampDuty(duty01);
    if (phase < duty) {
        return 0.5f * (phase / duty);
    }
    return 0.5f + 0.5f * ((phase - duty) / (1.0f - duty));
}

float waveformSample(WaveformType type, float phase01, float duty01) {
    constexpr float kTwoPi = 6.283185307179586f;
    constexpr float kPi = 3.141592653589793f;

    const float warped = warpPhase(phase01, duty01);
    switch (type) {
        case WaveformType::Triangle: {
            // -1 at 0, +1 at 0.5, -1 at 1.
            if (warped < 0.5f) {
                return -1.0f + 4.0f * warped;
            }
            return 3.0f - 4.0f * warped;
        }
        case WaveformType::Saw:
            return 2.0f * warped - 1.0f;
        case WaveformType::Square: {
            // True PWM: use the unwarped phase threshold.
            const float duty = clampDuty(duty01);
            return (std::clamp(phase01, 0.0f, 1.0f) < duty) ? 1.0f : -1.0f;
        }
        case WaveformType::Semisine:
            // One half-sine per cycle: includes all integer harmonics, ~1/n^2 rolloff.
            return std::sin(kPi * warped);
        case WaveformType::Sine:
        default:
            return std::sin(kTwoPi * warped);
    }
}

float applyNoiseOverdrive(float x, float drive01) {
    const float d = clamp01(drive01);
    const float g = d * 50.0f;  // Higher => closer to sign(x).
    if (g <= 1e-6f) {
        return x;
    }
    const float ax = std::abs(x);
    return (x * (1.0f + g)) / (1.0f + g * ax);
}

float onePoleAlphaFromCutoff(double sampleRate, float cutoffHz) {
    if (sampleRate <= 0.0) {
        return 1.0f;
    }
    const double fc = std::max(1.0, static_cast<double>(cutoffHz));
    const double a = 1.0 - std::exp(-(2.0 * 3.141592653589793 * fc) / sampleRate);
    return static_cast<float>(std::clamp(a, 0.001, 0.999));
}

float cutoffForNoiseColor(double sampleRate, float color01) {
    const double sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    const double minFc = 160.0;
    const double maxFc = std::max(minFc, 0.45 * sr);
    const double t = std::clamp(static_cast<double>(clamp01(color01)), 0.0, 1.0);
    const double fc = minFc * std::pow(maxFc / minFc, t);
    return static_cast<float>(fc);
}

void generateNoise(std::vector<float>& out,
                   std::mt19937& rng,
                   double sampleRate,
                   float jitter01,
                   float overdrive01,
                   float color01) {
    if (out.empty()) {
        return;
    }

    std::uniform_real_distribution<float> uniform(-1.0f, 1.0f);
    std::uniform_real_distribution<float> uniform01(0.0f, 1.0f);

    const float jitter = clamp01(jitter01);
    const float p = jitter * jitter;  // More resolution near 0.
    const float invSqrtP =
        (p > 1e-6f) ? std::min(4.0f, 1.0f / std::sqrt(p)) : 0.0f;

    for (auto& sample : out) {
        float x = uniform(rng);
        if (p < 0.9999f) {
            if (uniform01(rng) > p) {
                x = 0.0f;
            } else {
                x *= invSqrtP;
            }
        }
        x = applyNoiseOverdrive(x, overdrive01);
        sample = x;
    }

    const float cutoff = cutoffForNoiseColor(sampleRate, color01);
    const float alpha = onePoleAlphaFromCutoff(sampleRate, cutoff);
    float state = 0.0f;
    for (auto& sample : out) {
        state += alpha * (sample - state);
        sample = state;
    }
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

void KarplusStrongString::fillExcitationBuffer() {
    if (excitationBuffer_.empty()) {
        return;
    }

    std::mt19937 rng(rngSeed_);
    const bool randomMode =
        config_.excitationMode == ExcitationMode::RandomNoisePick;
    if (randomMode) {
        rngSeed_ = rng();  // Update seed so next excitation gets a new jitter pattern.
    }

    const std::size_t n = excitationBuffer_.size();
    std::vector<float> noise(n, 0.0f);
    const bool noiseOn =
        config_.noiseEnabled && clamp01(config_.noiseLevel) > 0.0001f;
    if (noiseOn) {
        generateNoise(noise, rng, config_.sampleRate, config_.noiseJitter,
                      config_.noiseOverdrive, config_.noiseColor);
        const float level = clamp01(config_.noiseLevel);
        for (auto& sample : noise) {
            sample *= level;
        }
    }

    const bool waveOn =
        config_.waveEnabled && clamp01(config_.waveLevel) > 0.0001f;
    const float waveLevel = clamp01(config_.waveLevel);
    const bool hammer = (config_.excitationType == ExcitationType::Hammer);
    constexpr float kPi = 3.141592653589793f;

    float mean = 0.0f;
    for (std::size_t i = 0; i < n; ++i) {
        const float phase =
            (n > 1) ? (static_cast<float>(i) / static_cast<float>(n - 1)) : 0.0f;
        const float env = hammer ? ((n > 1) ? std::sin(kPi * phase) : 1.0f) : 1.0f;

        float sample = 0.0f;
        if (waveOn) {
            sample += waveLevel *
                      waveformSample(config_.waveformType, phase, config_.waveDuty);
        }
        if (noiseOn) {
            sample += noise[i];
        }
        sample *= env;

        excitationBuffer_[i] = sample;
        mean += sample;
    }

    if (n > 1) {
        mean /= static_cast<float>(n);
        for (auto& sample : excitationBuffer_) {
            sample -= mean;
        }
    }
}

float KarplusStrongString::computeEffectivePickPosition() const {
    return 0.5f;
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
    (void)velocity;

    currentFrequency_ = frequency;
    currentPickPosition_ = computeEffectivePickPosition();

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

    if (config_.excitationType == ExcitationType::Hammer) {
        constexpr double contactSeconds = 0.0035;
        hammerSamplesTotal_ = static_cast<std::size_t>(std::lround(
            std::clamp(contactSeconds * config_.sampleRate, 2.0, 4096.0)));
        excitationBuffer_.assign(hammerSamplesTotal_, 0.0f);
        fillExcitationBuffer();
    } else {
        excitationBuffer_.assign(period, 0.0f);
        fillExcitationBuffer();
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
