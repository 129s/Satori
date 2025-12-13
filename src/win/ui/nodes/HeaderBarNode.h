#pragma once

#include <memory>
#include <string>
#include <vector>

#include "win/ui/UIModel.h"
#include "win/ui/layout/UILayoutNode.h"

namespace winui {

class DropdownSelectorNode;

class HeaderBarNode : public UILayoutNode {
public:
    HeaderBarNode();

    void setModel(const HeaderBarModel& model);

    std::shared_ptr<DropdownSelectorNode> deviceSelector() const { return deviceSelector_; }
    std::shared_ptr<DropdownSelectorNode> sampleRateSelector() const { return sampleRateSelector_; }
    std::shared_ptr<DropdownSelectorNode> bufferFramesSelector() const { return bufferFramesSelector_; }

    std::vector<std::shared_ptr<DropdownSelectorNode>> selectors() const;

    float preferredHeight(float width) const override;
    void arrange(const D2D1_RECT_F& bounds) override;
    void draw(const RenderResources& resources) override;

    bool onPointerDown(float x, float y) override;
    bool onPointerMove(float x, float y) override;
    void onPointerUp() override;

private:
    std::wstring logoText_ = L"Satori";
    std::wstring mixSampleRateText_;

    std::wstring deviceLabel_ = L"Device";
    std::wstring sampleRateLabel_ = L"SampleRate";
    std::wstring bufferFramesLabel_ = L"BufferFrames";

    std::shared_ptr<DropdownSelectorNode> deviceSelector_;
    std::shared_ptr<DropdownSelectorNode> sampleRateSelector_;
    std::shared_ptr<DropdownSelectorNode> bufferFramesSelector_;

    D2D1_RECT_F logoRect_{};
    D2D1_RECT_F mixRect_{};
    D2D1_RECT_F deviceLabelRect_{};
    D2D1_RECT_F sampleRateLabelRect_{};
    D2D1_RECT_F bufferFramesLabelRect_{};
};

}  // namespace winui
