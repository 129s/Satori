#pragma once

#include <cstdint>

#if defined(__SSE__) || defined(_M_IX86) || defined(_M_X64)
#include <xmmintrin.h>
#endif

namespace dsp {

// Disables denormals (DAZ) and flushes subnormals to zero (FTZ) for the current
// thread. This avoids severe CPU slowdowns in long decays (common in reverbs).
class ScopedDenormalsDisable {
public:
    ScopedDenormalsDisable() {
#if defined(__SSE__) || defined(_M_IX86) || defined(_M_X64)
        oldCsr_ = static_cast<std::uint32_t>(_mm_getcsr());
        // MXCSR bits:
        // - DAZ (Denormals-Are-Zero) = bit 6  (0x0040)
        // - FTZ (Flush-To-Zero)     = bit 15 (0x8000)
        const std::uint32_t csr = oldCsr_ | 0x0040u | 0x8000u;
        _mm_setcsr(csr);
        active_ = true;
#endif
    }

    ScopedDenormalsDisable(const ScopedDenormalsDisable&) = delete;
    ScopedDenormalsDisable& operator=(const ScopedDenormalsDisable&) = delete;

    ~ScopedDenormalsDisable() {
#if defined(__SSE__) || defined(_M_IX86) || defined(_M_X64)
        if (active_) {
            _mm_setcsr(static_cast<unsigned int>(oldCsr_));
        }
#endif
    }

private:
    std::uint32_t oldCsr_ = 0;
    bool active_ = false;
};

}  // namespace dsp

