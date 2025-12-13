#include "win/ui/ParameterKnob.h"

#include <algorithm>
#include <cmath>
#include <cwchar>

#include <d2d1helper.h>
#include <wrl/client.h>

namespace winui {

// 文本测量工具：在没有外部工厂的情况下，本地懒加载一个 DWriteFactory
namespace {
Microsoft::WRL::ComPtr<IDWriteFactory> GetLocalDWriteFactory() {
    static Microsoft::WRL::ComPtr<IDWriteFactory> s_factory;
    if (!s_factory) {
        (void)DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                                  __uuidof(IDWriteFactory),
                                  &s_factory);
    }
    return s_factory;
}

static bool MeasureText(IDWriteTextFormat* format,
                        const wchar_t* text,
                        UINT32 length,
                        float maxWidth,
                        float* outWidth,
                        float* outHeight) {
    auto f = GetLocalDWriteFactory();
    if (!f || !format || !text || !outWidth || !outHeight) return false;
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    HRESULT hr = f->CreateTextLayout(text, length, format,
                                     maxWidth, 10000.0f, &layout);
    if (FAILED(hr) || !layout) return false;
    // Mirror draw behavior
    layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    DWRITE_TRIMMING trimming{};
    trimming.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER;
    Microsoft::WRL::ComPtr<IDWriteInlineObject> ellipsis;
    if (SUCCEEDED(f->CreateEllipsisTrimmingSign(format, &ellipsis))) {
        layout->SetTrimming(&trimming, ellipsis.Get());
    }
    DWRITE_TEXT_METRICS m{};
    hr = layout->GetMetrics(&m);
    if (FAILED(hr)) return false;
    *outWidth = m.widthIncludingTrailingWhitespace;
    *outHeight = m.height;
    return true;
}} // namespace (local)\n
namespace {
float Clamp(float value, float min, float max) {
    return std::max(min, std::min(max, value));
}

bool IsRectValid(const D2D1_RECT_F& rect) {
    return rect.right > rect.left && rect.bottom > rect.top;
}
}  // namespace

struct ParameterKnob::Layout {
    float contentLeft = 0.0f;
    float contentRight = 0.0f;
    float contentTop = 0.0f;
    float contentBottom = 0.0f;
    float contentWidth = 0.0f;
    float contentHeight = 0.0f;
    float outerRadius = 0.0f;
    float radius = 0.0f;
    float slotGap = 0.0f;
    float slotThicknessBase = 0.0f;
    float labelHeight = 0.0f;
    float labelPadY = 0.0f;
    float labelTextWidth = 0.0f;
    D2D1_POINT_2F center{};
    D2D1_RECT_F labelOuterRect{};
    D2D1_RECT_F labelTextRect{};
};

using Microsoft::WRL::ComPtr;

ParameterKnob::ParameterKnob(std::wstring label,
                             float min,
                             float max,
                             float initialValue,
                             Callback onChange)
    : label_(std::move(label)),
      min_(min),
      max_(max),
      value_(Clamp(initialValue, min, max)),
      defaultValue_(Clamp(initialValue, min, max)),
      onChange_(std::move(onChange)) {
    bounds_ = D2D1::RectF(0.0f, 0.0f, 0.0f, 0.0f);
}

void ParameterKnob::setBounds(const D2D1_RECT_F& bounds) {
    bounds_ = bounds;
    debugRectsValid_ = false;
}

