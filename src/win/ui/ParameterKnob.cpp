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
      onChange_(std::move(onChange)) {
    bounds_ = D2D1::RectF(0.0f, 0.0f, 0.0f, 0.0f);
}

void ParameterKnob::setBounds(const D2D1_RECT_F& bounds) {
    bounds_ = bounds;
    debugRectsValid_ = false;
}

void ParameterKnob::draw(ID2D1HwndRenderTarget* target,
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

    const float width = bounds_.right - bounds_.left;
    const float height = bounds_.bottom - bounds_.top;
    if (width <= 0.0f || height <= 0.0f) {
        return;
    }

    // 布局：bounds_ 内只容纳“真实体积”——圆盘 + label + padding；
    // tooltip 允许溢出 bounds_。
    const float outerPaddingX = 8.0f;
    const float outerPaddingTop = 8.0f;
    const float outerPaddingBottom = 18.0f;  // 保证底部有足够留白

    const float contentLeft = bounds_.left + outerPaddingX;
    const float contentRight = bounds_.right - outerPaddingX;
    const float contentTop = bounds_.top + outerPaddingTop;
    const float contentBottom = bounds_.bottom - outerPaddingBottom;
    const auto contentRect =
        D2D1::RectF(contentLeft, contentTop, contentRight, contentBottom);
    const float contentWidth = contentRight - contentLeft;
    const float contentHeight = contentBottom - contentTop;
    if (contentWidth <= 0.0f || contentHeight <= 0.0f) {
        return;
    }

    // 几何约束：
    // - 在 contentRect 内，值槽外缘与一个隐含正方形相切；
    // - 从圆心到外缘，按“值槽厚度 : 空隙 : 圆盘半径 ≈ 2 : 1 : 5”比例分配；
    // - 圆盘高度与 label 高度大致 3 : 1，并在底部保留 18px 留白。

    const float topMargin = 8.0f;     // contentTop 到值槽外缘的距离
    const float bottomGap = 6.0f;     // 值槽外缘到底部 label 之间的间隙
    const float minLabelHeight = 18.0f;
    const float maxLabelHeight = 200.0f;

    const float halfContentWidth = contentWidth * 0.5f;
    const float sideMargin = 4.0f;  // 值槽外缘距离 contentRect 左右边的空间

    // 先以宽度为基准，假设值槽外缘半径。
    float outerRadius = halfContentWidth - sideMargin;
    if (outerRadius <= 0.0f) {
        return;
    }

    // 根据“半径:间隙:槽厚 ≈ 5:1:2”拆分。
    float radius = outerRadius * (5.0f / 8.0f);
    float slotGap = outerRadius * (1.0f / 8.0f);
    float slotThicknessBase = outerRadius * (2.0f / 8.0f);

    // 依据圆盘大小估算 label 高度，使圆盘:label 接近 2.5:1。
    float labelHeight = (2.0f * radius) / 3.0f;
    labelHeight =
        std::clamp(labelHeight, minLabelHeight, maxLabelHeight);

    // 如果高度不够容纳“上边距 + 值槽直径 + 间隙 + label”，则整体缩放。
    const float availableForCircleAndLabel =
        contentHeight - topMargin - bottomGap;
    if (availableForCircleAndLabel <= 0.0f) {
        return;
    }

    const float requiredHeightWithoutMargins =
        2.0f * outerRadius + labelHeight;
    if (requiredHeightWithoutMargins > availableForCircleAndLabel) {
        const float scale =
            availableForCircleAndLabel / requiredHeightWithoutMargins;
        if (scale <= 0.0f) {
            return;
        }
        outerRadius *= scale;
        radius *= scale;
        slotGap *= scale;
        slotThicknessBase *= scale;
        labelHeight *= scale;
    }

    const float centerX = (contentLeft + contentRight) * 0.5f;
    const float centerY = contentTop + topMargin + outerRadius;
    const D2D1_POINT_2F center =
        D2D1::Point2F(centerX, centerY);

    // Label 紧贴值槽外缘下方（与外缘之间留出 bottomGap）。
    const float labelTop = center.y + outerRadius + bottomGap;
    // 为 label 文本预留上下内边距，改善视觉呼吸感
    const float labelPadY = std::clamp((textFormat ? textFormat->GetFontSize() : 18.0f) * 0.18f, 3.0f, 12.0f);
    const auto labelOuterRect = D2D1::RectF(
        contentLeft,
        labelTop,
        contentRight,
        labelTop + labelHeight + labelPadY * 2.0f);
    const auto labelTextRect = D2D1::RectF(
        labelOuterRect.left,
        labelOuterRect.top + labelPadY,
        labelOuterRect.right,
        labelOuterRect.bottom - labelPadY);
    target->DrawText(label_.c_str(), static_cast<UINT32>(label_.size()),
                     textFormat, labelTextRect, textBrush);

    // 旋钮圆盘
    ID2D1SolidColorBrush* fill = fillBrush;             // 圆盘
    const bool active = hovered_ || dragging_;

    // 避免与背景撞色：外圈和槽线优先使用高对比度的 accent/text 颜色。
    ID2D1SolidColorBrush* ringBrush =
        accentBrush ? accentBrush : textBrush;          // 外圈
    ID2D1SolidColorBrush* slotBrush =
        accentBrush ? accentBrush : textBrush;          // 外圈值槽
    ID2D1SolidColorBrush* pointerBrush =
        textBrush ? textBrush : accentBrush;            // 指针优先用文本色，保证对比度

    // hover / 拖拽时让值槽变亮，而不是改变旋钮尺寸。
    if (active && accentBrush) {
        slotBrush = accentBrush;
    }

    const D2D1_ELLIPSE ellipse{center, radius, radius};
    target->FillEllipse(ellipse, fill);
    target->DrawEllipse(ellipse, ringBrush, 1.5f);

    // 归一化数值并限制到 [0, 1]，避免非法范围。
    const float range = max_ - min_;
    const float norm =
        range != 0.0f ? (value_ - min_) / range : 0.0f;
    const float clampedNorm = Clamp(norm, 0.0f, 1.0f);

    const float startAngle = -kPi * 1.25f;    // -225°
    const float sweep = kPi * 1.5f;           // 270°
    const float slotRadius =
        radius + slotGap + slotThicknessBase * 0.5f;  // 值槽中心线半径

    // 外圈值槽：在 270° 扫角范围内画一条略大于旋钮的弧线。
    // 使用 PathGeometry + ArcSegment 绘制连续圆弧，避免由多段线条拼接导致的“折线感”。
    if (slotBrush && clampedNorm > 0.001f) {
        const float slotThickness = slotThicknessBase;  // 粗细固定，不随状态变化

        const float endAngle = startAngle + sweep * clampedNorm;
        const float arcAngle = std::fabs(endAngle - startAngle);
        if (arcAngle > 1e-3f) {
            const D2D1_POINT_2F startPoint = D2D1::Point2F(
                center.x + std::cos(startAngle) * slotRadius,
                center.y + std::sin(startAngle) * slotRadius);
            const D2D1_POINT_2F endPoint = D2D1::Point2F(
                center.x + std::cos(endAngle) * slotRadius,
                center.y + std::sin(endAngle) * slotRadius);

            ComPtr<ID2D1Factory> factory;
            target->GetFactory(&factory);
            if (factory) {
                ComPtr<ID2D1PathGeometry> geometry;
                if (SUCCEEDED(factory->CreatePathGeometry(&geometry))) {
                    ComPtr<ID2D1GeometrySink> sink;
                    if (SUCCEEDED(geometry->Open(&sink))) {
                        sink->SetFillMode(D2D1_FILL_MODE_ALTERNATE);
                        sink->BeginFigure(startPoint,
                                          D2D1_FIGURE_BEGIN_HOLLOW);

                        D2D1_ARC_SEGMENT arc{};
                        arc.point = endPoint;
                        arc.size = D2D1::SizeF(slotRadius, slotRadius);
                        arc.rotationAngle = 0.0f;
                        arc.sweepDirection =
                            D2D1_SWEEP_DIRECTION_CLOCKWISE;
                        arc.arcSize = (arcAngle >= kPi)
                                          ? D2D1_ARC_SIZE_LARGE
                                          : D2D1_ARC_SIZE_SMALL;

                        sink->AddArc(arc);
                        sink->EndFigure(D2D1_FIGURE_END_OPEN);
                        sink->Close();

                        target->DrawGeometry(geometry.Get(), slotBrush,
                                             slotThickness);
                    }
                }
            }
        }
    }

    const float angle = startAngle + sweep * clampedNorm;
    const float pointerLen = radius * 0.8f;
    const float pointerWidth = dragging_ ? 3.0f : 2.0f;
    const D2D1_POINT_2F pointerEnd = D2D1::Point2F(
        center.x + std::cos(angle) * pointerLen,
        center.y + std::sin(angle) * pointerLen);
    target->DrawLine(center, pointerEnd, pointerBrush, pointerWidth);

    // 数值文本只在拖拽时以“底部 tooltip”形式显示在 label 下方。
    if (dragging_) {
        // 显示百分比（四舍五入到 1%）
        float norm = 0.0f;
        const float range = (max_ - min_);
        if (range > 1e-6f) {
            norm = (value_ - min_) / range;
        }
        norm = std::clamp(norm, 0.0f, 1.0f);
        const int percent = static_cast<int>(std::lround(norm * 100.0f));
        wchar_t valueBuffer[64];
        std::swprintf(valueBuffer, std::size(valueBuffer), L"%d%%", percent);

        // 单行度量
        float lhW = 0.0f, lhH = textFormat ? textFormat->GetFontSize() : 18.0f;
        (void)MeasureText(textFormat, L"Hg", 2, 10000.0f, &lhW, &lhH);

        float labelW = 0.0f, labelH = lhH;
        (void)MeasureText(textFormat, label_.c_str(), (UINT32)label_.size(), 10000.0f, &labelW, &labelH);

                float valueW = 0.0f, valueH = lhH;
        (void)MeasureText(textFormat, valueBuffer, (UINT32)wcslen(valueBuffer), 10000.0f, &valueW, &valueH);

        // 右列使用“最大可能宽度”稳定布局（减少拖拽时宽度抖动）
        float w100 = 0.0f, hTmp = lhH;
        (void)MeasureText(textFormat, L"100%", 4, 10000.0f, &w100, &hTmp);
        float w88 = 0.0f; hTmp = lhH;
        (void)MeasureText(textFormat, L"88%", 3, 10000.0f, &w88, &hTmp);
        float w0 = 0.0f; hTmp = lhH;
        (void)MeasureText(textFormat, L"0%", 2, 10000.0f, &w0, &hTmp);
        const float valueWMax = std::max(w100, std::max(w88, w0));

        // 宽度与上限（不超过“旋钮外缘直径的 2 倍”）
        const float knobWidth = outerRadius * 2.0f;
        const float tooltipMaxWidth = knobWidth * 2.0f;
        const float innerPadX = std::clamp(lhH * 0.22f, 4.0f, 10.0f);
        const float colGap = innerPadX; // 列间中缝

        // 估算理想宽度（含两侧 padding 各 1×、列内左右 padding 共 4×、中缝 1×）
        const float epsilon = 1.0f; // 保险像素，避免边界裁切
        float desiredWidth = labelW + valueWMax + innerPadX * 4.0f + colGap;
        float tooltipWidth = std::max(knobWidth, desiredWidth);
        tooltipWidth = std::min(tooltipWidth, tooltipMaxWidth);
        tooltipWidth = std::ceil(tooltipWidth) + 1.0f; // 向上取整并+1px保险，避免早截断

        const float innerPadY = std::clamp(lhH * 0.18f, 3.0f, 12.0f);
        const float tooltipHeight = labelHeight + innerPadY * 2.0f; // 含上下内边距

        const float tooltipLeft = center.x - tooltipWidth * 0.5f;
        const float tooltipTop = labelOuterRect.bottom + std::clamp(lhH * 0.18f, 3.0f, 12.0f);
        const auto tooltipRect = D2D1::RectF(
            tooltipLeft,
            tooltipTop,
            tooltipLeft + tooltipWidth,
            tooltipTop + tooltipHeight);

        // 背景
        if (baseBrush) {
            const auto bubble = D2D1::RoundedRect(tooltipRect, 4.0f, 4.0f);
            target->FillRoundedRectangle(bubble, baseBrush);
        }

        // 列宽分配：保证两列最小宽度（各含 2*padding），其余宽度按需分配
        const float minLeft = labelW + innerPadX * 2.0f + epsilon;
        const float minRight = valueWMax + innerPadX * 2.0f + epsilon;
        float leftWidth = std::max(minLeft, tooltipWidth - (colGap + minRight));
        float rightWidth = tooltipWidth - colGap - leftWidth;
        if (rightWidth < minRight) {
            rightWidth = minRight;
            leftWidth = std::max(minLeft, tooltipWidth - (colGap + rightWidth));
        }

        // 左列矩形（垂直居中交由 TextFormat 的 ParagraphAlignment=CENTER）
        const auto leftRect = D2D1::RectF(
            tooltipRect.left + innerPadX,
            tooltipRect.top + innerPadY,
            tooltipRect.left + leftWidth - innerPadX,
            tooltipRect.bottom - innerPadY);
        target->DrawText(label_.c_str(), static_cast<UINT32>(label_.size()),
                         textFormat, leftRect, textBrush);

        // 右列矩形
        const auto rightRect = D2D1::RectF(
            leftRect.right + colGap,
            tooltipRect.top + innerPadY,
            tooltipRect.right - innerPadX,
            tooltipRect.bottom - innerPadY);
        ID2D1SolidColorBrush* valueBrush = accentBrush ? accentBrush : textBrush;
        target->DrawText(valueBuffer, static_cast<UINT32>(wcslen(valueBuffer)),
                         textFormat, rightRect, valueBrush);
    }


    // 调试叠加：以统一盒模型样式绘制外框。
#if SATORI_UI_DEBUG_ENABLED
    const auto slotRect = D2D1::RectF(center.x - outerRadius,
                                      center.y - outerRadius,
                                      center.x + outerRadius,
                                      center.y + outerRadius);
    updateDebugRects(bounds_, slotRect, labelOuterRect);
#endif
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
