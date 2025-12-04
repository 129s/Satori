#include "win/ui/nodes/KnobPanelNode.h"

#include <algorithm>

#include <d2d1helper.h>

#include "win/ui/DebugOverlay.h"

namespace winui {

namespace {
bool ContainsPoint(const D2D1_RECT_F& rect, float x, float y) {
    return x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom;
}

bool IsRectValid(const D2D1_RECT_F& rect) {
    return rect.right > rect.left && rect.bottom > rect.top;
}

DebugBoxModel MakeGroupBox(const D2D1_RECT_F& bounds,
                           float padding,
                           float titleHeight) {
    DebugBoxModel model{};
    if (!IsRectValid(bounds)) {
        return model;
    }
    model.segments.push_back({DebugBoxLayer::kBorder, bounds});

    const auto inner = D2D1::RectF(bounds.left + padding, bounds.top + padding,
                                   bounds.right - padding,
                                   bounds.bottom - padding);
    if (!IsRectValid(inner)) {
        return model;
    }
    model.segments.push_back({DebugBoxLayer::kPadding, inner});

    const float knobAreaTop = inner.top + titleHeight + 6.0f;
    const auto knobRect =
        D2D1::RectF(inner.left, knobAreaTop, inner.right, inner.bottom);
    if (!IsRectValid(knobRect)) {
        return model;
    }
    const auto content =
        D2D1::RectF(knobRect.left + padding, knobRect.top + padding,
                    knobRect.right - padding, knobRect.bottom - padding);
    if (IsRectValid(content)) {
        model.segments.push_back({DebugBoxLayer::kContent, content});
    }
    return model;
}
}  // namespace

KnobPanelNode::KnobPanelNode() = default;

void KnobPanelNode::setDescriptors(
    const std::vector<SliderDescriptor>& descriptors) {
    rebuildGroups(descriptors);
}

void KnobPanelNode::syncKnobs() {
    for (auto& group : groups_) {
        for (auto& entry : group.knobs) {
            if (entry.knob && entry.descriptor.getter) {
                entry.knob->syncValue(entry.descriptor.getter());
            }
        }
    }
}

float KnobPanelNode::preferredHeight(float) const {
    return minHeight_;
}

void KnobPanelNode::arrange(const D2D1_RECT_F& bounds) {
    UILayoutNode::arrange(bounds);
    if (groups_.empty()) {
        return;
    }

    const float width = bounds.right - bounds.left;
    const float groupCount = static_cast<float>(groups_.size());
    const float totalSpacing = groupSpacing_ * (groupCount - 1.0f);
    const float groupWidth = std::max(
        0.0f, (width - totalSpacing) / std::max(1.0f, groupCount));

    float x = bounds.left;
    for (auto& group : groups_) {
        group.bounds = D2D1::RectF(x, bounds.top, x + groupWidth, bounds.bottom);
        x += groupWidth + groupSpacing_;

        const float innerLeft = group.bounds.left + padding_;
        const float innerRight = group.bounds.right - padding_;
        const float innerTop = group.bounds.top + padding_;
        const float innerBottom = group.bounds.bottom - padding_;
        const float innerWidth = innerRight - innerLeft;
        const float innerHeight = innerBottom - innerTop;

        // 标题占一行，其余区域用于放置旋钮
        const float titleHeight = 18.0f;
        const float knobAreaTop = innerTop + titleHeight + 6.0f;
        const float knobAreaHeight = std::max(0.0f, innerHeight - titleHeight - 6.0f);

        const std::size_t knobCount = group.knobs.size();
        if (knobCount == 0) {
            continue;
        }

        const std::size_t columns = std::min<std::size_t>(knobCount, 3);
        const std::size_t rows =
            static_cast<std::size_t>((knobCount + columns - 1) / columns);

        const float cellWidth =
            (innerWidth - static_cast<float>(columns - 1) * padding_) /
            static_cast<float>(columns);
        const float cellHeight =
            (knobAreaHeight - static_cast<float>(rows - 1) * padding_) /
            static_cast<float>(rows);

        for (std::size_t index = 0; index < knobCount; ++index) {
            auto& entry = group.knobs[index];
            if (!entry.knob) {
                continue;
            }
            const std::size_t row = index / columns;
            const std::size_t col = index % columns;

            const float cellLeft =
                innerLeft + static_cast<float>(col) * (cellWidth + padding_);
            const float cellTop =
                knobAreaTop + static_cast<float>(row) * (cellHeight + padding_);
            const D2D1_RECT_F knobBounds = D2D1::RectF(
                cellLeft, cellTop, cellLeft + cellWidth, cellTop + cellHeight);
            entry.knob->setBounds(knobBounds);
        }
    }
}

void KnobPanelNode::draw(const RenderResources& resources) {
    if (!resources.target || !resources.textBrush || !resources.textFormat) {
        return;
    }

    auto* panelBrush =
        resources.panelBrush ? resources.panelBrush : resources.trackBrush;
    auto* borderBrush =
        resources.gridBrush ? resources.gridBrush : resources.accentBrush;

    for (const auto& group : groups_) {
        if (panelBrush) {
            resources.target->FillRectangle(group.bounds, panelBrush);
        }
        if (borderBrush) {
            resources.target->DrawRectangle(group.bounds, borderBrush, 1.0f);
        }

        // 标题
        if (!group.title.empty()) {
            const auto titleRect =
                D2D1::RectF(group.bounds.left + padding_,
                            group.bounds.top + padding_,
                            group.bounds.right - padding_,
                            group.bounds.top + padding_ + 18.0f);
            resources.target->DrawText(
                group.title.c_str(), static_cast<UINT32>(group.title.size()),
                resources.textFormat, titleRect, resources.textBrush);
        }

        for (const auto& entry : group.knobs) {
            if (!entry.knob) {
                continue;
            }
            entry.knob->draw(resources.target, resources.trackBrush,
                             resources.fillBrush, resources.accentBrush,
                             resources.textBrush, resources.textFormat);
        }

    }
}

bool KnobPanelNode::onPointerDown(float x, float y) {
    for (auto& group : groups_) {
        for (auto& entry : group.knobs) {
            if (entry.knob && entry.knob->onPointerDown(x, y)) {
                return true;
            }
        }
    }
    return false;
}

bool KnobPanelNode::onPointerMove(float x, float y) {
    bool handled = false;
    for (auto& group : groups_) {
        for (auto& entry : group.knobs) {
            if (entry.knob && entry.knob->onPointerMove(x, y)) {
                handled = true;
            }
        }
    }
    return handled;
}

void KnobPanelNode::onPointerUp() {
    for (auto& group : groups_) {
        for (auto& entry : group.knobs) {
            if (entry.knob) {
                entry.knob->onPointerUp();
            }
        }
    }
}

std::optional<DebugBoxModel> KnobPanelNode::debugBoxForPoint(float x,
                                                             float y) const {
    if (auto knob = activeKnob()) {
        return knob->debugBoxModel();
    }

    for (const auto& group : groups_) {
        for (const auto& entry : group.knobs) {
            if (entry.knob && entry.knob->contains(x, y)) {
                return entry.knob->debugBoxModel();
            }
        }
    }

    for (const auto& group : groups_) {
        if (ContainsPoint(group.bounds, x, y)) {
            return MakeGroupBox(group.bounds, padding_, 18.0f);
        }
    }

    if (ContainsPoint(bounds_, x, y)) {
        return MakeGroupBox(bounds_, padding_, 20.0f);
    }
    return std::nullopt;
}

void KnobPanelNode::rebuildGroups(
    const std::vector<SliderDescriptor>& descriptors) {
    groups_.clear();
    if (descriptors.empty()) {
        return;
    }

    Group excitation;
    excitation.title = L"EXCITATION";
    Group stringGroup;
    stringGroup.title = L"STRING";
    Group filterGroup;
    filterGroup.title = L"FILTER / OUTPUT";
    Group other;
    other.title = L"OTHER";

    auto makeEntry = [](const SliderDescriptor& desc) {
        KnobEntry entry;
        entry.descriptor = desc;
        const float initial = desc.getter ? desc.getter() : desc.min;
        entry.knob = std::make_shared<ParameterKnob>(
            desc.label, desc.min, desc.max, initial,
            [setter = desc.setter](float value) {
                if (setter) {
                    setter(value);
                }
            });
        return entry;
    };

    for (const auto& desc : descriptors) {
        const auto& label = desc.label;
        if (label == L"Pick Position") {
            excitation.knobs.push_back(makeEntry(desc));
        } else if (label == L"Decay") {
            stringGroup.knobs.push_back(makeEntry(desc));
        } else if (label == L"Brightness") {
            filterGroup.knobs.push_back(makeEntry(desc));
        } else {
            other.knobs.push_back(makeEntry(desc));
        }
    }

    if (!excitation.knobs.empty()) {
        groups_.push_back(std::move(excitation));
    }
    if (!stringGroup.knobs.empty()) {
        groups_.push_back(std::move(stringGroup));
    }
    if (!filterGroup.knobs.empty()) {
        groups_.push_back(std::move(filterGroup));
    }
    if (!other.knobs.empty()) {
        groups_.push_back(std::move(other));
    }
}

std::shared_ptr<ParameterKnob> KnobPanelNode::activeKnob() const {
    for (const auto& group : groups_) {
        for (const auto& entry : group.knobs) {
            if (entry.knob && entry.knob->isDragging()) {
                return entry.knob;
            }
        }
    }
    return nullptr;
}

}  // namespace winui
