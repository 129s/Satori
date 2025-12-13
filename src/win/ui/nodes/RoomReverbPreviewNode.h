#pragma once

#include <memory>
#include <vector>

#include <d2d1.h>

#include "win/ui/UIModel.h"
#include "win/ui/WaveformView.h"
#include "win/ui/layout/UILayoutNode.h"
#include "win/ui/nodes/DropdownSelectorNode.h"

namespace winui {

// Serum-like Room panel preview: dropdown (IR), waveform window (IR), knobs are below (outside this node).
class RoomReverbPreviewNode : public UILayoutNode {
public:
    RoomReverbPreviewNode();

    void setDiagramState(const FlowDiagramState& state);
    std::shared_ptr<DropdownSelectorNode> selector() const { return selector_; }

    float preferredHeight(float) const override { return 180.0f; }
    void arrange(const D2D1_RECT_F& bounds) override;
    void draw(const RenderResources& resources) override;

    bool onPointerDown(float x, float y) override;
    bool onPointerMove(float x, float y) override;
    void onPointerUp() override;

private:
    std::shared_ptr<DropdownSelectorNode> selector_;
    WaveformView irWaveform_;
    std::vector<float> irSamples_;

    D2D1_RECT_F selectorRect_{};
    D2D1_RECT_F waveformRect_{};
};

}  // namespace winui

