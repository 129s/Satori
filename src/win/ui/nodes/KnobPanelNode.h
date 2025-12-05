#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "win/ui/DebugOverlay.h"
#include "win/ui/ParameterKnob.h"
#include "win/ui/UIModel.h"
#include "win/ui/layout/UILayoutNode.h"

namespace winui {

// 参数旋钮面板：按模块分组展示若干旋钮。
class KnobPanelNode : public UILayoutNode {
public:
    KnobPanelNode();

    void setDescriptors(const std::vector<SliderDescriptor>& descriptors);
    void syncKnobs();

    float preferredHeight(float) const override;
    void arrange(const D2D1_RECT_F& bounds) override;
    void draw(const RenderResources& resources) override;

    bool onPointerDown(float x, float y) override;
    bool onPointerMove(float x, float y) override;
    void onPointerUp() override;
    std::optional<DebugBoxModel> debugBoxForPoint(float x, float y) const;
    std::shared_ptr<ParameterKnob> activeKnob() const;

private:
    struct KnobEntry {
        SliderDescriptor descriptor;
        std::shared_ptr<ParameterKnob> knob;
    };

    struct Group {
        std::wstring title;
        std::vector<KnobEntry> knobs;
        D2D1_RECT_F bounds{};
    };

    std::vector<Group> groups_;
    float minHeight_ = 200.0f;
    float groupSpacing_ = 16.0f;
    float padding_ = 8.0f;

    void rebuildGroups(const std::vector<SliderDescriptor>& descriptors);
};

}  // namespace winui
