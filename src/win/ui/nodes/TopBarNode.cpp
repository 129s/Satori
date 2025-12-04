#include "win/ui/nodes/TopBarNode.h"

#include <sstream>

#include <d2d1helper.h>

namespace winui {

TopBarNode::TopBarNode() = default;

void TopBarNode::setTitle(std::wstring title) {
    title_ = std::move(title);
}

void TopBarNode::setSampleRate(float sampleRate) {
    sampleRate_ = sampleRate;
}

void TopBarNode::setAudioOnline(bool online) {
    audioOnline_ = online;
}

void TopBarNode::setStatusText(std::wstring text) {
    statusText_ = std::move(text);
}

float TopBarNode::preferredHeight(float) const {
    return 40.0f;
}

void TopBarNode::draw(const RenderResources& resources) {
    if (!resources.target || !resources.textBrush || !resources.textFormat) {
        return;
    }

    auto* backgroundBrush =
        resources.panelBrush ? resources.panelBrush : resources.trackBrush;
    if (backgroundBrush) {
        resources.target->FillRectangle(bounds_, backgroundBrush);
    }

    const float width = bounds_.right - bounds_.left;
    const float height = bounds_.bottom - bounds_.top;

    // 左侧标题 + 采样率，右侧简要状态文案，中间预留一段间隔，避免在 1280px 宽度下产生文本重叠。
    const float horizontalPadding = 12.0f;
    const float statusMaxWidth = 320.0f;
    const float gapBetweenTitleAndStatus = 8.0f;

    std::wstringstream ss;
    if (!title_.empty()) {
        ss << title_;
    } else {
        ss << L"Satori";
    }
    if (sampleRate_ > 0.0f) {
        ss << L"  " << static_cast<int>(sampleRate_) << L" Hz";
    }
    const auto titleText = ss.str();

    // 右侧状态区域优先保障固定宽度，其左边界不早于窗口中线，
    // 再在其左侧为标题预留空间，保证两个文本矩形不会重叠。
    const float statusRight = bounds_.right - horizontalPadding;
    const float statusLeftCandidate = statusRight - statusMaxWidth;
    const float statusLeft =
        std::max(bounds_.left + width * 0.5f, statusLeftCandidate);
    const auto statusRect = D2D1::RectF(
        statusLeft, bounds_.top, statusRight, bounds_.bottom);

    const float titleLeft = bounds_.left + horizontalPadding;
    const float titleRightMax = bounds_.left + width * 0.7f;
    const float titleRightSafe = statusLeft - gapBetweenTitleAndStatus;
    const float titleRight =
        std::min(titleRightMax, std::max(titleLeft, titleRightSafe));

    const auto titleRect =
        D2D1::RectF(titleLeft, bounds_.top, titleRight, bounds_.bottom);

    resources.target->DrawText(
        titleText.c_str(), static_cast<UINT32>(titleText.size()),
        resources.textFormat, titleRect, resources.textBrush);

    // 右侧简单状态文案（可选）
    if (!statusText_.empty()) {
        resources.target->DrawText(
            statusText_.c_str(), static_cast<UINT32>(statusText_.size()),
            resources.textFormat, statusRect, resources.textBrush);
    }

    // 音频在线/离线指示灯
    if (resources.accentBrush) {
        const float radius = 5.0f;
        const float cx = bounds_.right - 18.0f;
        const float cy = bounds_.top + height * 0.5f;
        auto* brush =
            audioOnline_ ? resources.accentBrush : resources.gridBrush;
        if (!brush) {
            brush = resources.accentBrush;
        }
        D2D1_ELLIPSE ellipse{{cx, cy}, radius, radius};
        resources.target->FillEllipse(ellipse, brush);
    }
}

}  // namespace winui
