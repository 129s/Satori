#include "win/ui/layout/UIHorizontalStack.h"

#include <algorithm>
#include <cmath>

namespace winui {

UIHorizontalStack::UIHorizontalStack(float spacing) : spacing_(spacing) {}

void UIHorizontalStack::setItems(std::vector<Item> items) {
    items_ = std::move(items);
}

float UIHorizontalStack::preferredHeight(float width) const {
    if (items_.empty()) {
        return 0.0f;
    }
    const float totalSpacing =
        spacing_ * static_cast<float>(items_.size() > 0 ? items_.size() - 1 : 0);
    const float contentWidth = std::max(0.0f, width - totalSpacing);

    float fixed = totalFixedWidth(contentWidth);
    float percent = totalPercentWidth(contentWidth);
    const float remaining = std::max(0.0f, contentWidth - fixed - percent);

    std::size_t autoCount = 0;
    for (const auto& item : items_) {
        if (item.size.mode == UISizeMode::kAuto) {
            ++autoCount;
        }
    }
    const float autoWidth =
        autoCount > 0 ? remaining / static_cast<float>(autoCount) : 0.0f;

    float maxHeight = 0.0f;
    for (const auto& item : items_) {
        float childWidth = 0.0f;
        switch (item.size.mode) {
            case UISizeMode::kFixed:
                childWidth = item.size.value;
                break;
            case UISizeMode::kPercent:
                childWidth = contentWidth * item.size.value;
                break;
            case UISizeMode::kAuto:
            default:
                childWidth = autoWidth;
                break;
        }
        float pref = item.size.minHeight;
        if (item.node) {
            pref = std::max(pref, item.node->preferredHeight(childWidth));
        }
        maxHeight = std::max(maxHeight, pref);
    }
    return maxHeight;
}

void UIHorizontalStack::arrange(const D2D1_RECT_F& bounds) {
    UILayoutNode::arrange(bounds);
    if (items_.empty()) {
        return;
    }
    const float width = bounds.right - bounds.left;
    const float height = bounds.bottom - bounds.top;

    const float totalSpacing =
        spacing_ * static_cast<float>(items_.size() > 0 ? items_.size() - 1 : 0);
    const float contentWidth = std::max(0.0f, width - totalSpacing);

    float fixed = totalFixedWidth(contentWidth);
    float percent = totalPercentWidth(contentWidth);
    const float remaining = std::max(0.0f, contentWidth - fixed - percent);

    std::size_t autoCount = 0;
    for (const auto& item : items_) {
        if (item.size.mode == UISizeMode::kAuto) {
            ++autoCount;
        }
    }
    const float autoWidth =
        autoCount > 0 ? remaining / static_cast<float>(autoCount) : 0.0f;

    float x = bounds.left;
    for (const auto& item : items_) {
        float childWidth = 0.0f;
        switch (item.size.mode) {
            case UISizeMode::kFixed:
                childWidth = item.size.value;
                break;
            case UISizeMode::kPercent:
                childWidth = contentWidth * item.size.value;
                break;
            case UISizeMode::kAuto:
            default:
                childWidth = autoWidth;
                break;
        }
        childWidth = std::max(0.0f, std::min(childWidth, bounds.right - x));
        if (item.node) {
            const D2D1_RECT_F childBounds =
                D2D1::RectF(x, bounds.top, x + childWidth, bounds.top + height);
            item.node->arrange(childBounds);
        }
        x += childWidth + spacing_;
        if (x >= bounds.right) {
            break;
        }
    }
}

void UIHorizontalStack::draw(const RenderResources& resources) {
    for (const auto& item : items_) {
        if (item.node) {
            item.node->draw(resources);
        }
    }
}

bool UIHorizontalStack::onPointerDown(float x, float y) {
    for (auto& item : items_) {
        if (item.node && item.node->onPointerDown(x, y)) {
            return true;
        }
    }
    return false;
}

bool UIHorizontalStack::onPointerMove(float x, float y) {
    bool handled = false;
    for (auto& item : items_) {
        if (item.node && item.node->onPointerMove(x, y)) {
            handled = true;
        }
    }
    return handled;
}

void UIHorizontalStack::onPointerUp() {
    for (auto& item : items_) {
        if (item.node) {
            item.node->onPointerUp();
        }
    }
}

float UIHorizontalStack::totalFixedWidth(float) const {
    float total = 0.0f;
    for (const auto& item : items_) {
        if (item.size.mode == UISizeMode::kFixed) {
            total += item.size.value;
        }
    }
    return total;
}

float UIHorizontalStack::totalPercentWidth(float containerWidth) const {
    float total = 0.0f;
    for (const auto& item : items_) {
        if (item.size.mode == UISizeMode::kPercent) {
            total += containerWidth * item.size.value;
        }
    }
    return total;
}

}  // namespace winui
