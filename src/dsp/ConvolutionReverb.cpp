#include "dsp/ConvolutionReverb.h"

#include <algorithm>
#include <cmath>

namespace dsp {

namespace {
float Clamp01(float v) { return std::max(0.0f, std::min(1.0f, v)); }
float ComputeOnePoleAlpha(double sampleRate, double timeSeconds) {
    if (sampleRate <= 0.0 || timeSeconds <= 0.0) {
        return 1.0f;
    }
    const double a = 1.0 - std::exp(-1.0 / (sampleRate * timeSeconds));
    return static_cast<float>(std::clamp(a, 0.0, 1.0));
}
}  // namespace

void ConvolutionReverb::setSampleRate(double sampleRate) {
    if (sampleRate <= 0.0) {
        return;
    }
    sampleRate_ = sampleRate;
    mixSmoothingAlpha_ = ComputeOnePoleAlpha(sampleRate_, 0.01);
    // block size stays fixed for now; we may tune later.
    rebuildForCurrentKernels();
}

void ConvolutionReverb::setMix(float mix01) { targetMix_ = Clamp01(mix01); }

void ConvolutionReverb::setIrKernels(std::vector<ConvolutionKernel> kernels) {
    kernels_ = std::move(kernels);
    if (kernels_.empty()) {
        irIndex_ = 0;
    } else {
        irIndex_ = std::clamp(irIndex_, 0, static_cast<int>(kernels_.size() - 1));
    }
    pendingIrIndex_ = -1;
    fadeSamplePos_ = 0;
    rebuildForCurrentKernels();
}

void ConvolutionReverb::setIrIndex(int index) {
    if (kernels_.empty()) {
        irIndex_ = 0;
        pendingIrIndex_ = -1;
        fadeSamplePos_ = 0;
        return;
    }
    index = std::clamp(index, 0, static_cast<int>(kernels_.size() - 1));
    if (index == irIndex_) {
        return;
    }
    pendingIrIndex_ = index;
    fadeSamplePos_ = 0;
    std::fill(overlapB_.begin(), overlapB_.end(), 0.0f);
}

void ConvolutionReverb::reset() {
    convolver_.reset();
    std::fill(inBlock_.begin(), inBlock_.end(), 0.0f);
    std::fill(wetBlockA_.begin(), wetBlockA_.end(), 0.0f);
    std::fill(wetBlockB_.begin(), wetBlockB_.end(), 0.0f);
    inPos_ = 0;
    outPos_ = 0;
    wetReady_ = false;
    pendingIrIndex_ = -1;
    fadeSamplePos_ = 0;

    currentMix_ = targetMix_;
    std::fill(overlapA_.begin(), overlapA_.end(), 0.0f);
    std::fill(overlapB_.begin(), overlapB_.end(), 0.0f);

    stereoDelay_.fill(0.0f);
    stereoPos_ = 0;
    stereoLp_ = 0.0f;
}

void ConvolutionReverb::processSample(float input, float& outL, float& outR) {
    const float dry = input;
    currentMix_ += (targetMix_ - currentMix_) * mixSmoothingAlpha_;

    float wetMono = 0.0f;
    if (wetReady_ && outPos_ < blockSize_) {
        wetMono = wetBlockA_[outPos_];
    }
    wetMono *= wetLevel_;

    // Stereo decorrelation: short wet-only delay taps + a bit of damping.
    stereoDelay_[stereoPos_] = wetMono;
    const std::size_t size = stereoDelay_.size();
    const auto tap = [&](std::size_t delaySamples) -> float {
        return stereoDelay_[(stereoPos_ + size - (delaySamples % size)) % size];
    };
    const float tapShort = tap(7);
    const float tapLong = tap(19);
    stereoLp_ = 0.25f * tapLong + 0.75f * stereoLp_;
    const float wetL = wetMono;
    const float wetR = 0.6f * tapShort + 0.4f * stereoLp_;
    stereoPos_ = (stereoPos_ + 1) % size;

    outL = dry * (1.0f - currentMix_) + wetL * currentMix_;
    outR = dry * (1.0f - currentMix_) + wetR * currentMix_;

    // Accumulate input block.
    if (inPos_ < blockSize_) {
        inBlock_[inPos_] = input;
    }
    ++inPos_;
    ++outPos_;

    if (inPos_ >= blockSize_) {
        processBlock();
        inPos_ = 0;
        outPos_ = 0;
        wetReady_ = true;
    }
}

void ConvolutionReverb::rebuildForCurrentKernels() {
    inBlock_.assign(blockSize_, 0.0f);
    wetBlockA_.assign(blockSize_, 0.0f);
    wetBlockB_.assign(blockSize_, 0.0f);
    overlapA_.assign(blockSize_, 0.0f);
    overlapB_.assign(blockSize_, 0.0f);
    convolver_.configure(blockSize_, fftSize_,
                         /*maxPartitions=*/kernels_.empty()
                              ? 1
                              : kernels_[0].partitions.size());
    // Max partitions may vary per IR; pick the maximum.
    std::size_t maxParts = 1;
    for (const auto& k : kernels_) {
        maxParts = std::max(maxParts, k.partitions.size());
    }
    convolver_.configure(blockSize_, fftSize_, maxParts);
    reset();
}

void ConvolutionReverb::processBlock() {
    convolver_.pushInputBlock(inBlock_.data());

    if (kernels_.empty()) {
        std::fill(wetBlockA_.begin(), wetBlockA_.end(), 0.0f);
        return;
    }

    const auto& a = kernels_[static_cast<std::size_t>(irIndex_)];
    convolver_.convolveWithOverlap(a, wetBlockA_.data(), overlapA_);

    if (pendingIrIndex_ < 0) {
        return;
    }

    const auto& b = kernels_[static_cast<std::size_t>(pendingIrIndex_)];
    convolver_.convolveWithOverlap(b, wetBlockB_.data(), overlapB_);

    const std::size_t totalSamples =
        std::max<std::size_t>(1, static_cast<std::size_t>(fadeTotalBlocks_) * blockSize_);
    const std::size_t base = fadeSamplePos_;
    for (std::size_t i = 0; i < blockSize_; ++i) {
        const float t =
            Clamp01(static_cast<float>(base + i) / static_cast<float>(totalSamples));
        wetBlockA_[i] = wetBlockA_[i] * (1.0f - t) + wetBlockB_[i] * t;
    }

    fadeSamplePos_ += blockSize_;
    if (fadeSamplePos_ >= totalSamples) {
        irIndex_ = pendingIrIndex_;
        pendingIrIndex_ = -1;
        fadeSamplePos_ = 0;
        overlapA_.swap(overlapB_);
        std::fill(overlapB_.begin(), overlapB_.end(), 0.0f);
    }
}

}  // namespace dsp
