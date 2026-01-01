#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "win/ui/UIModel.h"
#include "win/ui/WaveformView.h"
#include "win/ui/layout/UILayoutNode.h"

namespace winui {

class DropdownSelectorNode;

// A single module "card" preview: top half visualization, module title, and highlight state.
// This is used to build the unified 4-column layout (Excitation -> String -> Body -> Room).
class ModulePreviewNode : public UILayoutNode {
public:
    explicit ModulePreviewNode(FlowModule module);

    void setDiagramState(const FlowDiagramState& state);
    void setWaveformSamples(const std::vector<float>& samples) override;
    void setHighlighted(bool highlighted);
    void setOnSelected(std::function<void(FlowModule)> callback);

    // Optional: allow the Excitation preview to act as a Position slider.
    void setOnPickPositionChanged(std::function<void(float)> callback);
    void setPickPositionRange(float min, float max);
    bool isInteracting() const { return draggingPickPosition_; }

    std::shared_ptr<DropdownSelectorNode> waveformSelector() const {
        return waveformSelector_;
    }

    float preferredHeight(float) const override;
    void arrange(const D2D1_RECT_F& bounds) override;
    void draw(const RenderResources& resources) override;
    bool onPointerDown(float x, float y) override;
    bool onPointerMove(float x, float y) override;
    void onPointerUp() override;

private:
    D2D1_RECT_F computeVizRect() const;
    D2D1_RECT_F computePickTrackRect() const;
    float pickPositionFromX(float x) const;

    FlowModule module_ = FlowModule::kNone;
    FlowDiagramState state_{};
    bool highlighted_ = false;
    std::function<void(FlowModule)> onSelected_;
    std::function<void(float)> onPickPositionChanged_;
    float pickMin_ = 0.05f;
    float pickMax_ = 0.95f;
    bool draggingPickPosition_ = false;
    WaveformView waveformView_;

    std::shared_ptr<DropdownSelectorNode> waveformSelector_;
    D2D1_RECT_F excitationSelectorRect_{};
    D2D1_RECT_F excitationScopeRect_{};
};

}  // namespace winui
