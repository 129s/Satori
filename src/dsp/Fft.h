#pragma once

#include <complex>
#include <cstddef>
#include <vector>

namespace dsp {

// Minimal radix-2 FFT (in-place). Intended for small fixed sizes used by the
// partitioned convolution reverb. Not a general-purpose FFT API.
class Fft {
public:
    explicit Fft(std::size_t size = 0);

    void resize(std::size_t size);
    std::size_t size() const { return size_; }

    // Forward transform (time -> frequency).
    void forward(std::vector<std::complex<float>>& data) const;
    // Inverse transform (frequency -> time). Scales by 1/N.
    void inverse(std::vector<std::complex<float>>& data) const;

    static bool isPowerOfTwo(std::size_t n);

private:
    std::size_t size_ = 0;
    std::vector<std::size_t> bitReverse_;

    void buildBitReverse();
    void transform(std::vector<std::complex<float>>& data, bool inverse) const;
};

}  // namespace dsp

