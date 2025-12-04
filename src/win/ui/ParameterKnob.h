#pragma once

#include <functional>
#include <string>

#include <d2d1.h>
#include <dwrite.h>

namespace winui {

class ParameterKnob {
public:
    using Callback = std::function<void(float)>;

    ParameterKnob(std::wstring label,
                  float min,
                  float max,
                  float initialValue,
                  Callback onChange);

    void setBounds(const D2D1_RECT_F& bounds);
    void setDebugEnabled(bool enabled);

    void draw(ID2D1HwndRenderTarget* target,
              ID2D1SolidColorBrush* baseBrush,
              ID2D1SolidColorBrush* fillBrush,
              ID2D1SolidColorBrush* accentBrush,
              ID2D1SolidColorBrush* textBrush,
              IDWriteTextFormat* textFormat) const;

    bool onPointerDown(float x, float y);
    bool onPointerMove(float x, float y);
    void onPointerUp();

    void syncValue(float value);

private:
    bool hitTest(float x, float y) const;
    void setValue(float value, bool notify);

    std::wstring label_;
    float min_ = 0.0f;
    float max_ = 1.0f;
    float value_ = 0.0f;
    Callback onChange_;

    D2D1_RECT_F bounds_{};
    bool hovered_ = false;
    bool dragging_ = false;
    bool debugEnabled_ = false;
    float dragStartY_ = 0.0f;
    float dragStartValue_ = 0.0f;
};

}  // namespace winui
