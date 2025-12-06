#include "win/ui/nodes/FlowDiagramNode.h"

#include <algorithm>
#include <cmath>

#include <d2d1helper.h>

namespace winui {

FlowDiagramNode::FlowDiagramNode() = default;

void FlowDiagramNode::setDiagramState(const FlowDiagramState& state) {
    state_ = state;
}

void FlowDiagramNode::setWaveformSamples(const std::vector<float>& samples) {
    waveformView_.setSamples(samples);
}

float FlowDiagramNode::preferredHeight(float) const {
    return preferredHeight_;
}

void FlowDiagramNode::draw(const RenderResources& resources) {
    if (!resources.target || !resources.textBrush || !resources.textFormat) {
        return;
    }
    auto* panelBrush =
        resources.panelBrush ? resources.panelBrush : resources.trackBrush;
    auto* borderBrush =
        resources.gridBrush ? resources.gridBrush : resources.accentBrush;

    if (panelBrush) {
        resources.target->FillRectangle(bounds_, panelBrush);
    }
    if (borderBrush) {
        resources.target->DrawRectangle(bounds_, borderBrush, 1.0f);
    }

    const float padding = 12.0f;
    const float gap = 12.0f;
    const float innerLeft = bounds_.left + padding;
    const float innerRight = bounds_.right - padding;
    const float innerTop = bounds_.top + padding;
    const float innerBottom = bounds_.bottom - padding;
    const float innerWidth = innerRight - innerLeft;
    const float moduleWidth = (innerWidth - 3.0f * gap) / 4.0f;
    const float moduleHeight = innerBottom - innerTop;

    D2D1_RECT_F excitationRect = D2D1::RectF(
        innerLeft, innerTop, innerLeft + moduleWidth, innerTop + moduleHeight);
    D2D1_RECT_F stringRect = D2D1::RectF(
        excitationRect.right + gap, innerTop,
        excitationRect.right + gap + moduleWidth, innerTop + moduleHeight);
    D2D1_RECT_F bodyRect = D2D1::RectF(
        stringRect.right + gap, innerTop,
        stringRect.right + gap + moduleWidth, innerTop + moduleHeight);
    D2D1_RECT_F roomRect = D2D1::RectF(
        bodyRect.right + gap, innerTop,
        bodyRect.right + gap + moduleWidth, innerTop + moduleHeight);

    // 模块边框
    if (borderBrush) {
        resources.target->DrawRectangle(excitationRect, borderBrush, 1.0f);
        resources.target->DrawRectangle(stringRect, borderBrush, 1.0f);
        resources.target->DrawRectangle(bodyRect, borderBrush, 1.0f);
        resources.target->DrawRectangle(roomRect, borderBrush, 1.0f);
    }

    // 连接箭头
    auto drawArrow = [&](const D2D1_POINT_2F& from,
                         const D2D1_POINT_2F& to) {
        if (!resources.accentBrush) {
            return;
        }
        resources.target->DrawLine(from, to, resources.accentBrush, 1.5f);
        const float angle = std::atan2(to.y - from.y, to.x - from.x);
        const float len = 6.0f;
        const float a1 = angle + 3.1415926f * 0.75f;
        const float a2 = angle - 3.1415926f * 0.75f;
        D2D1_POINT_2F p1{to.x + std::cos(a1) * len, to.y + std::sin(a1) * len};
        D2D1_POINT_2F p2{to.x + std::cos(a2) * len, to.y + std::sin(a2) * len};
        resources.target->DrawLine(to, p1, resources.accentBrush, 1.5f);
        resources.target->DrawLine(to, p2, resources.accentBrush, 1.5f);
    };

    const float midY = innerTop + moduleHeight * 0.5f;
    drawArrow(D2D1::Point2F(excitationRect.right, midY),
              D2D1::Point2F(stringRect.left, midY));
    drawArrow(D2D1::Point2F(stringRect.right, midY),
              D2D1::Point2F(bodyRect.left, midY));
    drawArrow(D2D1::Point2F(bodyRect.right, midY),
              D2D1::Point2F(roomRect.left, midY));

    drawExcitation(resources.target, resources.gridBrush, resources.accentBrush,
                   resources.textFormat, excitationRect);
    drawString(resources.target, resources.gridBrush, resources.accentBrush,
               resources.textFormat, stringRect);
    drawBody(resources.target, resources.gridBrush, resources.accentBrush,
             resources.textFormat, bodyRect);
    drawRoom(resources.target, panelBrush, resources.gridBrush,
             resources.accentBrush, resources.textFormat, roomRect);
}

void FlowDiagramNode::drawExcitation(ID2D1HwndRenderTarget* target,
                                     ID2D1SolidColorBrush* gridBrush,
                                     ID2D1SolidColorBrush* accentBrush,
                                     IDWriteTextFormat* textFormat,
                                     const D2D1_RECT_F& rect) {
    if (!target || !textFormat) {
        return;
    }
    const float padding = 6.0f;
    const auto titleRect =
        D2D1::RectF(rect.left + padding, rect.top + padding,
                    rect.right - padding, rect.top + padding + 18.0f);
    const std::wstring title = L"EXCITATION";
    target->DrawText(title.c_str(), static_cast<UINT32>(title.size()),
                     textFormat, titleRect, gridBrush ? gridBrush : accentBrush);

    const auto roleRect = D2D1::RectF(titleRect.left, titleRect.bottom + 2.0f,
                                      titleRect.right, titleRect.bottom + 16.0f);
    const std::wstring role = L"per-voice";
    if (gridBrush) {
        target->DrawText(role.c_str(), static_cast<UINT32>(role.size()),
                         textFormat, roleRect, gridBrush);
    }

    const float innerTop = roleRect.bottom + 4.0f;
    const float innerBottom = rect.bottom - padding;
    const float innerLeft = rect.left + padding;
    const float innerRight = rect.right - padding;

    // 噪声纹理：简单的竖线阵列
    if (gridBrush) {
        const float width = innerRight - innerLeft;
        const int lines = 12;
        for (int i = 0; i < lines; ++i) {
            const float x =
                innerLeft + width * (static_cast<float>(i) / (lines - 1));
            const float hFactor = (state_.noiseType == 0) ? 0.5f : 0.9f;
            const float top =
                innerTop + (1.0f - hFactor) * (innerBottom - innerTop);
            target->DrawLine(D2D1::Point2F(x, top),
                             D2D1::Point2F(x, innerBottom), gridBrush, 1.0f);
        }
    }

    // 拨弦位置：一条弦和一个可移动的击弦点
    if (accentBrush) {
        const float stringY =
            innerTop + (innerBottom - innerTop) * 0.25f;
        target->DrawLine(D2D1::Point2F(innerLeft, stringY),
                         D2D1::Point2F(innerRight, stringY), accentBrush, 1.5f);
        const float pos = std::clamp(state_.pickPosition, 0.0f, 1.0f);
        const float x = innerLeft + (innerRight - innerLeft) * pos;
        target->DrawLine(D2D1::Point2F(x, stringY - 6.0f),
                         D2D1::Point2F(x, stringY + 6.0f), accentBrush, 2.0f);
    }
}

void FlowDiagramNode::drawString(ID2D1HwndRenderTarget* target,
                                 ID2D1SolidColorBrush* gridBrush,
                                 ID2D1SolidColorBrush* accentBrush,
                                 IDWriteTextFormat* textFormat,
                                 const D2D1_RECT_F& rect) {
    if (!target || !textFormat) {
        return;
    }
    const float padding = 6.0f;
    const auto titleRect =
        D2D1::RectF(rect.left + padding, rect.top + padding,
                    rect.right - padding, rect.top + padding + 18.0f);
    const std::wstring title = L"STRING LOOP";
    target->DrawText(title.c_str(), static_cast<UINT32>(title.size()),
                     textFormat, titleRect, gridBrush ? gridBrush : accentBrush);

    const auto roleRect = D2D1::RectF(titleRect.left, titleRect.bottom + 2.0f,
                                      titleRect.right, titleRect.bottom + 16.0f);
    const std::wstring role = L"per-voice";
    if (gridBrush) {
        target->DrawText(role.c_str(), static_cast<UINT32>(role.size()),
                         textFormat, roleRect, gridBrush);
    }

    const float innerTop = roleRect.bottom + 4.0f;
    const float innerBottom = rect.bottom - padding;
    const float innerLeft = rect.left + padding;
    const float innerRight = rect.right - padding;

    if (!accentBrush) {
        return;
    }

    const float width = innerRight - innerLeft;
    const float height = innerBottom - innerTop;
    const float baseY = innerBottom;

    // 根据 Decay 绘制简化包络：越大衰减越慢，曲线越“平缓”
    const float decayNorm = std::clamp(state_.decay, 0.0f, 1.0f);
    const float sustainLevel = 0.1f + decayNorm * 0.6f;

    const D2D1_POINT_2F p0{innerLeft, baseY};
    const D2D1_POINT_2F p1{innerLeft + width * 0.1f,
                           baseY - height * 0.9f};
    const D2D1_POINT_2F p2{innerLeft + width * 0.6f,
                           baseY - height * sustainLevel};
    const D2D1_POINT_2F p3{innerRight, baseY};

    target->DrawLine(p0, p1, accentBrush, 2.0f);
    target->DrawLine(p1, p2, accentBrush, 2.0f);
    target->DrawLine(p2, p3, accentBrush, 2.0f);

    const float dispersion = std::clamp(state_.dispersionAmount, 0.0f, 1.0f);
    if (gridBrush && dispersion > 0.001f) {
        const int lines = 4;
        const float spacing = width / static_cast<float>(lines + 1);
        const float tilt = 6.0f + 18.0f * dispersion;
        for (int i = 0; i < lines; ++i) {
            const float x = innerLeft + spacing * (i + 1);
            D2D1_POINT_2F a{x, innerBottom};
            D2D1_POINT_2F b{x + tilt, innerTop + height * 0.35f};
            target->DrawLine(a, b, gridBrush, 1.0f);
        }
    }
}

void FlowDiagramNode::drawBody(ID2D1HwndRenderTarget* target,
                               ID2D1SolidColorBrush* gridBrush,
                               ID2D1SolidColorBrush* accentBrush,
                               IDWriteTextFormat* textFormat,
                               const D2D1_RECT_F& rect) {
    if (!target || !textFormat) {
        return;
    }
    const float padding = 6.0f;
    const auto titleRect =
        D2D1::RectF(rect.left + padding, rect.top + padding,
                    rect.right - padding, rect.top + padding + 18.0f);
    const std::wstring title = L"BODY (SHARED)";
    target->DrawText(title.c_str(), static_cast<UINT32>(title.size()),
                     textFormat, titleRect, gridBrush ? gridBrush : accentBrush);

    const float innerTop = titleRect.bottom + 4.0f;
    const float innerBottom = rect.bottom - padding;
    const float innerLeft = rect.left + padding;
    const float innerRight = rect.right - padding;

    const float width = innerRight - innerLeft;
    const float height = innerBottom - innerTop;

    if (gridBrush) {
        const std::wstring role = L"shared";
        const auto roleRect =
            D2D1::RectF(innerLeft, innerTop - 14.0f, innerRight, innerTop + 2.0f);
        target->DrawText(role.c_str(), static_cast<UINT32>(role.size()),
                         textFormat, roleRect, gridBrush);
    }

    if (!accentBrush) {
        return;
    }

    const float tone = std::clamp(state_.bodyTone, 0.0f, 1.0f);
    const float shelfHeight = innerTop + height * (0.3f + 0.2f * tone);
    const float midHeight = innerTop + height * (0.55f - 0.25f * tone);

    D2D1_RECT_F lowShelf = D2D1::RectF(innerLeft, shelfHeight, innerLeft + width * 0.3f,
                                       innerBottom);
    D2D1_RECT_F midPeak = D2D1::RectF(innerLeft + width * 0.32f, midHeight,
                                      innerLeft + width * 0.68f, innerBottom);
    D2D1_RECT_F highShelf = D2D1::RectF(innerLeft + width * 0.7f,
                                        innerTop + height * 0.35f, innerRight, innerBottom);

    target->DrawRectangle(lowShelf, accentBrush, 1.5f);
    target->DrawRectangle(midPeak, accentBrush, 1.5f);
    target->DrawRectangle(highShelf, accentBrush, 1.5f);
}

void FlowDiagramNode::drawRoom(ID2D1HwndRenderTarget* target,
                               ID2D1SolidColorBrush* panelBrush,
                               ID2D1SolidColorBrush* gridBrush,
                               ID2D1SolidColorBrush* accentBrush,
                               IDWriteTextFormat* textFormat,
                               const D2D1_RECT_F& rect) {
    if (!target || !textFormat) {
        return;
    }
    const float padding = 6.0f;
    const auto titleRect =
        D2D1::RectF(rect.left + padding, rect.top + padding,
                    rect.right - padding, rect.top + padding + 18.0f);
    const std::wstring title = L"ROOM / OUTPUT";
    target->DrawText(title.c_str(), static_cast<UINT32>(title.size()),
                     textFormat, titleRect, gridBrush ? gridBrush : accentBrush);

    const float innerTop = titleRect.bottom + 4.0f;
    const float innerBottom = rect.bottom - padding;
    const float innerLeft = rect.left + padding;
    const float innerRight = rect.right - padding;

    if (gridBrush) {
        const std::wstring role = L"shared";
        const auto roleRect =
            D2D1::RectF(innerLeft, innerTop - 14.0f, innerRight, innerTop + 2.0f);
        target->DrawText(role.c_str(), static_cast<UINT32>(role.size()),
                         textFormat, roleRect, gridBrush);
    }

    waveformView_.setBounds(
        D2D1::RectF(innerLeft, innerTop, innerRight, innerBottom));
    waveformView_.draw(target,
                       panelBrush ? panelBrush : gridBrush,
                       gridBrush ? gridBrush : accentBrush,
                       accentBrush ? accentBrush : gridBrush);

    if (accentBrush) {
        const float room = std::clamp(state_.roomAmount, 0.0f, 1.0f);
        const float barWidth = 6.0f;
        const float barHeight = (innerBottom - innerTop) * (0.2f + 0.6f * room);
        const float x = innerRight - barWidth - 2.0f;
        const float y = innerBottom - barHeight;
        D2D1_RECT_F roomBar = D2D1::RectF(x, y, x + barWidth, innerBottom);
        target->FillRectangle(roomBar, accentBrush);
    }
}

}  // namespace winui
