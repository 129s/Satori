#pragma once

#include <vector>

#include "win/ui/layout/UILayoutNode.h"

namespace winui {

class UIStackPanel : public UILayoutNode {
public:
    struct Item {
        UILayoutNodePtr node;
        UISizeSpec size;
    };

    explicit UIStackPanel(float spacing = 12.0f);

    void setItems(std::vector<Item> items);

    float preferredHeight(float width) const override;
    float minimumHeight() const override;

    void arrange(const D2D1_RECT_F& bounds) override;
    void draw(const RenderResources& resources) override;

    bool onPointerDown(float x, float y) override;
    bool onPointerMove(float x, float y) override;
    void onPointerUp() override;

private:
    std::vector<Item> items_;
    float spacing_;
    mutable float cachedWidth_ = 0.0f;
    mutable float cachedPreferredHeight_ = 0.0f;

    float totalFixedHeight(float containerHeight) const;
    float totalPercentHeight(float containerHeight) const;
};

}  // namespace winui
