#include "win/ui/nodes/HeaderNode.h"

#include <algorithm>
#include <d2d1helper.h>

namespace winui {

HeaderNode::HeaderNode() = default;

void HeaderNode::setInstructions(std::vector<std::wstring> lines) {
    instructionLines_ = std::move(lines);
}

void HeaderNode::setStatus(const StatusInfo& status) {
    status_ = status;
}

float HeaderNode::preferredHeight(float) const {
    const std::size_t instructionCount =
        std::max<std::size_t>(1, instructionLines_.size());
    return 24.0f + static_cast<float>(instructionCount) * lineHeight_;
}

void HeaderNode::draw(const RenderResources& resources) {
    if (!resources.target || !resources.textBrush || !resources.textFormat) {
        return;
    }
    float y = bounds_.top + 12.0f;
    for (const auto& line : instructionLines_) {
        const auto rect = D2D1::RectF(bounds_.left + 12.0f, y,
                                      bounds_.left + 360.0f, y + lineHeight_);
        resources.target->DrawText(line.c_str(),
                                   static_cast<UINT32>(line.size()),
                                   resources.textFormat, rect,
                                   resources.textBrush);
        y += lineHeight_;
    }
    if (!status_.primary.empty()) {
        const auto rect = D2D1::RectF(bounds_.right - 360.0f,
                                      bounds_.top + 12.0f, bounds_.right - 12.0f,
                                      bounds_.top + 12.0f + lineHeight_);
        resources.target->DrawText(status_.primary.c_str(),
                                   static_cast<UINT32>(status_.primary.size()),
                                   resources.textFormat, rect,
                                   resources.textBrush);
    }
    if (!status_.secondary.empty()) {
        const auto rect = D2D1::RectF(bounds_.right - 360.0f,
                                      bounds_.top + 12.0f + lineHeight_,
                                      bounds_.right - 12.0f,
                                      bounds_.top + 12.0f + 2.0f * lineHeight_);
        resources.target->DrawText(status_.secondary.c_str(),
                                   static_cast<UINT32>(status_.secondary.size()),
                                   resources.textFormat, rect,
                                   resources.textBrush);
    }
}

}  // namespace winui
