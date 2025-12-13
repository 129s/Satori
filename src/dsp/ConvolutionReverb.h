#pragma once

#include <array>
#include <cstddef>
#include <vector>

#include "dsp/PartitionedConvolver.h"

namespace dsp {

struct StereoConvolutionKernel {
    ConvolutionKernel left;
    ConvolutionKernel right;  // empty if mono
    bool isStereo = false;
};

// Convolution reverb wrapper that provides:
// - block-based processing internally (sample-in/sample-out)
// - IR selection with click-free crossfade between kernels
// - wet/dry mix
class ConvolutionReverb {
public:
    void setSampleRate(double sampleRate);

    void setMix(float mix01);     // 0..1
    float mix() const { return targetMix_; }

    void setIrKernels(std::vector<StereoConvolutionKernel> kernels);
    int irCount() const { return static_cast<int>(kernels_.size()); }

    void setIrIndex(int index);   // 0..irCount-1
    int irIndex() const { return irIndex_; }

    void reset();

    // Process one mono sample and output stereo.
    // Dry stays centered; for mono IRs the wet is lightly decorrelated for width.
    void processSample(float input, float& outL, float& outR);

private:
    void rebuildForCurrentKernels();
    void processBlock();

    double sampleRate_ = 44100.0;
    std::size_t blockSize_ = 256;
    std::size_t fftSize_ = 512;

    float targetMix_ = 0.0f;
    float currentMix_ = 0.0f;
    float mixSmoothingAlpha_ = 1.0f;

    std::vector<StereoConvolutionKernel> kernels_;
    int irIndex_ = 0;
    int pendingIrIndex_ = -1;

    // Crossfade between IRs (in blocks).
    int fadeTotalBlocks_ = 16;  // ~90ms at 44.1k with 256-blocks
    std::size_t fadeSamplePos_ = 0;

    PartitionedConvolver convolver_;
    std::vector<float> overlapAL_;
    std::vector<float> overlapAR_;
    std::vector<float> overlapBL_;
    std::vector<float> overlapBR_;
    std::vector<float> inBlock_;
    std::vector<float> wetBlockAL_;
    std::vector<float> wetBlockAR_;
    std::vector<float> wetBlockBL_;
    std::vector<float> wetBlockBR_;
    std::size_t inPos_ = 0;
    std::size_t outPos_ = 0;
    bool wetReady_ = false;

    // Keep reverb output in a reasonable range (IRs are peak-normalized but can
    // still have large overall energy).
    float wetLevel_ = 0.25f;

    // Lightweight stereo decorrelation on the wet signal.
    std::array<float, 64> stereoDelay_{};
    std::size_t stereoPos_ = 0;
    float stereoLp_ = 0.0f;
};

}  // namespace dsp
