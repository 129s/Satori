#include "synthesis/KarplusStrongString.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace synthesis {

namespace {
float clamp01(float value) {
    return std::max(0.0f, std::min(1.0f, value));
}
}  // namespace

KarplusStrongString::KarplusStrongString(StringConfig config)
    : config_(config), rngSeed_(config.seed) {
    if (rngSeed_ == 0) {
        std::random_device rd;
        rngSeed_ = rd();
    }
}

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

    auto period = static_cast<std::size_t>(
        std::max(2.0, std::round(config_.sampleRate / frequency)));
    delayBuffer_.assign(period, 0.0f);

    std::mt19937 rng(rngSeed_);
    rngSeed_ = rng();  // 更新种子，保证下一次拨弦有新噪声
    std::uniform_real_distribution<float> noise(-1.0f, 1.0f);
    for (auto& sample : delayBuffer_) {
        sample = noise(rng);
    }

    outputBuffer_.assign(totalSamples, 0.0f);
    const float decay = clamp01(config_.decay);
    const float brightness = clamp01(config_.brightness);
    std::size_t readIndex = 0;

    for (std::size_t i = 0; i < totalSamples; ++i) {
        const float current = delayBuffer_[readIndex];
        const float next = delayBuffer_[(readIndex + 1) % delayBuffer_.size()];
        const float averaged = 0.5f * (current + next);
        const float filtered = brightness * averaged +
                               (1.0f - brightness) * current;
        delayBuffer_[readIndex] = decay * filtered;

        outputBuffer_[i] = current;
        readIndex = (readIndex + 1) % delayBuffer_.size();
    }

    return outputBuffer_;
}

}  // namespace synthesis
