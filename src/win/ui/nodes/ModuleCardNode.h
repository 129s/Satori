#pragma once

#include <memory>

#include <d2d1.h>

#include "win/ui/RenderResources.h"
#include "win/ui/UIModel.h"
#include "win/ui/layout/UILayoutNode.h"

namespace winui {

// A module "card" container that provides depth (rounded background + subtle shadow)
// and hosts a preview visualizer (top) plus controls (bottom).
class ModuleCardNode : public UILayoutNode {
public:
    ModuleCardNode(FlowModule module,
                   std::shared_ptr<UILayoutNode> preview,
                   std::shared_ptr<UILayoutNode> controls);

    void setHighlighted(bool highlighted);

    float preferredHeight(float width) const override;
    void arrange(const D2D1_RECT_F& bounds) override;
    void draw(const RenderResources& resources) override;

    bool onPointerDown(float x, float y) override;
    bool onPointerMove(float x, float y) override;
    void onPointerUp() override;

private:
    FlowModule module_ = FlowModule::kNone;
    std::shared_ptr<UILayoutNode> preview_;
    std::shared_ptr<UILayoutNode> controls_;
    bool highlighted_ = false;

    float padding_ = 15.0f;
    float spacing_ = 10.0f;
    float cornerRadius_ = 6.0f;

    D2D1_RECT_F inner_{};
};

}  // namespace winui

