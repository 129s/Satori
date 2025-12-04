#include "win/ui/nodes/KeyboardNode.h"

#include <d2d1helper.h>

namespace winui {

KeyboardNode::KeyboardNode() = default;

void KeyboardNode::setKeys(const std::vector<VirtualKeyDescriptor>& keys,
                           std::function<void(double)> callback) {
    std::vector<std::pair<std::wstring, double>> vk;
    vk.reserve(keys.size());
    for (const auto& key : keys) {
        vk.emplace_back(key.label, key.frequency);
    }
    keyboard_.setKeys(vk);
    keyboard_.setCallback(std::move(callback));
}

float KeyboardNode::preferredHeight(float) const {
    return preferredHeight_;
}

void KeyboardNode::arrange(const D2D1_RECT_F& bounds) {
    UILayoutNode::arrange(bounds);
    keyboard_.setBounds(bounds);
    keyboard_.layoutKeys();
}

void KeyboardNode::draw(const RenderResources& resources) {
    if (!resources.target || !resources.accentBrush || !resources.fillBrush ||
        !resources.panelBrush || !resources.textFormat) {
        return;
    }

    // 默认主题下沿用原有配色：
    // - 边框：accentBrush
    // - 普通键：panelBrush
    // - 按下键：fillBrush
    ID2D1SolidColorBrush* borderBrush = resources.accentBrush;
    ID2D1SolidColorBrush* fillBrush = resources.panelBrush;
    ID2D1SolidColorBrush* activeBrush = resources.fillBrush;

    keyboard_.draw(resources.target, borderBrush, fillBrush, activeBrush,
                   resources.textFormat);
}

bool KeyboardNode::onPointerDown(float x, float y) {
    return keyboard_.onPointerDown(x, y);
}

bool KeyboardNode::onPointerMove(float x, float y) {
    return keyboard_.onPointerMove(x, y);
}

void KeyboardNode::onPointerUp() {
    keyboard_.onPointerUp();
}

}  // namespace winui
