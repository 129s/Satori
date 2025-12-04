#pragma once

#include <cmath>

#include <d2d1.h>

namespace winui {

inline float AlignToPixelCenter(float value) {
    return std::floor(value) + 0.5f;
}

inline D2D1_RECT_F AlignRectToPixel(const D2D1_RECT_F& rect) {
    return D2D1::RectF(AlignToPixelCenter(rect.left),
                       AlignToPixelCenter(rect.top),
                       AlignToPixelCenter(rect.right),
                       AlignToPixelCenter(rect.bottom));
}

class ScopedAntialiasMode {
public:
    ScopedAntialiasMode(ID2D1RenderTarget* target,
                        D2D1_ANTIALIAS_MODE mode)
        : target_(target) {
        if (target_) {
            previous_ = target_->GetAntialiasMode();
            target_->SetAntialiasMode(mode);
        }
    }

    ~ScopedAntialiasMode() {
        if (target_) {
            target_->SetAntialiasMode(previous_);
        }
    }

    ScopedAntialiasMode(const ScopedAntialiasMode&) = delete;
    ScopedAntialiasMode& operator=(const ScopedAntialiasMode&) = delete;

private:
    ID2D1RenderTarget* target_ = nullptr;
    D2D1_ANTIALIAS_MODE previous_ = D2D1_ANTIALIAS_MODE_PER_PRIMITIVE;
};

}  // namespace winui
