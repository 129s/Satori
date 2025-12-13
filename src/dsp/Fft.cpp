#include "dsp/Fft.h"

#include <algorithm>
#include <cmath>

namespace dsp {

namespace {
constexpr float kPi = 3.14159265358979323846f;
}  // namespace

Fft::Fft(std::size_t size) { resize(size); }

bool Fft::isPowerOfTwo(std::size_t n) {
    return n != 0 && (n & (n - 1)) == 0;
}

void Fft::resize(std::size_t size) {
    if (size == size_) {
        return;
    }
    size_ = size;
    buildBitReverse();
}

void Fft::buildBitReverse() {
    bitReverse_.clear();
    if (size_ == 0) {
        return;
    }
    if (!isPowerOfTwo(size_)) {
        // Leave empty; transform() will no-op.
        return;
    }
    bitReverse_.resize(size_);
    std::size_t bits = 0;
    while ((static_cast<std::size_t>(1) << bits) < size_) {
        ++bits;
    }
    for (std::size_t i = 0; i < size_; ++i) {
        std::size_t x = i;
        std::size_t r = 0;
        for (std::size_t b = 0; b < bits; ++b) {
            r = (r << 1) | (x & 1);
            x >>= 1;
        }
        bitReverse_[i] = r;
    }
}

void Fft::forward(std::vector<std::complex<float>>& data) const {
    transform(data, /*inverse=*/false);
}

void Fft::inverse(std::vector<std::complex<float>>& data) const {
    transform(data, /*inverse=*/true);
    if (size_ == 0) {
        return;
    }
    const float invN = 1.0f / static_cast<float>(size_);
    for (auto& v : data) {
        v *= invN;
    }
}

void Fft::transform(std::vector<std::complex<float>>& data, bool inverse) const {
    if (size_ == 0 || data.size() != size_) {
        return;
    }
    if (bitReverse_.empty()) {
        return;
    }

    // Bit-reversal permutation.
    for (std::size_t i = 0; i < size_; ++i) {
        const std::size_t j = bitReverse_[i];
        if (j > i) {
            std::swap(data[i], data[j]);
        }
    }

    // Cooley-Tukey (iterative, radix-2).
    for (std::size_t len = 2; len <= size_; len <<= 1) {
        const float angSign = inverse ? 1.0f : -1.0f;
        const float angStep = angSign * (2.0f * kPi / static_cast<float>(len));
        const std::complex<float> wLen(std::cos(angStep), std::sin(angStep));

        for (std::size_t i = 0; i < size_; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            const std::size_t half = len >> 1;
            for (std::size_t j = 0; j < half; ++j) {
                const auto u = data[i + j];
                const auto v = data[i + j + half] * w;
                data[i + j] = u + v;
                data[i + j + half] = u - v;
                w *= wLen;
            }
        }
    }
}

}  // namespace dsp