bool ParameterKnob::computeLayout(IDWriteTextFormat* textFormat,
                                  Layout* outLayout,
                                  float* outLineHeight) const {
    if (!outLayout) {
        return false;
    }
    *outLayout = Layout{};

    const float width = bounds_.right - bounds_.left;
    const float height = bounds_.bottom - bounds_.top;
    if (width <= 0.0f || height <= 0.0f) {
        return false;
    }

    const float outerPaddingX = 8.0f;
    const float outerPaddingTop = 8.0f;
    const float outerPaddingBottom = 18.0f;  // 保证底部有足够留白

    outLayout->contentLeft = bounds_.left + outerPaddingX;
    outLayout->contentRight = bounds_.right - outerPaddingX;
    outLayout->contentTop = bounds_.top + outerPaddingTop;
    outLayout->contentBottom = bounds_.bottom - outerPaddingBottom;
    outLayout->contentWidth = outLayout->contentRight - outLayout->contentLeft;
    outLayout->contentHeight =
        outLayout->contentBottom - outLayout->contentTop;
    if (outLayout->contentWidth <= 0.0f || outLayout->contentHeight <= 0.0f) {
        return false;
    }

    const float topMargin = 8.0f;     // contentTop 到值槽外缘的距离
    const float bottomGap = 6.0f;     // 值槽外缘到底部 label 之间的间隙
    const float minLabelHeight = 18.0f;
    const float maxLabelHeight = 200.0f;

    const float halfContentWidth = outLayout->contentWidth * 0.5f;
    const float sideMargin = 4.0f;  // 值槽外缘距离 contentRect 左右边的空间

    float outerRadius = halfContentWidth - sideMargin;
    if (outerRadius <= 0.0f) {
        return false;
    }

    outLayout->radius = outerRadius * (5.0f / 8.0f);
    outLayout->slotGap = outerRadius * (1.0f / 8.0f);
    outLayout->slotThicknessBase = outerRadius * (2.0f / 8.0f);

    outLayout->labelHeight =
        std::clamp((2.0f * outLayout->radius) / 3.0f, minLabelHeight,
                   maxLabelHeight);

    const float availableForCircleAndLabel =
        outLayout->contentHeight - topMargin - bottomGap;
    if (availableForCircleAndLabel <= 0.0f) {
        return false;
    }

    const float requiredHeightWithoutMargins =
        2.0f * outerRadius + outLayout->labelHeight;
    if (requiredHeightWithoutMargins > availableForCircleAndLabel) {
        const float scale =
            availableForCircleAndLabel / requiredHeightWithoutMargins;
        if (scale <= 0.0f) {
            return false;
        }
        outerRadius *= scale;
        outLayout->radius *= scale;
        outLayout->slotGap *= scale;
        outLayout->slotThicknessBase *= scale;
        outLayout->labelHeight *= scale;
    }

    outLayout->outerRadius = outerRadius;

    const float centerX =
        (outLayout->contentLeft + outLayout->contentRight) * 0.5f;
    const float centerY = outLayout->contentTop + topMargin + outerRadius;
    outLayout->center = D2D1::Point2F(centerX, centerY);

    float lineHeight = textFormat ? textFormat->GetFontSize() : 18.0f;
    float lhW = 0.0f;
    float lhH = lineHeight;
    if (MeasureText(textFormat, L"Hg", 2, 10000.0f, &lhW, &lhH)) {
        if (lhH > 0.0f) {
            lineHeight = lhH;
        }
    }
    if (outLineHeight) {
        *outLineHeight = lineHeight;
    }

    outLayout->labelPadY = std::clamp(lineHeight * 0.18f, 3.0f, 12.0f);
    const float labelTop = outLayout->center.y + outerRadius + bottomGap;
    outLayout->labelOuterRect = D2D1::RectF(
        outLayout->contentLeft, labelTop, outLayout->contentRight,
        labelTop + outLayout->labelHeight + outLayout->labelPadY * 2.0f);
    const float availableLabelWidth =
        outLayout->labelOuterRect.right - outLayout->labelOuterRect.left;
    if (availableLabelWidth <= 0.0f) {
        return false;
    }

    float measuredLabelW = 0.0f;
    float measuredLabelH = lineHeight;
    if (MeasureText(textFormat, label_.c_str(),
                    static_cast<UINT32>(label_.size()), 10000.0f,
                    &measuredLabelW, &measuredLabelH) &&
        measuredLabelW > 0.0f) {
        outLayout->labelTextWidth = measuredLabelW;
    } else {
        outLayout->labelTextWidth = availableLabelWidth;
    }
    const float labelWidth =
        std::clamp(outLayout->labelTextWidth, 1.0f, availableLabelWidth);
    const float desiredLabelLeft = outLayout->center.x - labelWidth * 0.5f;
    const float minLabelLeft = outLayout->labelOuterRect.left;
    const float maxLabelLeft = outLayout->labelOuterRect.right - labelWidth;
    const float labelLeft =
        std::clamp(desiredLabelLeft, minLabelLeft, maxLabelLeft);
    outLayout->labelTextRect = D2D1::RectF(
        labelLeft, outLayout->labelOuterRect.top + outLayout->labelPadY,
        labelLeft + labelWidth,
        outLayout->labelOuterRect.bottom - outLayout->labelPadY);

    return true;
}

