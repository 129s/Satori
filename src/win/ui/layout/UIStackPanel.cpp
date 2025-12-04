#include "win/ui/layout/UIStackPanel.h"

#include <algorithm>
#include <cmath>

namespace winui {

UIStackPanel::UIStackPanel(float spacing) : spacing_(spacing) {}

void UIStackPanel::setItems(std::vector<Item> items) {
    items_ = std::move(items);
    cachedWidth_ = 0.0f;
    cachedPreferredHeight_ = 0.0f;
}

float UIStackPanel::preferredHeight(float width) const {
    if (std::abs(width - cachedWidth_) < 1e-3f && cachedPreferredHeight_ > 0.0f) {
        return cachedPreferredHeight_;
    }
    cachedWidth_ = width;
    float total = 0.0f;
    for (const auto& item : items_) {
        switch (item.size.mode) {
            case UISizeMode::kFixed:
                total += item.size.value;
                break;
            case UISizeMode::kPercent:
                // 在未知容器高度时无法准确估计，暂用最小高度
                total += item.size.minHeight;
                break;
            case UISizeMode::kAuto:
            default:
                if (item.node) {
                    total +=
                        std::max(item.node->preferredHeight(width), item.size.minHeight);
                }
                break;
        }
        total += spacing_;
    }
    if (!items_.empty()) {
        total -= spacing_;
    }
    cachedPreferredHeight_ = std::max(0.0f, total);
    return cachedPreferredHeight_;
}

float UIStackPanel::minimumHeight() const {
    float total = 0.0f;
    for (const auto& item : items_) {
        total += item.size.minHeight;
    }
    if (!items_.empty()) {
        total += spacing_ * (items_.size() - 1);
    }
    return total;
}

void UIStackPanel::arrange(const D2D1_RECT_F& bounds) {
    UILayoutNode::arrange(bounds);
    const float width = bounds.right - bounds.left;
    const float height = bounds.bottom - bounds.top;
    float y = bounds.top;

    float fixed = totalFixedHeight(height);
    float percent = totalPercentHeight(height);

    const float remaining = std::max(0.0f, height - fixed - percent);
    float availableForAuto = remaining;

    std::vector<float> autoHeights;
    autoHeights.reserve(items_.size());

    float autoDesired = 0.0f;
    for (const auto& item : items_) {
        if (item.size.mode == UISizeMode::kAuto && item.node) {
            const float desired =
                std::max(item.node->preferredHeight(width), item.size.minHeight);
            autoHeights.push_back(desired);
            autoDesired += desired;
        } else {
            autoHeights.push_back(0.0f);
        }
    }

    float scale = 1.0f;
    if (autoDesired > 0.0f && availableForAuto > 0.0f &&
        autoDesired > availableForAuto) {
        scale = availableForAuto / autoDesired;
    }

    for (std::size_t i = 0; i < items_.size(); ++i) {
        const auto& item = items_[i];
        float childHeight = 0.0f;
        switch (item.size.mode) {
            case UISizeMode::kFixed:
                childHeight = item.size.value;
                break;
            case UISizeMode::kPercent:
                childHeight = height * item.size.value;
                break;
            case UISizeMode::kAuto:
            default:
                childHeight = autoHeights[i] * scale;
                childHeight =
                    std::max(item.size.minHeight, std::min(childHeight, remaining));
                break;
        }
        childHeight = std::max(0.0f, std::min(childHeight, bounds.bottom - y));
        if (item.node) {
            const D2D1_RECT_F childBounds = D2D1::RectF(
                bounds.left, y, bounds.left + width, y + childHeight);
            item.node->arrange(childBounds);
        }
        y += childHeight + spacing_;
        if (y >= bounds.bottom) {
            break;
        }
    }
}

void UIStackPanel::draw(const RenderResources& resources) {
    for (const auto& item : items_) {
        if (item.node) {
            item.node->draw(resources);
        }
    }
}

bool UIStackPanel::onPointerDown(float x, float y) {
    for (auto& item : items_) {
        if (item.node && item.node->onPointerDown(x, y)) {
            return true;
        }
    }
    return false;
}

bool UIStackPanel::onPointerMove(float x, float y) {
    bool handled = false;
    for (auto& item : items_) {
        if (item.node && item.node->onPointerMove(x, y)) {
            handled = true;
        }
    }
    return handled;
}

void UIStackPanel::onPointerUp() {
    for (auto& item : items_) {
        if (item.node) {
            item.node->onPointerUp();
        }
    }
}

float UIStackPanel::totalFixedHeight(float) const {
    float total = 0.0f;
    for (const auto& item : items_) {
        if (item.size.mode == UISizeMode::kFixed) {
            total += item.size.value;
        }
    }
    if (!items_.empty()) {
        total += spacing_ * (items_.size() - 1);
    }
    return total;
}

float UIStackPanel::totalPercentHeight(float containerHeight) const {
    float total = 0.0f;
    for (const auto& item : items_) {
        if (item.size.mode == UISizeMode::kPercent) {
            total += containerHeight * item.size.value;
        }
    }
    return total;
}

}  // namespace winui
