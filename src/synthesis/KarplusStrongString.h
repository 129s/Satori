#pragma once

#include <cstddef>
#include <vector>

namespace synthesis {

struct StringConfig {
    double sampleRate = 44100.0;
    float decay = 0.996f;       // 基础能量衰减
    float brightness = 0.5f;    // 低通滤波混合，越高越亮
    unsigned int seed = 0;      // 随机噪声种子，0 表示使用 random_device
};

class KarplusStrongString {
public:
    explicit KarplusStrongString(StringConfig config = {});

    std::vector<float> pluck(double frequency, double durationSeconds);

private:
    StringConfig config_;
    std::vector<float> delayBuffer_;
    std::vector<float> outputBuffer_;
    unsigned int rngSeed_;
};

}  // namespace synthesis
