#include "win/ui/nodes/RoomReverbPreviewNode.h"

#include <algorithm>

#include <d2d1helper.h>

namespace winui {

namespace {
bool ContainsPoint(const D2D1_RECT_F& rect, float x, float y) {
    return x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom;
}
}  // namespace

RoomReverbPreviewNode::RoomReverbPreviewNode()
    : selector_(std::make_shared<DropdownSelectorNode>()) {
    selector_->setPageSize(6);
}

void RoomReverbPreviewNode::setDiagramState(const FlowDiagramState& state) {
    irSamples_ = state.roomIrPreviewSamples;
    irWaveform_.setSamples(irSamples_);
}

void RoomReverbPreviewNode::arrange(const D2D1_RECT_F& bounds) {
    UILayoutNode::arrange(bounds);

    const float padding = 6.0f;
    const float titleH = 18.0f;
    const float dropdownH = 28.0f;

    const auto inner = D2D1::RectF(bounds.left + padding, bounds.top + padding,
                                   bounds.right - padding, bounds.bottom - padding);
    const float innerW = inner.right - inner.left;
    const float innerH = inner.bottom - inner.top;
    if (innerW <= 0.0f || innerH <= 0.0f) {
        selectorRect_ = waveformRect_ = D2D1::RectF(0, 0, 0, 0);
        return;
    }

    selectorRect_ = D2D1::RectF(inner.left, inner.top + titleH,
                                inner.right, inner.top + titleH + dropdownH);
    waveformRect_ = D2D1::RectF(inner.left, selectorRect_.bottom + 6.0f,
                                inner.right, inner.bottom);

    if (selector_) {
        selector_->arrange(selectorRect_);
    }
}

void RoomReverbPreviewNode::draw(const RenderResources& resources) {
    if (!resources.target || !resources.textFormat) {
        return;
    }

    auto* bg = resources.panelBrush ? resources.panelBrush : resources.trackBrush;
    auto* grid = resources.gridBrush ? resources.gridBrush : resources.trackBrush;
    auto* text = resources.textBrush ? resources.textBrush : resources.gridBrush;
    auto* accent = resources.accentBrush ? resources.accentBrush : text;
    if (!bg || !grid || !text || !accent) {
        return;
    }

    resources.target->FillRectangle(bounds_, bg);

    // Small module label.
    const float padding = 6.0f;
    const auto titleRect =
        D2D1::RectF(bounds_.left + padding, bounds_.top + padding,
                    bounds_.right - padding, bounds_.top + padding + 18.0f);
    resources.target->DrawText(L"ROOM", 4, resources.textFormat, titleRect, text);

    if (selector_) {
        selector_->draw(resources);
    }

    // IR waveform window.
    if (ContainsPoint(bounds_, waveformRect_.left, waveformRect_.top)) {
        irWaveform_.setBounds(waveformRect_);
        irWaveform_.draw(resources.target, bg, grid, accent);
    }
}

bool RoomReverbPreviewNode::onPointerDown(float x, float y) {
    if (selector_ && selector_->onPointerDown(x, y)) {
        return true;
    }
    return ContainsPoint(bounds_, x, y);
}

bool RoomReverbPreviewNode::onPointerMove(float x, float y) {
    if (selector_ && selector_->onPointerMove(x, y)) {
        return true;
    }
    return ContainsPoint(bounds_, x, y);
}

void RoomReverbPreviewNode::onPointerUp() {
    if (selector_) {
        selector_->onPointerUp();
    }
}

}  // namespace winui

