#pragma once

#include <functional>
#include <memory>
#include <string>

#include <d2d1.h>
#include <dwrite.h>

namespace winui {

class ParameterSlider {
public:
    using Callback = std::function<void(float)>;

    ParameterSlider(std::wstring label,
                    float min,
                    float max,
                    float initialValue,
                    Callback onChange);

    void setBounds(const D2D1_RECT_F& bounds);
    void draw(ID2D1HwndRenderTarget* target,
              ID2D1SolidColorBrush* trackBrush,
              ID2D1SolidColorBrush* fillBrush,
              ID2D1SolidColorBrush* knobBrush,
              ID2D1SolidColorBrush* textBrush,
              IDWriteTextFormat* textFormat) const;

    bool onPointerDown(float x, float y);
    bool onPointerMove(float x, float y);
    void onPointerUp();
    void syncValue(float value);

    float value() const { return value_; }

private:
    bool hitTest(float x, float y) const;
    float positionToValue(float x) const;
    void setValue(float value, bool notify);

    std::wstring label_;
    float min_;
    float max_;
    float value_;
    Callback onChange_;
    bool dragging_ = false;

    D2D1_RECT_F bounds_{};
    D2D1_RECT_F trackRect_{};
};

}  // namespace winui
