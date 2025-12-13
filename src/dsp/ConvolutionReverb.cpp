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

void AddInPlace(const float* src, float* dst, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        dst[i] += src[i];
    }
}

void Clear(float* dst, std::size_t count) { std::fill(dst, dst + count, 0.0f); }
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
    std::fill(overlapLateBL_.begin(), overlapLateBL_.end(), 0.0f);
    std::fill(overlapLateBR_.begin(), overlapLateBR_.end(), 0.0f);
    std::fill(scheduledBL_.begin(), scheduledBL_.end(), 0.0f);
    std::fill(scheduledBR_.begin(), scheduledBR_.end(), 0.0f);
}

void ConvolutionReverb::reset() {
    convolverEarly_.reset();
    convolverLate_.reset();
    std::fill(inBlock_.begin(), inBlock_.end(), 0.0f);
    std::fill(wetBlockAL_.begin(), wetBlockAL_.end(), 0.0f);
    std::fill(wetBlockAR_.begin(), wetBlockAR_.end(), 0.0f);
    std::fill(wetBlockBL_.begin(), wetBlockBL_.end(), 0.0f);
    std::fill(wetBlockBR_.begin(), wetBlockBR_.end(), 0.0f);
    std::fill(lateInBlock_.begin(), lateInBlock_.end(), 0.0f);
    std::fill(lateOutAL_.begin(), lateOutAL_.end(), 0.0f);
    std::fill(lateOutAR_.begin(), lateOutAR_.end(), 0.0f);
    std::fill(lateOutBL_.begin(), lateOutBL_.end(), 0.0f);
    std::fill(lateOutBR_.begin(), lateOutBR_.end(), 0.0f);
    std::fill(scheduledAL_.begin(), scheduledAL_.end(), 0.0f);
    std::fill(scheduledAR_.begin(), scheduledAR_.end(), 0.0f);
    std::fill(scheduledBL_.begin(), scheduledBL_.end(), 0.0f);
    std::fill(scheduledBR_.begin(), scheduledBR_.end(), 0.0f);
    inPos_ = 0;
    outPos_ = 0;
    wetReady_ = false;
    pendingIrIndex_ = -1;
    fadeSamplePos_ = 0;
    lateInPos_ = 0;
    blockIndex_ = 0;

    currentMix_ = targetMix_;
    std::fill(overlapAL_.begin(), overlapAL_.end(), 0.0f);
    std::fill(overlapAR_.begin(), overlapAR_.end(), 0.0f);
    std::fill(overlapBL_.begin(), overlapBL_.end(), 0.0f);
    std::fill(overlapBR_.begin(), overlapBR_.end(), 0.0f);
    std::fill(overlapLateAL_.begin(), overlapLateAL_.end(), 0.0f);
    std::fill(overlapLateAR_.begin(), overlapLateAR_.end(), 0.0f);
    std::fill(overlapLateBL_.begin(), overlapLateBL_.end(), 0.0f);
    std::fill(overlapLateBR_.begin(), overlapLateBR_.end(), 0.0f);

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

void ConvolutionReverb::processBlockWet(const float* input,
                                       float* outWetL,
                                       float* outWetR) {
    if (!input || !outWetL || !outWetR || blockSize_ == 0) {
        return;
    }
    if (inBlock_.size() != blockSize_ ||
        wetBlockAL_.size() != blockSize_ ||
        wetBlockAR_.size() != blockSize_) {
        rebuildForCurrentKernels();
    }

    std::copy(input, input + blockSize_, inBlock_.begin());
    processBlock();  // fills wetBlockA* for this block (early+late+xfade), advances blockIndex_

    const bool useDecorrelation =
        (!kernels_.empty() && pendingIrIndex_ < 0 &&
         !kernels_[static_cast<std::size_t>(irIndex_)].isStereo);

    for (std::size_t i = 0; i < blockSize_; ++i) {
        float wetLBase = wetBlockAL_[i] * wetLevel_;
        float wetRBase = wetBlockAR_[i] * wetLevel_;

        float wetL = wetLBase;
        float wetR = wetRBase;
        {
            // Keep decorrelator state in sync even when bypassed.
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

        outWetL[i] = wetL;
        outWetR[i] = wetR;
    }
}

void ConvolutionReverb::rebuildForCurrentKernels() {
    static_assert(kLateBlocksPerChunk * 256 == kLateBlockSize,
                  "Late block size must be an integer multiple of the early block size");
    static_assert(kLateStartBlocks == kLateBlocksPerChunk,
                  "Late start blocks assumes tail begins after one late chunk");
    static_assert((kScheduleBlocks & (kScheduleBlocks - 1)) == 0,
                  "Schedule ring must be power-of-two");

    inBlock_.assign(blockSize_, 0.0f);
    wetBlockAL_.assign(blockSize_, 0.0f);
    wetBlockAR_.assign(blockSize_, 0.0f);
    wetBlockBL_.assign(blockSize_, 0.0f);
    wetBlockBR_.assign(blockSize_, 0.0f);
    overlapAL_.assign(blockSize_, 0.0f);
    overlapAR_.assign(blockSize_, 0.0f);
    overlapBL_.assign(blockSize_, 0.0f);
    overlapBR_.assign(blockSize_, 0.0f);
    overlapLateAL_.assign(kLateBlockSize, 0.0f);
    overlapLateAR_.assign(kLateBlockSize, 0.0f);
    overlapLateBL_.assign(kLateBlockSize, 0.0f);
    overlapLateBR_.assign(kLateBlockSize, 0.0f);

    lateInBlock_.assign(kLateBlockSize, 0.0f);
    lateOutAL_.assign(kLateBlockSize, 0.0f);
    lateOutAR_.assign(kLateBlockSize, 0.0f);
    lateOutBL_.assign(kLateBlockSize, 0.0f);
    lateOutBR_.assign(kLateBlockSize, 0.0f);

    scheduledAL_.assign(kScheduleBlocks * blockSize_, 0.0f);
    scheduledAR_.assign(kScheduleBlocks * blockSize_, 0.0f);
    scheduledBL_.assign(kScheduleBlocks * blockSize_, 0.0f);
    scheduledBR_.assign(kScheduleBlocks * blockSize_, 0.0f);

    // Max partitions may vary per IR; pick the maximum per stage.
    std::size_t maxPartsEarly = 1;
    std::size_t maxPartsLate = 1;
    for (const auto& k : kernels_) {
        maxPartsEarly = std::max(maxPartsEarly, k.left.partitions.size());
        if (k.isStereo) {
            maxPartsEarly = std::max(maxPartsEarly, k.right.partitions.size());
        }
        if (k.hasLate) {
            maxPartsLate = std::max(maxPartsLate, k.leftLate.partitions.size());
            if (k.isStereo) {
                maxPartsLate = std::max(maxPartsLate, k.rightLate.partitions.size());
            }
        }
    }
    convolverEarly_.configure(blockSize_, fftSize_, maxPartsEarly);
    convolverLate_.configure(kLateBlockSize, kLateFftSize, maxPartsLate);
    reset();
}

void ConvolutionReverb::processBlock() {
    convolverEarly_.pushInputBlock(inBlock_.data());

    const std::size_t scheduleMask = kScheduleBlocks - 1;
    const std::size_t scheduleSlot =
        static_cast<std::size_t>(blockIndex_ & scheduleMask);
    const std::size_t scheduleOffset = scheduleSlot * blockSize_;

    if (kernels_.empty()) {
        std::fill(wetBlockAL_.begin(), wetBlockAL_.end(), 0.0f);
        std::fill(wetBlockAR_.begin(), wetBlockAR_.end(), 0.0f);
        Clear(&scheduledAL_[scheduleOffset], blockSize_);
        Clear(&scheduledAR_[scheduleOffset], blockSize_);
        Clear(&scheduledBL_[scheduleOffset], blockSize_);
        Clear(&scheduledBR_[scheduleOffset], blockSize_);
        ++blockIndex_;
        return;
    }

    const auto& a = kernels_[static_cast<std::size_t>(irIndex_)];
    convolverEarly_.convolveWithOverlap(a.left, wetBlockAL_.data(), overlapAL_);
    if (a.isStereo) {
        convolverEarly_.convolveWithOverlap(a.right, wetBlockAR_.data(), overlapAR_);
    } else {
        std::copy(wetBlockAL_.begin(), wetBlockAL_.end(), wetBlockAR_.begin());
    }

    // Add scheduled late-tail contributions for this output block.
    AddInPlace(&scheduledAL_[scheduleOffset], wetBlockAL_.data(), blockSize_);
    AddInPlace(&scheduledAR_[scheduleOffset], wetBlockAR_.data(), blockSize_);
    Clear(&scheduledAL_[scheduleOffset], blockSize_);
    Clear(&scheduledAR_[scheduleOffset], blockSize_);

    if (pendingIrIndex_ >= 0) {
        const auto& b = kernels_[static_cast<std::size_t>(pendingIrIndex_)];
        convolverEarly_.convolveWithOverlap(b.left, wetBlockBL_.data(), overlapBL_);
        if (b.isStereo) {
            convolverEarly_.convolveWithOverlap(b.right, wetBlockBR_.data(), overlapBR_);
        } else {
            std::copy(wetBlockBL_.begin(), wetBlockBL_.end(), wetBlockBR_.begin());
        }

        AddInPlace(&scheduledBL_[scheduleOffset], wetBlockBL_.data(), blockSize_);
        AddInPlace(&scheduledBR_[scheduleOffset], wetBlockBR_.data(), blockSize_);
        Clear(&scheduledBL_[scheduleOffset], blockSize_);
        Clear(&scheduledBR_[scheduleOffset], blockSize_);
    } else {
        Clear(&scheduledBL_[scheduleOffset], blockSize_);
        Clear(&scheduledBR_[scheduleOffset], blockSize_);
    }

    // Late-stage input accumulation (every 8 blocks => 2048 samples).
    if (lateInBlock_.size() == kLateBlockSize && lateInPos_ + blockSize_ <= kLateBlockSize) {
        std::copy(inBlock_.begin(), inBlock_.end(),
                  lateInBlock_.begin() + static_cast<std::ptrdiff_t>(lateInPos_));
        lateInPos_ += blockSize_;
        if (lateInPos_ >= kLateBlockSize) {
            convolverLate_.pushInputBlock(lateInBlock_.data());

            // Convolve and schedule for A.
            if (a.hasLate && !a.leftLate.partitions.empty()) {
                convolverLate_.convolveWithOverlap(a.leftLate, lateOutAL_.data(), overlapLateAL_);
                if (a.isStereo) {
                    convolverLate_.convolveWithOverlap(a.rightLate, lateOutAR_.data(), overlapLateAR_);
                } else {
                    std::copy(lateOutAL_.begin(), lateOutAL_.end(), lateOutAR_.begin());
                }

                const std::uint64_t targetStartBlock = blockIndex_ + 1;
                for (std::size_t b = 0; b < kLateBlocksPerChunk; ++b) {
                    const std::uint64_t targetBlock = targetStartBlock + b;
                    const std::size_t slot =
                        static_cast<std::size_t>(targetBlock & scheduleMask);
                    const std::size_t off = slot * blockSize_;
                    const std::size_t srcOff = b * blockSize_;
                    for (std::size_t i = 0; i < blockSize_; ++i) {
                        scheduledAL_[off + i] += lateOutAL_[srcOff + i];
                        scheduledAR_[off + i] += lateOutAR_[srcOff + i];
                    }
                }
            }

            // Convolve and schedule for B during crossfade.
            if (pendingIrIndex_ >= 0) {
                const auto& b = kernels_[static_cast<std::size_t>(pendingIrIndex_)];
                if (b.hasLate && !b.leftLate.partitions.empty()) {
                    convolverLate_.convolveWithOverlap(b.leftLate, lateOutBL_.data(), overlapLateBL_);
                    if (b.isStereo) {
                        convolverLate_.convolveWithOverlap(b.rightLate, lateOutBR_.data(), overlapLateBR_);
                    } else {
                        std::copy(lateOutBL_.begin(), lateOutBL_.end(), lateOutBR_.begin());
                    }

                    const std::uint64_t targetStartBlock = blockIndex_ + 1;
                    for (std::size_t bb = 0; bb < kLateBlocksPerChunk; ++bb) {
                        const std::uint64_t targetBlock = targetStartBlock + bb;
                        const std::size_t slot =
                            static_cast<std::size_t>(targetBlock & scheduleMask);
                        const std::size_t off = slot * blockSize_;
                        const std::size_t srcOff = bb * blockSize_;
                        for (std::size_t i = 0; i < blockSize_; ++i) {
                            scheduledBL_[off + i] += lateOutBL_[srcOff + i];
                            scheduledBR_[off + i] += lateOutBR_[srcOff + i];
                        }
                    }
                }
            }

            lateInPos_ = 0;
        }
    }

    // Crossfade between kernels if needed.
    if (pendingIrIndex_ >= 0) {
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
            overlapLateAL_.swap(overlapLateBL_);
            overlapLateAR_.swap(overlapLateBR_);
            scheduledAL_.swap(scheduledBL_);
            scheduledAR_.swap(scheduledBR_);
            std::fill(overlapBL_.begin(), overlapBL_.end(), 0.0f);
            std::fill(overlapBR_.begin(), overlapBR_.end(), 0.0f);
            std::fill(overlapLateBL_.begin(), overlapLateBL_.end(), 0.0f);
            std::fill(overlapLateBR_.begin(), overlapLateBR_.end(), 0.0f);
            std::fill(scheduledBL_.begin(), scheduledBL_.end(), 0.0f);
            std::fill(scheduledBR_.begin(), scheduledBR_.end(), 0.0f);
        }
    }

    ++blockIndex_;
}

}  // namespace dsp
