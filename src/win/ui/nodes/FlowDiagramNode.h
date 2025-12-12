#pragma once

#include <optional>

#include "win/ui/UIModel.h"
#include "win/ui/WaveformView.h"
#include "win/ui/layout/UILayoutNode.h"

namespace winui {

// 合成流程图节点：展示 Excitation → String Loop → Body → Room 的信号流。
class FlowDiagramNode : public UILayoutNode {
public:
    FlowDiagramNode();

    void setDiagramState(const FlowDiagramState& state);
    void setHighlightedModule(FlowModule module);
    void setWaveformSamples(const std::vector<float>& samples) override;
    void setOnModuleSelected(std::function<void(FlowModule)> callback);

    float preferredHeight(float) const override;
    void draw(const RenderResources& resources) override;
    bool onPointerDown(float x, float y) override;

private:
    FlowDiagramState state_{};
    WaveformView waveformView_;
    float preferredHeight_ = 260.0f;
    std::function<void(FlowModule)> onModuleSelected_;
    D2D1_RECT_F excitationRect_{};
    D2D1_RECT_F stringRect_{};
    D2D1_RECT_F bodyRect_{};
    D2D1_RECT_F roomRect_{};

    void drawExcitation(ID2D1HwndRenderTarget* target,
                        ID2D1SolidColorBrush* textBrush,
                        ID2D1SolidColorBrush* gridBrush,
                        ID2D1SolidColorBrush* excitationBrush,
                        ID2D1SolidColorBrush* accentBrush,
                        IDWriteTextFormat* textFormat,
                        const D2D1_RECT_F& rect,
                        bool highlighted);

    void drawString(ID2D1HwndRenderTarget* target,
                    ID2D1SolidColorBrush* textBrush,
                    ID2D1SolidColorBrush* gridBrush,
                    ID2D1SolidColorBrush* accentBrush,
                    IDWriteTextFormat* textFormat,
                    const D2D1_RECT_F& rect,
                    bool highlighted);

    void drawBody(ID2D1HwndRenderTarget* target,
                  ID2D1SolidColorBrush* textBrush,
                  ID2D1SolidColorBrush* gridBrush,
                  ID2D1SolidColorBrush* accentBrush,
                  IDWriteTextFormat* textFormat,
                  const D2D1_RECT_F& rect,
                  bool highlighted);

    void drawRoom(ID2D1HwndRenderTarget* target,
                  ID2D1SolidColorBrush* panelBrush,
                  ID2D1SolidColorBrush* textBrush,
                  ID2D1SolidColorBrush* gridBrush,
                  ID2D1SolidColorBrush* accentBrush,
                      IDWriteTextFormat* textFormat,
                      const D2D1_RECT_F& rect,
                      bool highlighted);

    std::optional<FlowModule> hitTestModule(float x, float y) const;
};

}  // namespace winui
