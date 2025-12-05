#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "win/ui/UIModel.h"
#include "win/ui/VirtualKeyboard.h"
#include "win/ui/layout/UILayoutNode.h"

namespace winui {

class KeyboardNode : public UILayoutNode {
public:
    KeyboardNode();

    void setConfig(const KeyboardConfig& config,
                   std::function<void(double)> callback);
    void setColors(const KeyboardColors& colors);
    bool pressKeyByMidi(int midiNote);
    void releaseKeyByMidi(int midiNote);
    void releaseAllKeys();

    float preferredHeight(float) const override;
    void arrange(const D2D1_RECT_F& bounds) override;
    void draw(const RenderResources& resources) override;

    bool onPointerDown(float x, float y) override;
    bool onPointerMove(float x, float y) override;
    void onPointerUp() override;

private:
    VirtualKeyboard keyboard_;
    float preferredHeight_ = 120.0f;
    KeyboardColors colors_{};
};

}  // namespace winui
