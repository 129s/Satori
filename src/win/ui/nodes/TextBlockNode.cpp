#include "win/ui/nodes/TextBlockNode.h"

#include <d2d1helper.h>

namespace winui {

TextBlockNode::TextBlockNode() = default;

void TextBlockNode::setLines(std::vector<std::wstring> lines) {
    lines_ = std::move(lines);
}

void TextBlockNode::setPadding(float topBottom, float leftRight) {
    paddingVertical_ = topBottom;
    paddingHorizontal_ = leftRight;
}

void TextBlockNode::setLineHeight(float lineHeight) {
    lineHeight_ = lineHeight;
}

float TextBlockNode::preferredHeight(float) const {
    if (lines_.empty()) {
        return paddingVertical_ * 2.0f;
    }
    return paddingVertical_ * 2.0f +
           static_cast<float>(lines_.size()) * lineHeight_;
}

void TextBlockNode::draw(const RenderResources& resources) {
    if (!resources.target || !resources.textBrush || !resources.textFormat) {
        return;
    }
    float y = bounds_.top + paddingVertical_;
    for (const auto& line : lines_) {
        const auto rect = D2D1::RectF(bounds_.left + paddingHorizontal_, y,
                                      bounds_.right - paddingHorizontal_,
                                      y + lineHeight_);
        resources.target->DrawText(line.c_str(),
                                   static_cast<UINT32>(line.size()),
                                   resources.textFormat, rect,
                                   resources.textBrush);
        y += lineHeight_;
    }
}

}  // namespace winui
