#pragma once

#include <cstddef>
#include <memory>
#include <vector>

namespace dsp {
class FilterChain;
}

namespace synthesis {

enum class NoiseType { White, Binary };

struct StringConfig {
    double sampleRate = 44100.0;
    float decay = 0.996f;        // 基础能量衰减
    float brightness = 0.5f;     // 控制低通滤波强度
    float pickPosition = 0.5f;   // 0-1 之间
    NoiseType noiseType = NoiseType::White;
    bool enableLowpass = true;
    unsigned int seed = 0;  // 随机噪声种子，0 表示使用 random_device
};

class KarplusStrongString {
public:
    explicit KarplusStrongString(StringConfig config = {});
    ~KarplusStrongString();

    std::vector<float> pluck(double frequency, double durationSeconds);
    const StringConfig& config() const { return config_; }
    void updateConfig(const StringConfig& config) { config_ = config; configureFilters(); }

private:
    void fillExcitationNoise();
    void applyPickPositionShape();
    void configureFilters();

    StringConfig config_;
    std::vector<float> delayBuffer_;
    std::vector<float> outputBuffer_;
    unsigned int rngSeed_;
    std::unique_ptr<dsp::FilterChain> filterChain_;
};

}  // namespace synthesis
