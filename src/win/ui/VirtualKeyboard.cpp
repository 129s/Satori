#include "win/ui/VirtualKeyboard.h"

#include <algorithm>

#include <d2d1helper.h>

namespace winui {

void VirtualKeyboard::setBounds(const D2D1_RECT_F& bounds) {
    bounds_ = bounds;
    layoutKeys();
}

void VirtualKeyboard::setCallback(Callback callback) {
    callback_ = std::move(callback);
}

void VirtualKeyboard::setKeys(const std::vector<std::pair<std::wstring, double>>& keys) {
    keys_.clear();
    keys_.reserve(keys.size());
    for (const auto& [label, freq] : keys) {
        Key key;
        key.label = label;
        key.frequency = freq;
        keys_.push_back(std::move(key));
    }
    layoutKeys();
}

void VirtualKeyboard::draw(ID2D1HwndRenderTarget* target,
                           ID2D1SolidColorBrush* borderBrush,
                           ID2D1SolidColorBrush* fillBrush,
                           ID2D1SolidColorBrush* activeBrush,
                           IDWriteTextFormat* textFormat) const {
    if (!target || !borderBrush || !fillBrush || !activeBrush || !textFormat) {
        return;
    }
    for (const auto& key : keys_) {
        auto* brush = key.pressed ? activeBrush : fillBrush;
        target->FillRectangle(key.bounds, brush);
        target->DrawRectangle(key.bounds, borderBrush, 1.0f);
        target->DrawText(key.label.c_str(), static_cast<UINT32>(key.label.size()),
                         textFormat, key.bounds, borderBrush);
    }
}

bool VirtualKeyboard::onPointerDown(float x, float y) {
    Key* key = hitTest(x, y);
    if (!key) {
        return false;
    }
    dragging_ = true;
    activeKey_ = key;
    triggerKey(key);
    return true;
}

bool VirtualKeyboard::onPointerMove(float x, float y) {
    if (!dragging_) {
        return false;
    }
    Key* key = hitTest(x, y);
    if (key && key != activeKey_) {
        if (activeKey_) {
            activeKey_->pressed = false;
        }
        activeKey_ = key;
        triggerKey(key);
    }
    return key != nullptr;
}

void VirtualKeyboard::onPointerUp() {
    dragging_ = false;
    if (activeKey_) {
        activeKey_->pressed = false;
        activeKey_ = nullptr;
    }
}

VirtualKeyboard::Key* VirtualKeyboard::hitTest(float x, float y) {
    for (auto& key : keys_) {
        if (x >= key.bounds.left && x <= key.bounds.right && y >= key.bounds.top &&
            y <= key.bounds.bottom) {
            return &key;
        }
    }
    return nullptr;
}

void VirtualKeyboard::triggerKey(Key* key) {
    if (!key) {
        return;
    }
    key->pressed = true;
    if (callback_) {
        callback_(key->frequency);
    }
}

void VirtualKeyboard::layoutKeys() {
    if (keys_.empty()) {
        return;
    }
    const float width = bounds_.right - bounds_.left;
    if (width <= 0.0f) {
        return;
    }
    const float keyWidth = width / static_cast<float>(keys_.size());
    float x = bounds_.left;
    for (auto& key : keys_) {
        key.bounds = D2D1::RectF(x + 4.0f, bounds_.top + 4.0f, x + keyWidth - 4.0f,
                                 bounds_.bottom - 4.0f);
        x += keyWidth;
    }
}

}  // namespace winui
