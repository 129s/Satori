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

KarplusStrongString::~KarplusStrongString() = default;

KarplusStrongString::KarplusStrongString(KarplusStrongString&&) noexcept = default;
KarplusStrongString& KarplusStrongString::operator=(
    KarplusStrongString&&) noexcept = default;

std::vector<float> KarplusStrongString::pluck(double frequency,
                                              double durationSeconds) {
    if (frequency <= 0.0 || durationSeconds <= 0.0 ||
        config_.sampleRate <= 0.0) {
        return {};
    }

    const auto totalSamples = static_cast<std::size_t>(
        std::max(0.0, std::floor(durationSeconds * config_.sampleRate)));
    if (totalSamples == 0) {
        return {};
    }

    start(frequency);
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

void KarplusStrongString::fillExcitationNoise() {
    std::mt19937 rng(rngSeed_);
    rngSeed_ = rng();  // 更新种子，保证下一次拨弦有新噪声

    std::uniform_real_distribution<float> uniform(-1.0f, 1.0f);
    std::bernoulli_distribution binary(0.5);

    for (auto& sample : delayBuffer_) {
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
}

void KarplusStrongString::applyPickPositionShape() {
    if (delayBuffer_.size() < 3) {
        return;
    }
    const float pickPos = clampPick(config_.pickPosition);
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

void KarplusStrongString::configureFilters() {
    if (!config_.enableLowpass) {
        filterChain_.reset();
        return;
    }

    if (!filterChain_) {
        filterChain_ = std::make_unique<dsp::FilterChain>();
    } else {
        filterChain_->clear();
    }

    auto lowpass = std::make_unique<dsp::OnePoleLowPass>(clamp01(config_.brightness));
    filterChain_->addFilter(std::move(lowpass));
}

void KarplusStrongString::start(double frequency) {
    if (frequency <= 0.0 || config_.sampleRate <= 0.0) {
        active_ = false;
        return;
    }

    configureFilters();

    const auto period = static_cast<std::size_t>(
        std::max(2.0, std::round(config_.sampleRate / frequency)));
    delayBuffer_.assign(period, 0.0f);
    readIndex_ = 0;
    decayFactor_ = clamp01(config_.decay);
    lastOutput_ = 0.0f;

    fillExcitationNoise();
    applyPickPositionShape();

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

}  // namespace synthesis
