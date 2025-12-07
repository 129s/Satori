#pragma once

#include <vector>

#include "win/ui/layout/UILayoutNode.h"

namespace winui {

class UIHorizontalStack : public UILayoutNode {
public:
    struct Item {
        UILayoutNodePtr node;
        UISizeSpec size;
    };

    explicit UIHorizontalStack(float spacing = 12.0f);

    void setItems(std::vector<Item> items);

    float preferredHeight(float width) const override;
    void arrange(const D2D1_RECT_F& bounds) override;
    void draw(const RenderResources& resources) override;

    bool onPointerDown(float x, float y) override;
    bool onPointerMove(float x, float y) override;
    void onPointerUp() override;

private:
    std::vector<Item> items_;
    float spacing_ = 12.0f;

    float totalFixedWidth(float containerWidth) const;
    float totalPercentWidth(float containerWidth) const;
};

}  // namespace winui
