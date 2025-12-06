#pragma once

#include "win/ui/UIModel.h"
#include "win/ui/WaveformView.h"
#include "win/ui/layout/UILayoutNode.h"

namespace winui {

// 合成流程图节点：展示 Excitation → String Loop → Body → Room 的信号流。
class FlowDiagramNode : public UILayoutNode {
public:
    FlowDiagramNode();

    void setDiagramState(const FlowDiagramState& state);
    void setWaveformSamples(const std::vector<float>& samples) override;

    float preferredHeight(float) const override;
    void draw(const RenderResources& resources) override;

private:
    FlowDiagramState state_{};
    WaveformView waveformView_;
    float preferredHeight_ = 260.0f;

    void drawExcitation(ID2D1HwndRenderTarget* target,
                        ID2D1SolidColorBrush* gridBrush,
                        ID2D1SolidColorBrush* accentBrush,
                        IDWriteTextFormat* textFormat,
                        const D2D1_RECT_F& rect);

    void drawString(ID2D1HwndRenderTarget* target,
                    ID2D1SolidColorBrush* gridBrush,
                    ID2D1SolidColorBrush* accentBrush,
                    IDWriteTextFormat* textFormat,
                    const D2D1_RECT_F& rect);

    void drawBody(ID2D1HwndRenderTarget* target,
                  ID2D1SolidColorBrush* gridBrush,
                  ID2D1SolidColorBrush* accentBrush,
                  IDWriteTextFormat* textFormat,
                  const D2D1_RECT_F& rect);

    void drawRoom(ID2D1HwndRenderTarget* target,
                  ID2D1SolidColorBrush* panelBrush,
                  ID2D1SolidColorBrush* gridBrush,
                  ID2D1SolidColorBrush* accentBrush,
                  IDWriteTextFormat* textFormat,
                  const D2D1_RECT_F& rect);
};

}  // namespace winui
