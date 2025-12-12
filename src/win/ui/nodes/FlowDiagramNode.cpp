#include "win/ui/nodes/FlowDiagramNode.h"

#include <algorithm>
#include <cmath>

#include <d2d1helper.h>

namespace winui {

namespace {
bool ContainsPoint(const D2D1_RECT_F& rect, float x, float y) {
    return x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom;
}
}  // namespace

FlowDiagramNode::FlowDiagramNode() = default;

void FlowDiagramNode::setDiagramState(const FlowDiagramState& state) {
    state_ = state;
}

void FlowDiagramNode::setHighlightedModule(FlowModule module) {
    state_.highlightedModule = module;
}

void FlowDiagramNode::setWaveformSamples(const std::vector<float>& samples) {
    waveformView_.setSamples(samples);
}

void FlowDiagramNode::setOnModuleSelected(
    std::function<void(FlowModule)> callback) {
    onModuleSelected_ = std::move(callback);
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

    auto fillWithOpacity = [&](ID2D1SolidColorBrush* brush,
                               const D2D1_RECT_F& rect, float opacity) {
        if (!brush || opacity <= 0.0f) {
            return;
        }
        const float original = brush->GetOpacity();
        brush->SetOpacity(opacity);
        resources.target->FillRectangle(rect, brush);
        brush->SetOpacity(original);
    };

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
    excitationRect_ = excitationRect;
    stringRect_ = stringRect;
    bodyRect_ = bodyRect;
    roomRect_ = roomRect;

    const bool highlightExcitation =
        state_.highlightedModule == FlowModule::kExcitation;
    const bool highlightString =
        state_.highlightedModule == FlowModule::kString;
    const bool highlightBody = state_.highlightedModule == FlowModule::kBody;
    const bool highlightRoom = state_.highlightedModule == FlowModule::kRoom;

    auto drawModuleBackground = [&](const D2D1_RECT_F& rect, bool shared,
                                    bool highlighted) {
        const float baseOpacity = shared ? 0.22f : 0.18f;
        const float activeOpacity = shared ? 0.32f : 0.26f;
        fillWithOpacity(shared ? resources.trackBrush : resources.fillBrush,
                        rect, highlighted ? activeOpacity : baseOpacity);
        if (highlighted) {
            fillWithOpacity(resources.accentBrush, rect, 0.12f);
        }
    };

    drawModuleBackground(excitationRect, false, highlightExcitation);
    drawModuleBackground(stringRect, false, highlightString);
    drawModuleBackground(bodyRect, true, highlightBody);
    drawModuleBackground(roomRect, true, highlightRoom);

    // 模块边框
    auto drawModuleBorder = [&](const D2D1_RECT_F& rect, bool highlighted) {
        auto* brush =
            highlighted && resources.accentBrush ? resources.accentBrush : borderBrush;
        const float thickness = highlighted ? 2.0f : 1.0f;
        if (brush) {
            resources.target->DrawRectangle(rect, brush, thickness);
        }
    };

    drawModuleBorder(excitationRect, highlightExcitation);
    drawModuleBorder(stringRect, highlightString);
    drawModuleBorder(bodyRect, highlightBody);
    drawModuleBorder(roomRect, highlightRoom);

    // 连接箭头
    auto drawArrow = [&](const D2D1_POINT_2F& from,
                         const D2D1_POINT_2F& to, bool emphasized) {
        if (!resources.accentBrush) {
            return;
        }
        const float thickness = emphasized ? 2.0f : 1.5f;
        resources.target->DrawLine(from, to, resources.accentBrush, thickness);
        const float angle = std::atan2(to.y - from.y, to.x - from.x);
        const float len = 6.0f;
        const float a1 = angle + 3.1415926f * 0.75f;
        const float a2 = angle - 3.1415926f * 0.75f;
        D2D1_POINT_2F p1{to.x + std::cos(a1) * len, to.y + std::sin(a1) * len};
        D2D1_POINT_2F p2{to.x + std::cos(a2) * len, to.y + std::sin(a2) * len};
        resources.target->DrawLine(to, p1, resources.accentBrush, thickness);
        resources.target->DrawLine(to, p2, resources.accentBrush, thickness);
    };

    const float midY = innerTop + moduleHeight * 0.5f;
    drawArrow(D2D1::Point2F(excitationRect.right, midY),
              D2D1::Point2F(stringRect.left, midY),
              highlightExcitation || highlightString);
    drawArrow(D2D1::Point2F(stringRect.right, midY),
              D2D1::Point2F(bodyRect.left, midY),
              highlightString || highlightBody);
    drawArrow(D2D1::Point2F(bodyRect.right, midY),
              D2D1::Point2F(roomRect.left, midY),
              highlightBody || highlightRoom);

    drawExcitation(resources.target, resources.textBrush, resources.gridBrush,
                   resources.excitationBrush, resources.accentBrush,
                   resources.textFormat, excitationRect, highlightExcitation);
    drawString(resources.target, resources.textBrush, resources.gridBrush,
               resources.accentBrush, resources.textFormat, stringRect,
               highlightString);
    drawBody(resources.target, resources.textBrush, resources.gridBrush,
             resources.accentBrush, resources.textFormat, bodyRect,
             highlightBody);
    drawRoom(resources.target, panelBrush, resources.textBrush,
             resources.gridBrush, resources.accentBrush, resources.textFormat,
             roomRect, highlightRoom);
}

void FlowDiagramNode::drawExcitation(ID2D1HwndRenderTarget* target,
                                     ID2D1SolidColorBrush* textBrush,
                                     ID2D1SolidColorBrush* gridBrush,
                                     ID2D1SolidColorBrush* excitationBrush,
                                     ID2D1SolidColorBrush* accentBrush,
                                     IDWriteTextFormat* textFormat,
                                     const D2D1_RECT_F& rect,
                                     bool highlighted) {
    if (!target || !textFormat) {
        return;
    }
    const float padding = 6.0f;
    const auto titleRect =
        D2D1::RectF(rect.left + padding, rect.top + padding,
                    rect.right - padding, rect.top + padding + 18.0f);
    const std::wstring title = L"EXCITATION";
    ID2D1SolidColorBrush* titleBrush =
        textBrush ? textBrush : (gridBrush ? gridBrush : accentBrush);
    target->DrawText(title.c_str(), static_cast<UINT32>(title.size()),
                     textFormat, titleRect, titleBrush);

    const auto roleRect = D2D1::RectF(titleRect.left, titleRect.bottom + 2.0f,
                                      titleRect.right, titleRect.bottom + 16.0f);
    const std::wstring role = L"per-voice";
    ID2D1SolidColorBrush* roleBrush =
        highlighted && accentBrush ? accentBrush : (gridBrush ? gridBrush : textBrush);
    if (roleBrush) {
        target->DrawText(role.c_str(), static_cast<UINT32>(role.size()),
                         textFormat, roleRect, roleBrush);
    }

    const float innerTop = roleRect.bottom + 4.0f;
    const float innerBottom = rect.bottom - padding;
    const float innerLeft = rect.left + padding;
    const float innerRight = rect.right - padding;

    const float innerHeight = std::max(0.0f, innerBottom - innerTop);
    const float scopeHeight = innerHeight * 0.6f;
    const auto scopeRect = D2D1::RectF(innerLeft, innerTop, innerRight,
                                       innerTop + scopeHeight);

    // Scope label
    if (gridBrush) {
        const std::wstring scopeLabel = L"Transient Scope";
        const auto scopeLabelRect =
            D2D1::RectF(scopeRect.left + 2.0f, scopeRect.top + 2.0f,
                        scopeRect.right - 2.0f, scopeRect.top + 16.0f);
        const float originalOpacity = gridBrush->GetOpacity();
        gridBrush->SetOpacity(0.65f);
        target->DrawText(scopeLabel.c_str(),
                         static_cast<UINT32>(scopeLabel.size()), textFormat,
                         scopeLabelRect, gridBrush);
        gridBrush->SetOpacity(originalOpacity);
    }

    // 绘制激励瞬态/包络线
    const auto& samples = state_.excitationSamples;
    ID2D1SolidColorBrush* scopeBrush =
        excitationBrush ? excitationBrush
                        : (accentBrush ? accentBrush : gridBrush);
    if (scopeBrush && samples.size() >= 2) {
        const float width = scopeRect.right - scopeRect.left;
        const float height = scopeRect.bottom - scopeRect.top;
        if (width > 0.0f && height > 0.0f) {
            float peak = 0.0f;
            for (float s : samples) {
                peak = std::max(peak, std::abs(s));
            }
            const float invPeak = peak > 1e-4f ? (1.0f / peak) : 1.0f;
            const float midY = scopeRect.top + height * 0.5f;
            const float scaleY = height * 0.45f;
            float prevX = scopeRect.left;
            float prevY =
                midY - std::clamp(samples[0] * invPeak, -1.0f, 1.0f) * scaleY;
            const float step =
                width / static_cast<float>(samples.size() - 1);

            const float originalOpacity = scopeBrush->GetOpacity();
            scopeBrush->SetOpacity(highlighted ? 1.0f : 0.85f);
            const float thickness = highlighted ? 2.0f : 1.6f;
            for (std::size_t i = 1; i < samples.size(); ++i) {
                const float x =
                    scopeRect.left + step * static_cast<float>(i);
                const float y =
                    midY - std::clamp(samples[i] * invPeak, -1.0f, 1.0f) *
                               scaleY;
                target->DrawLine(D2D1::Point2F(prevX, prevY),
                                 D2D1::Point2F(x, y), scopeBrush, thickness);
                prevX = x;
                prevY = y;
            }
            scopeBrush->SetOpacity(originalOpacity);
        }
    }

    // 拨弦位置：一条弦和一个可移动的击弦点（放在 scope 下方）
    if (accentBrush) {
        const float stringAreaTop = scopeRect.bottom + 6.0f;
        const float stringAreaBottom = innerBottom;
        const float stringAreaHeight =
            std::max(0.0f, stringAreaBottom - stringAreaTop);
        const float stringY =
            stringAreaTop + stringAreaHeight * 0.5f;
        target->DrawLine(D2D1::Point2F(innerLeft, stringY),
                         D2D1::Point2F(innerRight, stringY), accentBrush,
                         highlighted ? 2.2f : 1.5f);
        const float pos = std::clamp(state_.pickPosition, 0.0f, 1.0f);
        const float x = innerLeft + (innerRight - innerLeft) * pos;
        target->DrawLine(D2D1::Point2F(x, stringY - 6.0f),
                         D2D1::Point2F(x, stringY + 6.0f), accentBrush,
                         highlighted ? 2.6f : 2.0f);
    }
}

void FlowDiagramNode::drawString(ID2D1HwndRenderTarget* target,
                                 ID2D1SolidColorBrush* textBrush,
                                 ID2D1SolidColorBrush* gridBrush,
                                 ID2D1SolidColorBrush* accentBrush,
                                 IDWriteTextFormat* textFormat,
                                 const D2D1_RECT_F& rect,
                                 bool highlighted) {
    if (!target || !textFormat) {
        return;
    }
    const float padding = 6.0f;
    const auto titleRect =
        D2D1::RectF(rect.left + padding, rect.top + padding,
                    rect.right - padding, rect.top + padding + 18.0f);
    const std::wstring title = L"STRING LOOP";
    ID2D1SolidColorBrush* titleBrush =
        textBrush ? textBrush : (gridBrush ? gridBrush : accentBrush);
    target->DrawText(title.c_str(), static_cast<UINT32>(title.size()),
                     textFormat, titleRect, titleBrush);

    const auto roleRect = D2D1::RectF(titleRect.left, titleRect.bottom + 2.0f,
                                      titleRect.right, titleRect.bottom + 16.0f);
    const std::wstring role = L"per-voice";
    ID2D1SolidColorBrush* roleBrush =
        highlighted && accentBrush ? accentBrush : (gridBrush ? gridBrush : textBrush);
    if (roleBrush) {
        target->DrawText(role.c_str(), static_cast<UINT32>(role.size()),
                         textFormat, roleRect, roleBrush);
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

    const float thickness = highlighted ? 2.6f : 2.0f;
    target->DrawLine(p0, p1, accentBrush, thickness);
    target->DrawLine(p1, p2, accentBrush, thickness);
    target->DrawLine(p2, p3, accentBrush, thickness);

    const float dispersion = std::clamp(state_.dispersionAmount, 0.0f, 1.0f);
    if (gridBrush && dispersion > 0.001f) {
        const int lines = 4;
        const float spacing = width / static_cast<float>(lines + 1);
        const float tilt = 6.0f + 18.0f * dispersion;
        for (int i = 0; i < lines; ++i) {
            const float x = innerLeft + spacing * (i + 1);
            D2D1_POINT_2F a{x, innerBottom};
            D2D1_POINT_2F b{x + tilt, innerTop + height * 0.35f};
            target->DrawLine(a, b, gridBrush, highlighted ? 1.6f : 1.0f);
        }
    }
}

void FlowDiagramNode::drawBody(ID2D1HwndRenderTarget* target,
                               ID2D1SolidColorBrush* textBrush,
                               ID2D1SolidColorBrush* gridBrush,
                               ID2D1SolidColorBrush* accentBrush,
                               IDWriteTextFormat* textFormat,
                               const D2D1_RECT_F& rect,
                               bool highlighted) {
    if (!target || !textFormat) {
        return;
    }
    const float padding = 6.0f;
    const auto titleRect =
        D2D1::RectF(rect.left + padding, rect.top + padding,
                    rect.right - padding, rect.top + padding + 18.0f);
    const std::wstring title = L"BODY (SHARED)";
    ID2D1SolidColorBrush* titleBrush =
        textBrush ? textBrush : (gridBrush ? gridBrush : accentBrush);
    target->DrawText(title.c_str(), static_cast<UINT32>(title.size()),
                     textFormat, titleRect, titleBrush);

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
        ID2D1SolidColorBrush* roleBrush =
            highlighted && accentBrush ? accentBrush : gridBrush;
        target->DrawText(role.c_str(), static_cast<UINT32>(role.size()),
                         textFormat, roleRect, roleBrush);
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

    const float thickness = highlighted ? 2.2f : 1.5f;
    target->DrawRectangle(lowShelf, accentBrush, thickness);
    target->DrawRectangle(midPeak, accentBrush, thickness);
    target->DrawRectangle(highShelf, accentBrush, thickness);
}

void FlowDiagramNode::drawRoom(ID2D1HwndRenderTarget* target,
                               ID2D1SolidColorBrush* panelBrush,
                               ID2D1SolidColorBrush* textBrush,
                               ID2D1SolidColorBrush* gridBrush,
                               ID2D1SolidColorBrush* accentBrush,
                               IDWriteTextFormat* textFormat,
                               const D2D1_RECT_F& rect,
                               bool highlighted) {
    if (!target || !textFormat) {
        return;
    }
    const float padding = 6.0f;
    const auto titleRect =
        D2D1::RectF(rect.left + padding, rect.top + padding,
                    rect.right - padding, rect.top + padding + 18.0f);
    const std::wstring title = L"ROOM / OUTPUT";
    ID2D1SolidColorBrush* titleBrush =
        textBrush ? textBrush : (gridBrush ? gridBrush : accentBrush);
    target->DrawText(title.c_str(), static_cast<UINT32>(title.size()),
                     textFormat, titleRect, titleBrush);

    const float innerTop = titleRect.bottom + 4.0f;
    const float innerBottom = rect.bottom - padding;
    const float innerLeft = rect.left + padding;
    const float innerRight = rect.right - padding;

    if (gridBrush) {
        const std::wstring role = L"shared";
        const auto roleRect =
            D2D1::RectF(innerLeft, innerTop - 14.0f, innerRight, innerTop + 2.0f);
        ID2D1SolidColorBrush* roleBrush =
            highlighted && accentBrush ? accentBrush : gridBrush;
        target->DrawText(role.c_str(), static_cast<UINT32>(role.size()),
                         textFormat, roleRect, roleBrush);
    }

    waveformView_.setBounds(
        D2D1::RectF(innerLeft, innerTop, innerRight, innerBottom));
    waveformView_.draw(target,
                       panelBrush ? panelBrush : gridBrush,
                       gridBrush ? gridBrush : accentBrush,
                       accentBrush ? accentBrush : gridBrush);

    if (accentBrush) {
        const float room = std::clamp(state_.roomAmount, 0.0f, 1.0f);
        const float barWidth = highlighted ? 8.0f : 6.0f;
        const float barHeight = (innerBottom - innerTop) * (0.2f + 0.6f * room);
        const float x = innerRight - barWidth - 2.0f;
        const float y = innerBottom - barHeight;
        D2D1_RECT_F roomBar = D2D1::RectF(x, y, x + barWidth, innerBottom);
        target->FillRectangle(roomBar, accentBrush);
    }
}

std::optional<FlowModule> FlowDiagramNode::hitTestModule(float x,
                                                         float y) const {
    if (ContainsPoint(excitationRect_, x, y)) {
        return FlowModule::kExcitation;
    }
    if (ContainsPoint(stringRect_, x, y)) {
        return FlowModule::kString;
    }
    if (ContainsPoint(bodyRect_, x, y)) {
        return FlowModule::kBody;
    }
    if (ContainsPoint(roomRect_, x, y)) {
        return FlowModule::kRoom;
    }
    return std::nullopt;
}

bool FlowDiagramNode::onPointerDown(float x, float y) {
    if (!onModuleSelected_) {
        return false;
    }
    if (auto module = hitTestModule(x, y)) {
        onModuleSelected_(*module);
        return true;
    }
    return false;
}

}  // namespace winui
