#pragma once

#include "win/ui/UIModel.h"
#include "win/ui/layout/UILayoutNode.h"

namespace winui {

class HeaderNode : public UILayoutNode {
public:
    HeaderNode();

    void setInstructions(std::vector<std::wstring> lines);
    void setStatus(const StatusInfo& status);

    float preferredHeight(float) const override;
    void draw(const RenderResources& resources) override;

private:
    std::vector<std::wstring> instructionLines_;
    StatusInfo status_;
    float lineHeight_ = 26.0f;
};

}  // namespace winui
