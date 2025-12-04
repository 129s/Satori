#pragma once

#include <vector>

#include "win/ui/layout/UILayoutNode.h"

namespace winui {

class UIOverlay : public UILayoutNode {
public:
    void setChildren(std::vector<UILayoutNodePtr> children);

    float preferredHeight(float width) const override;

    void arrange(const D2D1_RECT_F& bounds) override;
    void draw(const RenderResources& resources) override;

    bool onPointerDown(float x, float y) override;
    bool onPointerMove(float x, float y) override;
    void onPointerUp() override;

private:
    std::vector<UILayoutNodePtr> children_;
};

}  // namespace winui
