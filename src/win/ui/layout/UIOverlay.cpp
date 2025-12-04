#include "win/ui/layout/UIOverlay.h"

#include <algorithm>

namespace winui {

void UIOverlay::setChildren(std::vector<UILayoutNodePtr> children) {
    children_ = std::move(children);
}

float UIOverlay::preferredHeight(float width) const {
    float maxHeight = 0.0f;
    for (const auto& child : children_) {
        if (child) {
            maxHeight = std::max(maxHeight, child->preferredHeight(width));
        }
    }
    return maxHeight;
}

void UIOverlay::arrange(const D2D1_RECT_F& bounds) {
    UILayoutNode::arrange(bounds);
    for (auto& child : children_) {
        if (child) {
            child->arrange(bounds);
        }
    }
}

void UIOverlay::draw(const RenderResources& resources) {
    for (const auto& child : children_) {
        if (child) {
            child->draw(resources);
        }
    }
}

bool UIOverlay::onPointerDown(float x, float y) {
    for (auto& child : children_) {
        if (child && child->onPointerDown(x, y)) {
            return true;
        }
    }
    return false;
}

bool UIOverlay::onPointerMove(float x, float y) {
    bool handled = false;
    for (auto& child : children_) {
        if (child && child->onPointerMove(x, y)) {
            handled = true;
        }
    }
    return handled;
}

void UIOverlay::onPointerUp() {
    for (auto& child : children_) {
        if (child) {
            child->onPointerUp();
        }
    }
}

}  // namespace winui
