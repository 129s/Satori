#include "win/ui/nodes/WaveformNode.h"

#include <algorithm>

#include <d2d1helper.h>

namespace winui {

WaveformNode::WaveformNode() = default;

void WaveformNode::setSamples(const std::vector<float>& samples) {
    view_.setSamples(samples);
}

float WaveformNode::preferredHeight(float) const {
    return preferredHeight_;
}

void WaveformNode::draw(const RenderResources& resources) {
    if (!resources.target || !resources.panelBrush || !resources.gridBrush ||
        !resources.accentBrush) {
        return;
    }
    view_.setBounds(bounds_);
    view_.draw(resources.target, resources.panelBrush, resources.gridBrush,
               resources.accentBrush);
}

void WaveformNode::setBackgroundOpacity(float) {}

}  // namespace winui
