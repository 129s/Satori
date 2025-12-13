#include "dsp/ConvolutionReverb.h"

#include <algorithm>
#include <cmath>

namespace dsp {

namespace {
float Clamp01(float v) { return std::max(0.0f, std::min(1.0f, v)); }
}  // namespace

void ConvolutionReverb::setSampleRate(double sampleRate) {
    if (sampleRate <= 0.0) {
        return;
    }
    sampleRate_ = sampleRate;
    // block size stays fixed for now; we may tune later.
    rebuildForCurrentKernels();
}

void ConvolutionReverb::setMix(float mix01) { mix_ = Clamp01(mix01); }

void ConvolutionReverb::setIrKernels(std::vector<ConvolutionKernel> kernels) {
    kernels_ = std::move(kernels);
    if (kernels_.empty()) {
        irIndex_ = 0;
    } else {
        irIndex_ = std::clamp(irIndex_, 0, static_cast<int>(kernels_.size() - 1));
    }
    pendingIrIndex_ = -1;
    fadeBlockPos_ = 0;
    rebuildForCurrentKernels();
}

void ConvolutionReverb::setIrIndex(int index) {
    if (kernels_.empty()) {
        irIndex_ = 0;
        pendingIrIndex_ = -1;
        fadeBlockPos_ = 0;
        return;
    }
    index = std::clamp(index, 0, static_cast<int>(kernels_.size() - 1));
    if (index == irIndex_) {
        return;
    }
    pendingIrIndex_ = index;
    fadeBlockPos_ = 0;
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
    fadeBlockPos_ = 0;

    stereoDelay_.fill(0.0f);
    stereoPos_ = 0;
    stereoLp_ = 0.0f;
}

void ConvolutionReverb::processSample(float input, float& outL, float& outR) {
    const float dry = input;

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

    outL = dry * (1.0f - mix_) + wetL * mix_;
    outR = dry * (1.0f - mix_) + wetR * mix_;

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
    convolver_.convolve(a, wetBlockA_.data());

    if (pendingIrIndex_ < 0) {
        return;
    }

    const auto& b = kernels_[static_cast<std::size_t>(pendingIrIndex_)];
    convolver_.convolve(b, wetBlockB_.data());

    const float t = fadeTotalBlocks_ > 0
                        ? Clamp01(static_cast<float>(fadeBlockPos_) /
                                  static_cast<float>(fadeTotalBlocks_))
                        : 1.0f;
    for (std::size_t i = 0; i < blockSize_; ++i) {
        wetBlockA_[i] = wetBlockA_[i] * (1.0f - t) + wetBlockB_[i] * t;
    }

    ++fadeBlockPos_;
    if (fadeBlockPos_ >= fadeTotalBlocks_) {
        irIndex_ = pendingIrIndex_;
        pendingIrIndex_ = -1;
        fadeBlockPos_ = 0;
    }
}

}  // namespace dsp
