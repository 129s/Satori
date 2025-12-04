#pragma once

#include <string>
#include <vector>

#include "win/ui/layout/UILayoutNode.h"

namespace winui {

class TextBlockNode : public UILayoutNode {
public:
    TextBlockNode();

    void setLines(std::vector<std::wstring> lines);
    void setPadding(float topBottom, float leftRight);
    void setLineHeight(float lineHeight);

    float preferredHeight(float width) const override;
    void draw(const RenderResources& resources) override;

private:
    std::vector<std::wstring> lines_;
    float paddingVertical_ = 8.0f;
    float paddingHorizontal_ = 8.0f;
    float lineHeight_ = 24.0f;
};

}  // namespace winui
