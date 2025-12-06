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
    float dispersionAmount = 0.12f;  // 频散强度，0 表示关闭
    NoiseType noiseType = NoiseType::White;
    bool enableLowpass = true;
    unsigned int seed = 0;  // 随机噪声种子，0 表示使用 random_device
};

class KarplusStrongString {
public:
    explicit KarplusStrongString(StringConfig config = {});
    ~KarplusStrongString();
    KarplusStrongString(const KarplusStrongString&) = delete;
    KarplusStrongString& operator=(const KarplusStrongString&) = delete;
    KarplusStrongString(KarplusStrongString&&) noexcept;
    KarplusStrongString& operator=(KarplusStrongString&&) noexcept;

    // 预渲染整段样本（离线用途）
    std::vector<float> pluck(double frequency, double durationSeconds);
    // 启动持续性的弦震动，用于实时 voice 播放
    void start(double frequency);
    // 拉取单个样本；未启动时返回 0
    float processSample();
    bool active() const { return active_; }
    float lastOutput() const { return lastOutput_; }

    const StringConfig& config() const { return config_; }
    void updateConfig(const StringConfig& config) { config_ = config; configureFilters(); }

private:
    void fillExcitationNoise();
    void applyPickPositionShape();
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
};

}  // namespace synthesis
