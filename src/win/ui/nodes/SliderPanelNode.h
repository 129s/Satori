#pragma once

#include <memory>
#include <vector>

#include "win/ui/ParameterSlider.h"
#include "win/ui/UIModel.h"
#include "win/ui/layout/UILayoutNode.h"

namespace winui {

class SliderPanelNode : public UILayoutNode {
public:
    SliderPanelNode();

    void setDescriptors(const std::vector<SliderDescriptor>& descriptors);

    float preferredHeight(float width) const override;
    void arrange(const D2D1_RECT_F& bounds) override;
    void draw(const RenderResources& resources) override;

    bool onPointerDown(float x, float y) override;
    bool onPointerMove(float x, float y) override;
    void onPointerUp() override;

    void syncSliders() override;

private:
    struct SliderEntry {
        SliderDescriptor descriptor;
        std::shared_ptr<ParameterSlider> slider;
    };

    std::vector<SliderEntry> sliders_;
    float sliderHeight_ = 80.0f;
    float spacing_ = 16.0f;
};

}  // namespace winui
