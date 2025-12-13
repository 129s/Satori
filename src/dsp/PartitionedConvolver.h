#pragma once

#include <complex>
#include <cstddef>
#include <vector>

#include "dsp/Fft.h"

namespace dsp {

struct ConvolutionKernel {
    // Frequency-domain partitions. Each partition has fftSize bins.
    std::vector<std::vector<std::complex<float>>> partitions;
};

// Partitioned convolution with a shared input history.
// Call pushInputBlock() once per block, then convolve() with one or more kernels.
class PartitionedConvolver {
public:
    PartitionedConvolver() = default;

    void configure(std::size_t blockSize, std::size_t fftSize, std::size_t maxPartitions);
    void reset();

    std::size_t blockSize() const { return blockSize_; }
    std::size_t fftSize() const { return fftSize_; }

    void pushInputBlock(const float* input);          // input length = blockSize
    void convolve(const ConvolutionKernel& kernel, float* out);  // out length = blockSize
    void convolveWithOverlap(const ConvolutionKernel& kernel,
                             float* out,
                             std::vector<float>& overlap);  // overlap length = blockSize

    static ConvolutionKernel buildKernelFromIr(const std::vector<float>& ir,
                                               std::size_t blockSize,
                                               std::size_t fftSize);

private:
    std::size_t blockSize_ = 0;
    std::size_t fftSize_ = 0;
    std::size_t ringSize_ = 0;
    std::size_t ringIndex_ = 0;  // next write

    Fft fft_;
    std::vector<std::vector<std::complex<float>>> xRing_;  // ringSize_ x fftSize_

    std::vector<std::complex<float>> workTime_;
    std::vector<std::complex<float>> workFreq_;
    std::vector<std::complex<float>> accFreq_;
    std::vector<float> overlap_;  // blockSize_
};

}  // namespace dsp