void ParameterKnob::draw(ID2D1HwndRenderTarget* target,
                         ID2D1SolidColorBrush* baseBrush,
                         ID2D1SolidColorBrush* fillBrush,
                         ID2D1SolidColorBrush* accentBrush,
                         ID2D1SolidColorBrush* textBrush,
                         IDWriteTextFormat* textFormat) const {
    drawBody(target, baseBrush, fillBrush, accentBrush, textBrush, textFormat);
    drawTooltip(target, baseBrush, fillBrush, accentBrush, textBrush,
                textFormat);
}

void ParameterKnob::drawBody(ID2D1HwndRenderTarget* target,
                             ID2D1SolidColorBrush* baseBrush,
                             ID2D1SolidColorBrush* fillBrush,
                             ID2D1SolidColorBrush* accentBrush,
                             ID2D1SolidColorBrush* textBrush,
                             IDWriteTextFormat* textFormat) const {
    if (!target || !baseBrush || !fillBrush || !accentBrush || !textBrush ||
        !textFormat) {
        return;
    }

    debugRectsValid_ = false;

    constexpr float kPi = 3.1415926f;

    Layout layout{};
    if (!computeLayout(textFormat, &layout, nullptr)) {
        return;
    }

    auto dwriteFactory = GetLocalDWriteFactory();
    bool labelDrawn = false;
    if (dwriteFactory) {
        ComPtr<IDWriteTextLayout> labelLayout;
        if (SUCCEEDED(dwriteFactory->CreateTextLayout(
                label_.c_str(), static_cast<UINT32>(label_.size()),
                textFormat,
                layout.labelTextRect.right - layout.labelTextRect.left,
                layout.labelTextRect.bottom - layout.labelTextRect.top,
                &labelLayout)) &&
            labelLayout) {
            labelLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            labelLayout->SetParagraphAlignment(
                DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            labelLayout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

            // Auto-fit label to avoid ellipsis/truncation.
            DWRITE_TEXT_METRICS metrics{};
            if (SUCCEEDED(labelLayout->GetMetrics(&metrics))) {
                const float availW =
                    layout.labelTextRect.right - layout.labelTextRect.left;
                if (metrics.widthIncludingTrailingWhitespace > availW + 0.5f) {
                    const float baseSize = textFormat->GetFontSize();
                    const float scale =
                        availW / std::max(1.0f, metrics.widthIncludingTrailingWhitespace);
                    const float newSize = std::max(12.0f, baseSize * scale);
                    const DWRITE_TEXT_RANGE range{
                        0u, static_cast<UINT32>(label_.size())};
                    (void)labelLayout->SetFontSize(newSize, range);
                }
            }
            target->DrawTextLayout(
                D2D1::Point2F(layout.labelTextRect.left,
                              layout.labelTextRect.top),
                labelLayout.Get(), textBrush);
            labelDrawn = true;
        }
    }
    if (!labelDrawn) {
        target->DrawText(label_.c_str(), static_cast<UINT32>(label_.size()),
                         textFormat, layout.labelTextRect, textBrush);
    }

    const bool active = hovered_ || dragging_;

    ID2D1SolidColorBrush* ringBrush = baseBrush ? baseBrush : fillBrush;
    ID2D1SolidColorBrush* slotBrush = accentBrush ? accentBrush : textBrush;
    ID2D1SolidColorBrush* discBrush = fillBrush ? fillBrush : baseBrush;

    // Subtle inner disc for depth.
    const D2D1_ELLIPSE ellipse{layout.center, layout.radius, layout.radius};
    if (discBrush) {
        const float original = discBrush->GetOpacity();
        discBrush->SetOpacity(active ? 0.95f : 0.85f);
        target->FillEllipse(ellipse, discBrush);
        discBrush->SetOpacity(original);
    }

    // 归一化数值并限制到 [0, 1]，避免非法范围。
    const float range = max_ - min_;
    const float norm =
        range != 0.0f ? (value_ - min_) / range : 0.0f;
    const float clampedNorm = Clamp(norm, 0.0f, 1.0f);

    const float startAngle = -kPi * 1.25f;    // -225°
    const float sweep = kPi * 1.5f;           // 270°
    const float slotRadius = layout.radius + layout.slotGap +
                             layout.slotThicknessBase *
                                 0.5f;  // 值槽中心线半径

    const float slotThickness = layout.slotThicknessBase;

    ComPtr<ID2D1Factory> d2dFactory;
    target->GetFactory(&d2dFactory);
    auto drawArc = [&](float a0, float a1, ID2D1SolidColorBrush* brush) {
        if (!d2dFactory || !brush) return;
        const float arcAngle = std::fabs(a1 - a0);
        if (arcAngle < 1e-3f) return;
        const D2D1_POINT_2F startPoint = D2D1::Point2F(
            layout.center.x + std::cos(a0) * slotRadius,
            layout.center.y + std::sin(a0) * slotRadius);
        const D2D1_POINT_2F endPoint = D2D1::Point2F(
            layout.center.x + std::cos(a1) * slotRadius,
            layout.center.y + std::sin(a1) * slotRadius);
        ComPtr<ID2D1PathGeometry> geometry;
        if (SUCCEEDED(d2dFactory->CreatePathGeometry(&geometry)) && geometry) {
            ComPtr<ID2D1GeometrySink> sink;
            if (SUCCEEDED(geometry->Open(&sink)) && sink) {
                sink->SetFillMode(D2D1_FILL_MODE_ALTERNATE);
                sink->BeginFigure(startPoint, D2D1_FIGURE_BEGIN_HOLLOW);
                D2D1_ARC_SEGMENT arc{};
                arc.point = endPoint;
                arc.size = D2D1::SizeF(slotRadius, slotRadius);
                arc.rotationAngle = 0.0f;
                arc.sweepDirection = D2D1_SWEEP_DIRECTION_CLOCKWISE;
                arc.arcSize =
                    (arcAngle >= kPi) ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL;
                sink->AddArc(arc);
                sink->EndFigure(D2D1_FIGURE_END_OPEN);
                (void)sink->Close();
                target->DrawGeometry(geometry.Get(), brush, slotThickness);
            }
        }
    };

    // Background ring (full sweep, dim).
    if (ringBrush) {
        const float original = ringBrush->GetOpacity();
        ringBrush->SetOpacity(active ? 0.95f : 0.70f);
        drawArc(startAngle, startAngle + sweep, ringBrush);
        ringBrush->SetOpacity(original);
    }

    // Foreground progress arc + endpoint dot.
    if (slotBrush && clampedNorm > 0.001f) {
        const float endAngle = startAngle + sweep * clampedNorm;
        drawArc(startAngle, endAngle, slotBrush);
        const D2D1_POINT_2F endPoint = D2D1::Point2F(
            layout.center.x + std::cos(endAngle) * slotRadius,
            layout.center.y + std::sin(endAngle) * slotRadius);
        // 端点圆的直径需要与弧线截面（strokeWidth）一致，否则默认状态会显得“缩一圈”。
        const float dotR = slotThickness * 0.50f;
        target->FillEllipse(D2D1::Ellipse(endPoint, dotR, dotR), slotBrush);
    }

    // 调试叠加：以统一盒模型样式绘制外框。
#if SATORI_UI_DEBUG_ENABLED
    const auto slotRect = D2D1::RectF(layout.center.x - layout.outerRadius,
                                      layout.center.y - layout.outerRadius,
                                      layout.center.x + layout.outerRadius,
                                      layout.center.y + layout.outerRadius);
    updateDebugRects(bounds_, slotRect, layout.labelOuterRect);
#endif
}

void ParameterKnob::drawTooltip(ID2D1HwndRenderTarget* target,
                                ID2D1SolidColorBrush* baseBrush,
                                ID2D1SolidColorBrush* fillBrush,
                                ID2D1SolidColorBrush* accentBrush,
                                ID2D1SolidColorBrush* textBrush,
                                IDWriteTextFormat* textFormat) const {
    (void)fillBrush;
    if (!dragging_ || !target || !baseBrush || !textBrush || !textFormat) {
        return;
    }

    Layout layout{};
    float lineHeight = 0.0f;
    if (!computeLayout(textFormat, &layout, &lineHeight)) {
        return;
    }

    float norm = 0.0f;
    const float range = (max_ - min_);
    if (range > 1e-6f) {
        norm = (value_ - min_) / range;
    }
    norm = std::clamp(norm, 0.0f, 1.0f);
    const int percent = static_cast<int>(std::lround(norm * 100.0f));
    wchar_t valueBuffer[64];
    std::swprintf(valueBuffer, std::size(valueBuffer), L"%d%%", percent);

    float lhW = 0.0f;
    float lhH = lineHeight;
    (void)MeasureText(textFormat, L"Hg", 2, 10000.0f, &lhW, &lhH);

    float labelW = layout.labelTextWidth > 0.0f ? layout.labelTextWidth : lhH;
    float labelH = lhH;
    (void)MeasureText(textFormat, label_.c_str(),
                      static_cast<UINT32>(label_.size()), 10000.0f, &labelW,
                      &labelH);
    (void)labelH;

    float valueW = 0.0f;
    float valueH = lhH;
    (void)MeasureText(textFormat, valueBuffer,
                      static_cast<UINT32>(wcslen(valueBuffer)), 10000.0f,
                      &valueW, &valueH);
    (void)valueH;

    float w100 = 0.0f;
    float hTmp = lhH;
    (void)MeasureText(textFormat, L"100%", 4, 10000.0f, &w100, &hTmp);
    float w88 = 0.0f;
    hTmp = lhH;
    (void)MeasureText(textFormat, L"88%", 3, 10000.0f, &w88, &hTmp);
    float w0 = 0.0f;
    hTmp = lhH;
    (void)MeasureText(textFormat, L"0%", 2, 10000.0f, &w0, &hTmp);
    const float valueWMax = std::max(w100, std::max(w88, w0));

    const float knobWidth = layout.outerRadius * 2.0f;
    const float tooltipMaxWidth = knobWidth * 2.0f;
    const float innerPadX = std::clamp(lhH * 0.22f, 4.0f, 10.0f);
    const float colGap = innerPadX;  // 列间中缝

    const float epsilon = 1.0f;  // 保险像素，避免边界裁切
    float desiredWidth =
        labelW + valueWMax + innerPadX * 4.0f + colGap;
    float tooltipWidth = std::max(knobWidth, desiredWidth);
    tooltipWidth = std::min(tooltipWidth, tooltipMaxWidth);
    tooltipWidth = std::ceil(tooltipWidth) +
                   1.0f;  // 向上取整并+1px保险，避免早截断

    const float innerPadY = std::clamp(lhH * 0.18f, 3.0f, 12.0f);
    const float tooltipHeight =
        layout.labelHeight + innerPadY * 2.0f;  // 含上下内边距

    const float tooltipLeft = layout.center.x - tooltipWidth * 0.5f;
    const float tooltipTop =
        layout.labelOuterRect.bottom + std::clamp(lhH * 0.18f, 3.0f, 12.0f);
    const auto tooltipRect = D2D1::RectF(
        tooltipLeft, tooltipTop, tooltipLeft + tooltipWidth,
        tooltipTop + tooltipHeight);
    if (tooltipRect.right <= tooltipRect.left ||
        tooltipRect.bottom <= tooltipRect.top) {
        return;
    }

    if (baseBrush) {
        auto shadowRect = tooltipRect;
        shadowRect.top += 2.0f;
        shadowRect.bottom += 2.0f;
        ComPtr<ID2D1SolidColorBrush> shadowBrush;
        if (SUCCEEDED(target->CreateSolidColorBrush(
                D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.35f), &shadowBrush))) {
            const auto shadowBubble =
                D2D1::RoundedRect(shadowRect, 4.0f, 4.0f);
            target->FillRoundedRectangle(shadowBubble, shadowBrush.Get());
        }

        const auto bubble = D2D1::RoundedRect(tooltipRect, 4.0f, 4.0f);
        target->FillRoundedRectangle(bubble, baseBrush);
    }

    const float minLeft = labelW + innerPadX * 2.0f + epsilon;
    const float minRight = valueWMax + innerPadX * 2.0f + epsilon;
    float leftWidth = std::max(minLeft, tooltipWidth - (colGap + minRight));
    float rightWidth = tooltipWidth - colGap - leftWidth;
    if (rightWidth < minRight) {
        rightWidth = minRight;
        leftWidth = std::max(minLeft, tooltipWidth - (colGap + rightWidth));
    }

    const auto leftRect = D2D1::RectF(
        tooltipRect.left + innerPadX, tooltipRect.top + innerPadY,
        tooltipRect.left + leftWidth - innerPadX,
        tooltipRect.bottom - innerPadY);
    target->DrawText(label_.c_str(), static_cast<UINT32>(label_.size()),
                     textFormat, leftRect, textBrush);

    const auto rightRect = D2D1::RectF(
        leftRect.right + colGap, tooltipRect.top + innerPadY,
        tooltipRect.right - innerPadX, tooltipRect.bottom - innerPadY);
    ID2D1SolidColorBrush* valueBrush =
        accentBrush ? accentBrush : textBrush;
    target->DrawText(valueBuffer, static_cast<UINT32>(wcslen(valueBuffer)),
                     textFormat, rightRect, valueBrush);
}

