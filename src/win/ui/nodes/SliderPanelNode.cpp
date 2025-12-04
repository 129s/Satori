#include "win/ui/nodes/SliderPanelNode.h"

#include <algorithm>

#include <d2d1helper.h>

namespace winui {

SliderPanelNode::SliderPanelNode() = default;

void SliderPanelNode::setDescriptors(
    const std::vector<SliderDescriptor>& descriptors) {
    sliders_.clear();
    sliders_.reserve(descriptors.size());
    for (const auto& desc : descriptors) {
        SliderEntry entry;
        entry.descriptor = desc;
        const float initial = desc.getter ? desc.getter() : desc.min;
        entry.slider = std::make_shared<ParameterSlider>(
            desc.label, desc.min, desc.max, initial,
            [setter = desc.setter](float value) {
                if (setter) {
                    setter(value);
                }
            });
        sliders_.push_back(std::move(entry));
    }
}

float SliderPanelNode::preferredHeight(float) const {
    if (sliders_.empty()) {
        return 0.0f;
    }
    return sliders_.size() * sliderHeight_ +
           spacing_ * static_cast<float>(sliders_.size() - 1);
}

void SliderPanelNode::arrange(const D2D1_RECT_F& bounds) {
    UILayoutNode::arrange(bounds);
    float y = bounds.top;
    for (auto& entry : sliders_) {
        if (!entry.slider) {
            continue;
        }
        D2D1_RECT_F sliderBounds =
            D2D1::RectF(bounds.left, y, bounds.right, y + sliderHeight_);
        if (sliderBounds.bottom > bounds.bottom) {
            sliderBounds.bottom = bounds.bottom;
        }
        entry.slider->setBounds(sliderBounds);
        y += sliderHeight_ + spacing_;
        if (y >= bounds.bottom) {
            break;
        }
    }
}

void SliderPanelNode::draw(const RenderResources& resources) {
    if (!resources.target || !resources.trackBrush || !resources.fillBrush ||
        !resources.accentBrush || !resources.textBrush ||
        !resources.textFormat) {
        return;
    }
    for (const auto& entry : sliders_) {
        if (!entry.slider) {
            continue;
        }
        entry.slider->draw(resources.target, resources.trackBrush,
                           resources.fillBrush, resources.accentBrush,
                           resources.textBrush, resources.textFormat);
    }
}

bool SliderPanelNode::onPointerDown(float x, float y) {
    for (const auto& entry : sliders_) {
        if (entry.slider && entry.slider->onPointerDown(x, y)) {
            return true;
        }
    }
    return false;
}

bool SliderPanelNode::onPointerMove(float x, float y) {
    bool handled = false;
    for (const auto& entry : sliders_) {
        if (entry.slider && entry.slider->onPointerMove(x, y)) {
            handled = true;
        }
    }
    return handled;
}

void SliderPanelNode::onPointerUp() {
    for (const auto& entry : sliders_) {
        if (entry.slider) {
            entry.slider->onPointerUp();
        }
    }
}

void SliderPanelNode::syncSliders() {
    for (auto& entry : sliders_) {
        if (entry.slider && entry.descriptor.getter) {
            entry.slider->syncValue(entry.descriptor.getter());
        }
    }
}

}  // namespace winui
