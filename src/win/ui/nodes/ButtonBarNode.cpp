#include "win/ui/nodes/ButtonBarNode.h"

#include <algorithm>

#include <d2d1helper.h>

namespace winui {

ButtonBarNode::ButtonBarNode() = default;

void ButtonBarNode::setButtons(std::vector<ButtonDescriptor> buttons) {
    // Buttons are rebuilt from descriptors; discard any in-flight interaction
    // state to avoid keeping dangling pointers across UI refreshes.
    active_ = nullptr;
    buttons_.clear();
    buttons_.reserve(buttons.size());
    for (auto& desc : buttons) {
        buttons_.push_back(ButtonState{std::move(desc)});
    }
}

float ButtonBarNode::preferredHeight(float width) const {
    if (buttons_.empty()) {
        return 0.0f;
    }
    const float fullWidth = width - spacing_;
    const int perRow =
        std::max(1, static_cast<int>(fullWidth / (buttonWidth_ + spacing_)));
    const int rows =
        static_cast<int>((buttons_.size() + perRow - 1) / perRow);
    return rows * buttonHeight_ + (rows - 1) * spacing_;
}

void ButtonBarNode::arrange(const D2D1_RECT_F& bounds) {
    UILayoutNode::arrange(bounds);
    layoutButtons(bounds.right - bounds.left);
}

void ButtonBarNode::draw(const RenderResources& resources) {
    if (!resources.target || !resources.fillBrush || !resources.accentBrush ||
        !resources.textFormat || !resources.textBrush) {
        return;
    }
    for (const auto& button : buttons_) {
        ID2D1SolidColorBrush* brush =
            resources.trackBrush ? resources.trackBrush : resources.fillBrush;
        if (button.pressed) {
            // 按下：使用强调色填充
            brush = resources.accentBrush;
        } else if (button.hovered) {
            // hover：略提高亮度，仍保持与整体风格一致
            brush = resources.fillBrush;
        }
        resources.target->FillRectangle(button.bounds, brush);
        resources.target->DrawText(button.desc.label.c_str(),
                                   static_cast<UINT32>(button.desc.label.size()),
                                   resources.textFormat, button.bounds,
                                   resources.textBrush);
    }
}

bool ButtonBarNode::onPointerDown(float x, float y) {
    for (auto& button : buttons_) {
        if (x >= button.bounds.left && x <= button.bounds.right &&
            y >= button.bounds.top && y <= button.bounds.bottom) {
            button.pressed = true;
            button.hovered = true;
            active_ = &button;
            return true;
        }
    }
    return false;
}

bool ButtonBarNode::onPointerMove(float x, float y) {
    bool handled = false;

    if (active_) {
        const bool insideActive =
            x >= active_->bounds.left && x <= active_->bounds.right &&
            y >= active_->bounds.top && y <= active_->bounds.bottom;
        active_->pressed = insideActive;
        active_->hovered = insideActive;
        handled = true;
    }

    for (auto& button : buttons_) {
        const bool inside = x >= button.bounds.left &&
                            x <= button.bounds.right &&
                            y >= button.bounds.top &&
                            y <= button.bounds.bottom;
        bool changed = false;
        if (&button != active_) {
            if (button.hovered != inside) {
                button.hovered = inside;
                changed = true;
            }
        }
        handled = handled || changed;
    }

    return handled;
}

void ButtonBarNode::onPointerUp() {
    if (active_) {
        const bool execute = active_->pressed;
        active_->pressed = false;
        auto callback = active_->desc.onClick;
        active_ = nullptr;
        if (execute && callback) {
            callback();
        }
    }
}

void ButtonBarNode::layoutButtons(float width) {
    if (buttons_.empty()) {
        return;
    }
    const int perRow = std::max(
        1, static_cast<int>((width + spacing_) / (buttonWidth_ + spacing_)));
    float x = bounds_.left;
    float y = bounds_.top;
    int column = 0;
    for (auto& button : buttons_) {
        button.bounds = D2D1::RectF(x, y, x + buttonWidth_, y + buttonHeight_);
        ++column;
        if (column >= perRow) {
            column = 0;
            x = bounds_.left;
            y += buttonHeight_ + spacing_;
        } else {
            x += buttonWidth_ + spacing_;
        }
    }
}

}  // namespace winui
