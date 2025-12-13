#include <catch2/catch_amalgamated.hpp>

#include <cmath>

#include "dsp/Fft.h"
#include "dsp/PartitionedConvolver.h"
#include "dsp/ConvolutionReverb.h"

TEST_CASE("FFT roundtrip preserves samples (approx)", "[dsp][fft]") {
    dsp::Fft fft(8);
    std::vector<std::complex<float>> data(8);
    for (std::size_t i = 0; i < data.size(); ++i) {
        data[i] = std::complex<float>(std::sin(static_cast<float>(i) * 0.7f), 0.0f);
    }
    auto freq = data;
    fft.forward(freq);
    fft.inverse(freq);
    for (std::size_t i = 0; i < data.size(); ++i) {
        REQUIRE(freq[i].real() == Catch::Approx(data[i].real()).margin(1e-4));
        REQUIRE(freq[i].imag() == Catch::Approx(0.0f).margin(1e-4));
    }
}

TEST_CASE("Partitioned convolver reproduces IR for impulse input", "[dsp][convolution]") {
    const std::size_t block = 8;
    const std::size_t fftSize = 16;

    std::vector<float> ir = {1.0f, 0.5f, 0.25f, 0.0f, 0.0f, 0.1f, 0.0f, 0.0f,
                             0.05f, 0.0f, 0.0f, 0.0f};
    const auto kernel = dsp::PartitionedConvolver::buildKernelFromIr(ir, block, fftSize);

    dsp::PartitionedConvolver conv;
    conv.configure(block, fftSize, /*maxPartitions=*/kernel.partitions.size());
    conv.reset();

    std::vector<float> in(block, 0.0f);
    in[0] = 1.0f;
    std::vector<float> out(block, 0.0f);

    conv.pushInputBlock(in.data());
    conv.convolve(kernel, out.data());

    // First block matches IR[0..block-1]
    for (std::size_t i = 0; i < block; ++i) {
        const float expected = i < ir.size() ? ir[i] : 0.0f;
        REQUIRE(out[i] == Catch::Approx(expected).margin(1e-4));
    }

    // Second block matches IR[block..2*block-1]
    std::fill(in.begin(), in.end(), 0.0f);
    conv.pushInputBlock(in.data());
    conv.convolve(kernel, out.data());
    for (std::size_t i = 0; i < block; ++i) {
        const std::size_t irIdx = block + i;
        const float expected = irIdx < ir.size() ? ir[irIdx] : 0.0f;
        REQUIRE(out[i] == Catch::Approx(expected).margin(1e-4));
    }
}

TEST_CASE("ConvolutionReverb mix=0 passes dry", "[dsp][reverb]") {
    const std::size_t block = 8;
    const std::size_t fftSize = 16;
    std::vector<float> ir(block * 2, 0.0f);
    ir[0] = 1.0f;

    std::vector<dsp::ConvolutionKernel> kernels;
    kernels.push_back(dsp::PartitionedConvolver::buildKernelFromIr(ir, block, fftSize));

    dsp::ConvolutionReverb reverb;
    reverb.setIrKernels(std::move(kernels));
    reverb.setMix(0.0f);

    for (int i = 0; i < 20; ++i) {
        float in = (i % 3 == 0) ? 0.3f : -0.2f;
        float outL = 0.0f, outR = 0.0f;
        reverb.processSample(in, outL, outR);
        REQUIRE(outL == Catch::Approx(in).epsilon(1e-6));
        REQUIRE(outR == Catch::Approx(in).epsilon(1e-6));
    }
}