bool ParameterKnob::hitTest(float x, float y) const {
    return x >= bounds_.left && x <= bounds_.right && y >= bounds_.top &&
           y <= bounds_.bottom;
}

void ParameterKnob::setValue(float value, bool notify) {
    value = Clamp(value, min_, max_);
    if (std::abs(value - value_) < 1e-4f) {
        return;
    }
    value_ = value;
    if (notify && onChange_) {
        onChange_(value_);
    }
}

bool ParameterKnob::onPointerDown(float x, float y) {
    if (!hitTest(x, y)) {
        return false;
    }

    // Double-click to reset to default value.
    const auto now = std::chrono::steady_clock::now();
    bool isDoubleClick = false;
    if (lastClickTime_ != std::chrono::steady_clock::time_point{}) {
        const auto dt = now - lastClickTime_;
        if (dt < std::chrono::milliseconds(450)) {
            const float dx = x - lastClickX_;
            const float dy = y - lastClickY_;
            if ((dx * dx + dy * dy) < 25.0f) {
                isDoubleClick = true;
            }
        }
    }
    lastClickTime_ = now;
    lastClickX_ = x;
    lastClickY_ = y;

    if (isDoubleClick) {
        setValue(defaultValue_, true);
        dragging_ = false;
        hovered_ = true;
        return true;
    }

    dragging_ = true;
    hovered_ = true;
    dragStartY_ = y;
    dragStartValue_ = value_;
    return true;
}

