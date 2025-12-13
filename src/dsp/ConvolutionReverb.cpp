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

void ConvolutionReverb::setIrKernels(std::vector<StereoConvolutionKernel> kernels) {
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
    std::fill(overlapBL_.begin(), overlapBL_.end(), 0.0f);
    std::fill(overlapBR_.begin(), overlapBR_.end(), 0.0f);
}

void ConvolutionReverb::reset() {
    convolver_.reset();
    std::fill(inBlock_.begin(), inBlock_.end(), 0.0f);
    std::fill(wetBlockAL_.begin(), wetBlockAL_.end(), 0.0f);
    std::fill(wetBlockAR_.begin(), wetBlockAR_.end(), 0.0f);
    std::fill(wetBlockBL_.begin(), wetBlockBL_.end(), 0.0f);
    std::fill(wetBlockBR_.begin(), wetBlockBR_.end(), 0.0f);
    inPos_ = 0;
    outPos_ = 0;
    wetReady_ = false;
    pendingIrIndex_ = -1;
    fadeSamplePos_ = 0;

    currentMix_ = targetMix_;
    std::fill(overlapAL_.begin(), overlapAL_.end(), 0.0f);
    std::fill(overlapAR_.begin(), overlapAR_.end(), 0.0f);
    std::fill(overlapBL_.begin(), overlapBL_.end(), 0.0f);
    std::fill(overlapBR_.begin(), overlapBR_.end(), 0.0f);

    stereoDelay_.fill(0.0f);
    stereoPos_ = 0;
    stereoLp_ = 0.0f;
}

void ConvolutionReverb::processSample(float input, float& outL, float& outR) {
    const float dry = input;
    currentMix_ += (targetMix_ - currentMix_) * mixSmoothingAlpha_;

    float wetLBase = 0.0f;
    float wetRBase = 0.0f;
    if (wetReady_ && outPos_ < blockSize_) {
        wetLBase = wetBlockAL_[outPos_];
        wetRBase = wetBlockAR_[outPos_];
    }
    wetLBase *= wetLevel_;
    wetRBase *= wetLevel_;

    float wetL = wetLBase;
    float wetR = wetRBase;
    const bool useDecorrelation =
        (!kernels_.empty() && pendingIrIndex_ < 0 && !kernels_[static_cast<std::size_t>(irIndex_)].isStereo);
    {
        // Keep decorrelator state in sync even when bypassed (prevents jumps when
        // switching between mono and stereo IRs).
        const float wetMono = 0.5f * (wetLBase + wetRBase);
        stereoDelay_[stereoPos_] = wetMono;
        const std::size_t size = stereoDelay_.size();
        const auto tap = [&](std::size_t delaySamples) -> float {
            return stereoDelay_[(stereoPos_ + size - (delaySamples % size)) % size];
        };
        const float tapShort = tap(7);
        const float tapLong = tap(19);
        stereoLp_ = 0.25f * tapLong + 0.75f * stereoLp_;
        if (useDecorrelation) {
            wetL = wetMono;
            wetR = 0.6f * tapShort + 0.4f * stereoLp_;
        }
        stereoPos_ = (stereoPos_ + 1) % size;
    }

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
    wetBlockAL_.assign(blockSize_, 0.0f);
    wetBlockAR_.assign(blockSize_, 0.0f);
    wetBlockBL_.assign(blockSize_, 0.0f);
    wetBlockBR_.assign(blockSize_, 0.0f);
    overlapAL_.assign(blockSize_, 0.0f);
    overlapAR_.assign(blockSize_, 0.0f);
    overlapBL_.assign(blockSize_, 0.0f);
    overlapBR_.assign(blockSize_, 0.0f);
    // Max partitions may vary per IR; pick the maximum.
    std::size_t maxParts = 1;
    for (const auto& k : kernels_) {
        maxParts = std::max(maxParts, k.left.partitions.size());
        if (k.isStereo) {
            maxParts = std::max(maxParts, k.right.partitions.size());
        }
    }
    convolver_.configure(blockSize_, fftSize_, maxParts);
    reset();
}

void ConvolutionReverb::processBlock() {
    convolver_.pushInputBlock(inBlock_.data());

    if (kernels_.empty()) {
        std::fill(wetBlockAL_.begin(), wetBlockAL_.end(), 0.0f);
        std::fill(wetBlockAR_.begin(), wetBlockAR_.end(), 0.0f);
        return;
    }

    const auto& a = kernels_[static_cast<std::size_t>(irIndex_)];
    convolver_.convolveWithOverlap(a.left, wetBlockAL_.data(), overlapAL_);
    if (a.isStereo) {
        convolver_.convolveWithOverlap(a.right, wetBlockAR_.data(), overlapAR_);
    } else {
        std::copy(wetBlockAL_.begin(), wetBlockAL_.end(), wetBlockAR_.begin());
    }

    if (pendingIrIndex_ < 0) {
        return;
    }

    const auto& b = kernels_[static_cast<std::size_t>(pendingIrIndex_)];
    convolver_.convolveWithOverlap(b.left, wetBlockBL_.data(), overlapBL_);
    if (b.isStereo) {
        convolver_.convolveWithOverlap(b.right, wetBlockBR_.data(), overlapBR_);
    } else {
        std::copy(wetBlockBL_.begin(), wetBlockBL_.end(), wetBlockBR_.begin());
    }

    const std::size_t totalSamples =
        std::max<std::size_t>(1, static_cast<std::size_t>(fadeTotalBlocks_) * blockSize_);
    const std::size_t base = fadeSamplePos_;
    for (std::size_t i = 0; i < blockSize_; ++i) {
        const float t =
            Clamp01(static_cast<float>(base + i) / static_cast<float>(totalSamples));
        wetBlockAL_[i] = wetBlockAL_[i] * (1.0f - t) + wetBlockBL_[i] * t;
        wetBlockAR_[i] = wetBlockAR_[i] * (1.0f - t) + wetBlockBR_[i] * t;
    }

    fadeSamplePos_ += blockSize_;
    if (fadeSamplePos_ >= totalSamples) {
        irIndex_ = pendingIrIndex_;
        pendingIrIndex_ = -1;
        fadeSamplePos_ = 0;
        overlapAL_.swap(overlapBL_);
        overlapAR_.swap(overlapBR_);
        std::fill(overlapBL_.begin(), overlapBL_.end(), 0.0f);
        std::fill(overlapBR_.begin(), overlapBR_.end(), 0.0f);
    }
}

}  // namespace dsp
