#pragma once

#include <vector>

#include "win/ui/UIModel.h"
#include "win/ui/layout/UILayoutNode.h"

namespace winui {

class ButtonBarNode : public UILayoutNode {
public:
    ButtonBarNode();

    void setButtons(std::vector<ButtonDescriptor> buttons);

    float preferredHeight(float width) const override;
    void arrange(const D2D1_RECT_F& bounds) override;
    void draw(const RenderResources& resources) override;

    bool onPointerDown(float x, float y) override;
    bool onPointerMove(float x, float y) override;
    void onPointerUp() override;

private:
    struct ButtonState {
        ButtonDescriptor desc;
        D2D1_RECT_F bounds{};
        bool pressed = false;
        bool hovered = false;
    };

    std::vector<ButtonState> buttons_;
    float buttonHeight_ = 36.0f;
    float buttonWidth_ = 120.0f;
    float spacing_ = 12.0f;
    ButtonState* active_ = nullptr;

    void layoutButtons(float width);
};

}  // namespace winui
