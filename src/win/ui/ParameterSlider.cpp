#include "win/ui/ParameterSlider.h"

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <string>

#include <d2d1helper.h>

namespace winui {

namespace {
float Clamp(float value, float min, float max) {
    return std::max(min, std::min(max, value));
}
}  // namespace

ParameterSlider::ParameterSlider(std::wstring label,
                                 float min,
                                 float max,
                                 float initialValue,
                                 Callback onChange)
    : label_(std::move(label)),
      min_(min),
      max_(max),
      value_(Clamp(initialValue, min, max)),
      onChange_(std::move(onChange)) {
    bounds_ = D2D1::RectF(0, 0, 0, 0);
    trackRect_ = bounds_;
}

void ParameterSlider::setBounds(const D2D1_RECT_F& bounds) {
    bounds_ = bounds;
    trackRect_ = D2D1::RectF(bounds.left,
                             bounds.bottom - 28.0f,
                             bounds.right,
                             bounds.bottom - 10.0f);
}

void ParameterSlider::draw(ID2D1HwndRenderTarget* target,
                           ID2D1SolidColorBrush* trackBrush,
                           ID2D1SolidColorBrush* fillBrush,
                           ID2D1SolidColorBrush* knobBrush,
                           ID2D1SolidColorBrush* textBrush,
                           IDWriteTextFormat* textFormat) const {
    if (!target || !trackBrush || !fillBrush || !knobBrush || !textBrush ||
        !textFormat) {
        return;
    }

    // 根据 hover/drag 状态调整视觉权重，保持与按钮/键盘一致的反馈层级
    ID2D1SolidColorBrush* track = trackBrush;
    ID2D1SolidColorBrush* fill = fillBrush;
    ID2D1SolidColorBrush* knob = knobBrush;
    if (hovered_ && !dragging_) {
        // hover：略提升填充亮度
        track = fillBrush;
    }
    if (dragging_) {
        // 按下/拖拽：使用强调色突出滑块与已选区
        track = fillBrush;
        fill = knobBrush;
    }

    // Draw label
    D2D1_RECT_F labelRect =
        D2D1::RectF(bounds_.left, bounds_.top, bounds_.right, bounds_.top + 24.0f);
    target->DrawText(label_.c_str(), static_cast<UINT32>(label_.size()),
                     textFormat, labelRect, textBrush);

    // Draw track and fill
    target->FillRectangle(trackRect_, track);
    const float width = trackRect_.right - trackRect_.left;
    const float normalized = (value_ - min_) / (max_ - min_);
    D2D1_RECT_F fillRect = trackRect_;
    fillRect.right = trackRect_.left + normalized * width;
    target->FillRectangle(fillRect, fill);

    // Draw knob
    const float knobWidth = 6.0f;
    D2D1_RECT_F knobRect =
        D2D1::RectF(fillRect.right - knobWidth, trackRect_.top - 6.0f,
                    fillRect.right + knobWidth, trackRect_.bottom + 6.0f);
    target->FillRectangle(knobRect, knob);

    // Draw value text
    wchar_t buffer[64];
    std::swprintf(buffer, std::size(buffer), L"%.3f", value_);
    D2D1_RECT_F valueRect =
        D2D1::RectF(bounds_.left, trackRect_.bottom + 4.0f, bounds_.right,
                    bounds_.bottom);
    target->DrawText(buffer, static_cast<UINT32>(wcslen(buffer)), textFormat,
                     valueRect, textBrush);
}

bool ParameterSlider::hitTest(float x, float y) const {
    return x >= bounds_.left && x <= bounds_.right && y >= bounds_.top &&
           y <= bounds_.bottom;
}

float ParameterSlider::positionToValue(float x) const {
    const float width = trackRect_.right - trackRect_.left;
    if (width <= 0.0f) {
        return min_;
    }
    const float ratio = Clamp((x - trackRect_.left) / width, 0.0f, 1.0f);
    return min_ + ratio * (max_ - min_);
}

void ParameterSlider::setValue(float value, bool notify) {
    value = Clamp(value, min_, max_);
    if (std::abs(value - value_) < 1e-4f) {
        return;
    }
    value_ = value;
    if (notify && onChange_) {
        onChange_(value_);
    }
}

bool ParameterSlider::onPointerDown(float x, float y) {
    if (!hitTest(x, y)) {
        return false;
    }
    dragging_ = true;
    hovered_ = true;
    setValue(positionToValue(x), true);
    return true;
}

bool ParameterSlider::onPointerMove(float x, float y) {
    const bool inside = hitTest(x, y);
    bool changed = false;

    if (dragging_) {
        setValue(positionToValue(x), true);
        changed = true;
    }

    if (hovered_ != inside) {
        hovered_ = inside;
        changed = true;
    }

    return changed;
}

void ParameterSlider::onPointerUp() {
    dragging_ = false;
    // 保留 hovered_ 状态，由后续鼠标移动事件更新
}

void ParameterSlider::syncValue(float value) {
    setValue(value, false);
}

}  // namespace winui
