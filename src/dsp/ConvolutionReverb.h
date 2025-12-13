#pragma once

#include <array>
#include <cstdint>
#include <cstddef>
#include <vector>

#include "dsp/PartitionedConvolver.h"

namespace dsp {

struct StereoConvolutionKernel {
    ConvolutionKernel left;
    ConvolutionKernel right;  // empty if mono
    bool isStereo = false;

    // Optional late-tail kernel for non-uniform partitioning.
    // When provided, the reverb runs a two-stage convolution:
    // - early stage: 256/512 (sample-accurate early reflections)
    // - late stage:  2048/4096, computed less frequently and scheduled ahead
    ConvolutionKernel leftLate;
    ConvolutionKernel rightLate;  // empty if mono
    bool hasLate = false;
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

    // Process one input block and output wet-only stereo, aligned to the block.
    // input length = 256, out buffers length = 256.
    // Intended for realtime worker threads; avoids an extra block of latency
    // introduced by the sample-in/sample-out wrapper.
    void processBlockWet(const float* input, float* outWetL, float* outWetR);

private:
    void rebuildForCurrentKernels();
    void processBlock();

    double sampleRate_ = 44100.0;
    std::size_t blockSize_ = 256;
    std::size_t fftSize_ = 512;

    // Late stage uses a smaller FFT to reduce CPU spikes (computed every 4 blocks).
    static constexpr std::size_t kLateBlockSize = 1024;
    static constexpr std::size_t kLateFftSize = 2048;
    static constexpr std::size_t kLateBlocksPerChunk = 4;  // 1024 / 256
    static constexpr std::size_t kLateStartBlocks = 4;     // IR tail starts after 1024 samples
    static constexpr std::size_t kScheduleBlocks = 64;     // power-of-two ring

    float targetMix_ = 0.0f;
    float currentMix_ = 0.0f;
    float mixSmoothingAlpha_ = 1.0f;

    std::vector<StereoConvolutionKernel> kernels_;
    int irIndex_ = 0;
    int pendingIrIndex_ = -1;

    // Crossfade between IRs (in blocks).
    int fadeTotalBlocks_ = 16;  // ~90ms at 44.1k with 256-blocks
    std::size_t fadeSamplePos_ = 0;

    PartitionedConvolver convolverEarly_;
    PartitionedConvolver convolverLate_;
    std::vector<float> overlapAL_;
    std::vector<float> overlapAR_;
    std::vector<float> overlapBL_;
    std::vector<float> overlapBR_;
    std::vector<float> overlapLateAL_;
    std::vector<float> overlapLateAR_;
    std::vector<float> overlapLateBL_;
    std::vector<float> overlapLateBR_;
    std::vector<float> inBlock_;
    std::vector<float> wetBlockAL_;
    std::vector<float> wetBlockAR_;
    std::vector<float> wetBlockBL_;
    std::vector<float> wetBlockBR_;
    std::vector<float> lateInBlock_;   // 2048
    std::vector<float> lateOutAL_;     // 2048
    std::vector<float> lateOutAR_;     // 2048
    std::vector<float> lateOutBL_;     // 2048
    std::vector<float> lateOutBR_;     // 2048
    std::size_t lateInPos_ = 0;        // 0..2048
    std::uint64_t blockIndex_ = 0;

    // Scheduled late-tail contributions keyed by output block index.
    // Layout: [blockSlot * blockSize + sampleIndex]
    std::vector<float> scheduledAL_;
    std::vector<float> scheduledAR_;
    std::vector<float> scheduledBL_;
    std::vector<float> scheduledBR_;
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