bool ParameterKnob::onPointerMove(float x, float y) {
    const bool inside = hitTest(x, y);
    bool changed = false;

    if (dragging_) {
        // 垂直拖拽控制数值，向上增大、向下减小
        const float delta = (dragStartY_ - y) * 0.004f;  // 调整灵敏度
        const float range = max_ - min_;
        setValue(dragStartValue_ + delta * range, true);
        changed = true;
    }

    // 仅在非拖拽状态下根据鼠标位置更新 hover，
    // 避免拖拽过程中一旦指针离开区域，视觉状态立刻退回“非 hover”。
    if (!dragging_ && hovered_ != inside) {
        hovered_ = inside;
        changed = true;
    }

    return changed;
}


void ParameterKnob::onPointerUp() {
    dragging_ = false;
    // hovered_ 状态交由后续鼠标移动更新
}

void ParameterKnob::syncValue(float value) {
    setValue(value, false);
}

bool ParameterKnob::contains(float x, float y) const {
    return hitTest(x, y);
}

DebugBoxModel ParameterKnob::debugBoxModel() const {
#if SATORI_UI_DEBUG_ENABLED
    DebugBoxModel model{};
    auto push = [&](DebugBoxLayer layer, const D2D1_RECT_F& rect) {
        if (IsRectValid(rect)) {
            model.segments.push_back({layer, rect});
        }
    };
    if (debugRectsValid_) {
        push(DebugBoxLayer::kBorder, debugBorderRect_);
        push(DebugBoxLayer::kPadding, debugPaddingRect_);
        push(DebugBoxLayer::kContent, debugContentRect_);
        if (!model.segments.empty()) {
            return model;
        }
    }

    push(DebugBoxLayer::kBorder, bounds_);
    const float paddingInset = 6.0f;
    const auto padding =
        D2D1::RectF(bounds_.left + paddingInset, bounds_.top + paddingInset,
                    bounds_.right - paddingInset, bounds_.bottom - paddingInset);
    push(DebugBoxLayer::kPadding, padding);
    const float contentInset = 4.0f;
    const auto content =
        D2D1::RectF(padding.left + contentInset, padding.top + contentInset,
                    padding.right - contentInset,
                    padding.bottom - contentInset);
    push(DebugBoxLayer::kContent, content);
    return model;
#else
    return {};
#endif
}

void ParameterKnob::updateDebugRects(const D2D1_RECT_F& border,
                                     const D2D1_RECT_F& padding,
                                     const D2D1_RECT_F& content) const {
#if SATORI_UI_DEBUG_ENABLED
    debugBorderRect_ = border;
    debugPaddingRect_ = padding;
    debugContentRect_ = content;
    debugRectsValid_ = IsRectValid(border);
#else
    (void)border;
    (void)padding;
    (void)content;
#endif
}

}  // namespace winui
