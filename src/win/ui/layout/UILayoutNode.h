#pragma once

#include <memory>
#include <vector>

#include <d2d1.h>

#include "win/ui/RenderResources.h"

namespace winui {

enum class UISizeMode { kAuto, kFixed, kPercent };

struct UISizeSpec {
    UISizeMode mode = UISizeMode::kAuto;
    float value = 0.0f;   // pixels for Fixed，比例 [0,1] 用于 Percent
    float minHeight = 0.0f;
};

class UILayoutNode {
public:
    virtual ~UILayoutNode() = default;

    virtual float preferredHeight(float width) const = 0;
    virtual float minimumHeight() const { return 0.0f; }

    virtual void arrange(const D2D1_RECT_F& bounds) { bounds_ = bounds; }
    virtual void draw(const RenderResources& resources) = 0;

    virtual bool onPointerDown(float x, float y) { return false; }
    virtual bool onPointerMove(float x, float y) { return false; }
    virtual void onPointerUp() {}

    virtual void setWaveformSamples(const std::vector<float>&) {}
    virtual void syncSliders() {}

    const D2D1_RECT_F& bounds() const { return bounds_; }

protected:
    D2D1_RECT_F bounds_{};
};

using UILayoutNodePtr = std::shared_ptr<UILayoutNode>;

}  // namespace winui
