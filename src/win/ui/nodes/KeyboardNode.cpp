#include "win/ui/nodes/KeyboardNode.h"

#include <d2d1helper.h>

namespace winui {

KeyboardNode::KeyboardNode() = default;

void KeyboardNode::setConfig(const KeyboardConfig& config,
                             std::function<void(int, double, bool)> callback) {
    keyboard_.setCallback(std::move(callback));
    if (!hasConfig_ || config.showLabels != config_.showLabels) {
        keyboard_.setShowLabels(config.showLabels);
    }
    if (!hasConfig_ || config.hoverOutline != config_.hoverOutline) {
        keyboard_.setHoverOutline(config.hoverOutline);
    }
    if (!hasConfig_ || config.baseMidiNote != config_.baseMidiNote ||
        config.octaveCount != config_.octaveCount) {
        keyboard_.setPianoLayout(config.baseMidiNote, config.octaveCount);
    }
    config_ = config;
    hasConfig_ = true;
    preferredHeight_ = 168.0f;
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
    if (!resources.target || !resources.textFormat) {
        return;
    }

    KeyboardColors colors = colors_;
    // 若未提供专用配色，退回默认刷子组合。
    if (!colors.whiteFill) {
        colors.whiteFill = resources.panelBrush;
    }
    if (!colors.whiteBorder) {
        colors.whiteBorder = resources.accentBrush;
    }
    if (!colors.whitePressed) {
        colors.whitePressed = resources.fillBrush;
    }
    if (!colors.whiteText) {
        colors.whiteText = resources.textBrush ? resources.textBrush : resources.accentBrush;
    }
    if (!colors.blackFill) {
        colors.blackFill = resources.trackBrush ? resources.trackBrush : resources.panelBrush;
    }
    if (!colors.blackBorder) {
        colors.blackBorder = colors.whiteBorder;
    }
    if (!colors.blackPressed) {
        colors.blackPressed = colors.whitePressed;
    }
    if (!colors.blackText) {
        colors.blackText = colors.whiteText;
    }
    if (!colors.hoverOutline) {
        colors.hoverOutline = colors.whiteBorder;
    }

    keyboard_.draw(resources.target, colors, resources.textFormat);
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

void KeyboardNode::setColors(const KeyboardColors& colors) {
    colors_ = colors;
}

bool KeyboardNode::pressKeyByMidi(int midiNote) {
    return keyboard_.pressKeyByMidi(midiNote);
}

void KeyboardNode::releaseKeyByMidi(int midiNote) {
    keyboard_.releaseKeyByMidi(midiNote);
}

void KeyboardNode::releaseAllKeys() {
    keyboard_.releaseAllKeys();
}

}  // namespace winui
