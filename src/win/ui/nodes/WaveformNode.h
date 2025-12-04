#pragma once

#include <vector>

#include "win/ui/WaveformView.h"
#include "win/ui/layout/UILayoutNode.h"

namespace winui {

class WaveformNode : public UILayoutNode {
public:
    WaveformNode();

    void setSamples(const std::vector<float>& samples);
    float preferredHeight(float width) const override;
    void draw(const RenderResources& resources) override;

    void setBackgroundOpacity(float opacity);

private:
    WaveformView view_;
    float preferredHeight_ = 200.0f;
};

}  // namespace winui
