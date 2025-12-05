#include "win/ui/nodes/TopBarNode.h"

#include <algorithm>
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

void TopBarNode::setSecondaryStatusText(std::wstring text) {
    secondaryStatusText_ = std::move(text);
}

float TopBarNode::preferredHeight(float) const {
    // 预留更多垂直空间以容纳双行状态文案与上下内边距。
    return 52.0f;
}

void TopBarNode::draw(const RenderResources& resources) {
    if (!resources.target || !resources.textBrush || !resources.textFormat) {
        return;
    }

    auto* backgroundBrush =
        resources.panelBrush ? resources.panelBrush : resources.trackBrush;
    if (backgroundBrush) {
        resources.target->FillRectangle(bounds_, backgroundBrush);
        if (resources.trackBrush) {
            const auto divider = D2D1::RectF(
                bounds_.left, bounds_.bottom - 1.5f, bounds_.right,
                bounds_.bottom);
            resources.target->FillRectangle(divider, resources.trackBrush);
        }
    }

    const float width = bounds_.right - bounds_.left;
    const float height = bounds_.bottom - bounds_.top;
    const float paddingX = 14.0f;
    const float paddingY = 6.0f;
    const float gapBetweenTitleAndStatus = 12.0f;

    // 右侧状态块预留更宽的空间，支持双行（音频状态 + 预设状态）。
    const float statusMaxWidth = 420.0f;
    const float statusRight = bounds_.right - paddingX;
    const float statusLeftCandidate = statusRight - statusMaxWidth;
    const float statusLeft =
        std::max(bounds_.left + width * 0.55f, statusLeftCandidate);

    // 左侧标题区域在不与状态块重叠的前提下尽量放宽。
    const float titleLeft = bounds_.left + paddingX;
    const float titleRightLimit = bounds_.left + width * 0.60f;
    const float titleRightSafe = statusLeft - gapBetweenTitleAndStatus;
    const float titleRight =
        std::min(titleRightLimit, std::max(titleLeft, titleRightSafe));

    const auto titleRect = D2D1::RectF(titleLeft, bounds_.top + paddingY,
                                       titleRight, bounds_.bottom - paddingY);

    const float indicatorRadius = 5.5f;
    const float indicatorGap = 8.0f;
    const float indicatorSlot = indicatorRadius * 2.0f + indicatorGap;

    const float statusTextLeft =
        std::min(statusRight, statusLeft + indicatorSlot);
    const auto statusRect =
        D2D1::RectF(statusTextLeft, bounds_.top + paddingY, statusRight,
                    bounds_.bottom - paddingY);

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

    resources.target->DrawText(
        titleText.c_str(), static_cast<UINT32>(titleText.size()),
        resources.textFormat, titleRect, resources.textBrush);

    std::wstringstream status;
    bool hasStatusContent = false;
    if (!statusText_.empty()) {
        status << statusText_;
        hasStatusContent = true;
    }
    if (!secondaryStatusText_.empty()) {
        if (hasStatusContent) {
            status << L"\n";
        }
        status << secondaryStatusText_;
        hasStatusContent = true;
    }
    const auto statusText = status.str();

    // 右侧状态文案（可选，支持双行）
    const bool hasStatusArea =
        statusRect.right > statusRect.left && statusRect.bottom > statusRect.top;
    if (hasStatusContent && hasStatusArea) {
        resources.target->DrawText(
            statusText.c_str(), static_cast<UINT32>(statusText.size()),
            resources.textFormat, statusRect, resources.textBrush);
    }

    // 音频在线/离线指示灯
    if (resources.accentBrush) {
        const float cx = statusLeft + indicatorRadius;
        const float cy = bounds_.top + height * 0.5f;
        auto* brush =
            audioOnline_ ? resources.accentBrush : resources.gridBrush;
        if (!brush) {
            brush = resources.accentBrush;
        }
        D2D1_ELLIPSE ellipse{{cx, cy}, indicatorRadius, indicatorRadius};
        resources.target->FillEllipse(ellipse, brush);
        if (resources.textBrush) {
            resources.target->DrawEllipse(ellipse, resources.textBrush, 1.0f);
        }
    }
}

}  // namespace winui
