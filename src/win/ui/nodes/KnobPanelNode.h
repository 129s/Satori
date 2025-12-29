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

    void setModules(const std::vector<ModuleUI>& modules,
                    bool surfaceOnly,
                    bool compactLayout);
    void setExternalHighlight(std::optional<FlowModule> module);
    void syncKnobs();

    float preferredHeight(float) const override;
    void arrange(const D2D1_RECT_F& bounds) override;
    void draw(const RenderResources& resources) override;

    bool onPointerDown(float x, float y) override;
    bool onPointerMove(float x, float y) override;
    void onPointerUp() override;
    std::optional<DebugBoxModel> debugBoxForPoint(float x, float y) const;
    std::shared_ptr<ParameterKnob> activeKnob() const;
    std::optional<FlowModule> activeModule() const;

private:
    struct KnobEntry {
        ModuleParamDescriptor descriptor;
        std::shared_ptr<ParameterKnob> knob;
        FlowModule module = FlowModule::kNone;
        bool isSurface = false;
    };

    struct Group {
        std::wstring title;
        FlowModule module = FlowModule::kNone;
        std::vector<KnobEntry> knobs;
        D2D1_RECT_F bounds{};
    };

    std::vector<Group> groups_;
    float minHeight_ = 240.0f;
    float groupSpacing_ = 16.0f;
    float padding_ = 8.0f;
    bool compactLayout_ = false;
    bool surfaceOnly_ = true;
    std::optional<FlowModule> externalHighlight_;
    std::optional<FlowModule> hoverModule_;
    std::weak_ptr<ParameterKnob> hoverKnob_;
    std::optional<FlowModule> draggingModule_;
    std::size_t maxColumns_ = 3;

    void rebuildGroups(const std::vector<ModuleUI>& modules);
    std::size_t effectiveColumns(std::size_t knobCount) const;
    std::optional<FlowModule> currentHighlight() const;
    bool updateHoverModule(float x, float y);
    };

}  // namespace winui
