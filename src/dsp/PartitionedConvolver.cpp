#include "dsp/PartitionedConvolver.h"

#include <algorithm>
#include <cmath>

namespace dsp {

void PartitionedConvolver::configure(std::size_t blockSize,
                                     std::size_t fftSize,
                                     std::size_t maxPartitions) {
    blockSize_ = blockSize;
    fftSize_ = fftSize;
    ringSize_ = std::max<std::size_t>(1, maxPartitions);
    ringIndex_ = 0;

    fft_.resize(fftSize_);

    xRing_.assign(ringSize_, std::vector<std::complex<float>>(fftSize_));
    workTime_.assign(fftSize_, {});
    workFreq_.assign(fftSize_, {});
    accFreq_.assign(fftSize_, {});
    overlap_.assign(blockSize_, 0.0f);
}

void PartitionedConvolver::reset() {
    for (auto& frame : xRing_) {
        std::fill(frame.begin(), frame.end(), std::complex<float>(0.0f, 0.0f));
    }
    std::fill(overlap_.begin(), overlap_.end(), 0.0f);
    ringIndex_ = 0;
}

void PartitionedConvolver::pushInputBlock(const float* input) {
    if (!input || blockSize_ == 0 || fftSize_ == 0 || xRing_.empty()) {
        return;
    }

    // Time buffer (zero-padded).
    for (std::size_t i = 0; i < blockSize_; ++i) {
        workTime_[i] = std::complex<float>(input[i], 0.0f);
    }
    for (std::size_t i = blockSize_; i < fftSize_; ++i) {
        workTime_[i] = std::complex<float>(0.0f, 0.0f);
    }

    workFreq_ = workTime_;
    fft_.forward(workFreq_);

    // Store in ring.
    auto& dst = xRing_[ringIndex_];
    dst = workFreq_;
    ringIndex_ = (ringIndex_ + 1) % ringSize_;
}

void PartitionedConvolver::convolve(const ConvolutionKernel& kernel, float* out) {
    if (!out || blockSize_ == 0 || fftSize_ == 0 || xRing_.empty()) {
        return;
    }
    if (kernel.partitions.empty()) {
        std::fill(out, out + blockSize_, 0.0f);
        return;
    }

    const std::size_t partCount = kernel.partitions.size();
    std::fill(accFreq_.begin(), accFreq_.end(), std::complex<float>(0.0f, 0.0f));

    // ringIndex_ points to the next write; most recent block is ringIndex_-1.
    for (std::size_t p = 0; p < partCount; ++p) {
        const std::size_t idx =
            (ringIndex_ + ringSize_ - 1 - p) % ringSize_;
        const auto& X = xRing_[idx];
        const auto& H = kernel.partitions[p];
        if (H.size() != fftSize_ || X.size() != fftSize_) {
            continue;
        }
        for (std::size_t k = 0; k < fftSize_; ++k) {
            accFreq_[k] += X[k] * H[k];
        }
    }

    workTime_ = accFreq_;
    fft_.inverse(workTime_);

    // Overlap-add: output first block, keep second block as overlap.
    for (std::size_t i = 0; i < blockSize_; ++i) {
        const float v = workTime_[i].real();
        out[i] = v + overlap_[i];
        overlap_[i] = workTime_[i + blockSize_].real();
    }
}

ConvolutionKernel PartitionedConvolver::buildKernelFromIr(const std::vector<float>& ir,
                                                          std::size_t blockSize,
                                                          std::size_t fftSize) {
    ConvolutionKernel kernel;
    if (ir.empty() || blockSize == 0 || fftSize == 0) {
        return kernel;
    }
    const std::size_t partCount =
        (ir.size() + blockSize - 1) / blockSize;
    kernel.partitions.assign(partCount, std::vector<std::complex<float>>(fftSize));

    Fft fft(fftSize);
    std::vector<std::complex<float>> time(fftSize);
    for (std::size_t p = 0; p < partCount; ++p) {
        const std::size_t offset = p * blockSize;
        const std::size_t copyCount =
            std::min(blockSize, ir.size() - offset);

        for (std::size_t i = 0; i < copyCount; ++i) {
            time[i] = std::complex<float>(ir[offset + i], 0.0f);
        }
        for (std::size_t i = copyCount; i < fftSize; ++i) {
            time[i] = std::complex<float>(0.0f, 0.0f);
        }

        auto freq = time;
        fft.forward(freq);
        kernel.partitions[p] = std::move(freq);
    }
    return kernel;
}

}  // namespace dsp

