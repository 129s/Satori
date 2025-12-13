#include "win/ui/nodes/HeaderBarNode.h"

#include <algorithm>

#include <d2d1helper.h>

#include "win/ui/nodes/DropdownSelectorNode.h"

namespace winui {

namespace {
float RectHeight(const D2D1_RECT_F& r) { return r.bottom - r.top; }
}  // namespace

HeaderBarNode::HeaderBarNode()
    : deviceSelector_(std::make_shared<DropdownSelectorNode>()),
      sampleRateSelector_(std::make_shared<DropdownSelectorNode>()),
      bufferFramesSelector_(std::make_shared<DropdownSelectorNode>()) {}

void HeaderBarNode::setModel(const HeaderBarModel& model) {
    logoText_ = model.logoText.empty() ? L"Satori" : model.logoText;
    mixSampleRateText_ = model.mixSampleRateText;

    deviceLabel_ = model.device.label.empty() ? L"Device" : model.device.label;
    sampleRateLabel_ =
        model.sampleRate.label.empty() ? L"SampleRate" : model.sampleRate.label;
    bufferFramesLabel_ =
        model.bufferFrames.label.empty() ? L"BufferFrames" : model.bufferFrames.label;

    if (deviceSelector_) {
        deviceSelector_->setOnChanged({});
        deviceSelector_->setItems(model.device.items);
        deviceSelector_->setPageSize(model.device.pageSize);
        deviceSelector_->setSelectedIndex(model.device.selectedIndex);
        deviceSelector_->setOnChanged(model.device.onChanged);
    }
    if (sampleRateSelector_) {
        sampleRateSelector_->setOnChanged({});
        sampleRateSelector_->setItems(model.sampleRate.items);
        sampleRateSelector_->setPageSize(model.sampleRate.pageSize);
        sampleRateSelector_->setSelectedIndex(model.sampleRate.selectedIndex);
        sampleRateSelector_->setOnChanged(model.sampleRate.onChanged);
    }
    if (bufferFramesSelector_) {
        bufferFramesSelector_->setOnChanged({});
        bufferFramesSelector_->setItems(model.bufferFrames.items);
        bufferFramesSelector_->setPageSize(model.bufferFrames.pageSize);
        bufferFramesSelector_->setSelectedIndex(model.bufferFrames.selectedIndex);
        bufferFramesSelector_->setOnChanged(model.bufferFrames.onChanged);
    }
}

std::vector<std::shared_ptr<DropdownSelectorNode>> HeaderBarNode::selectors() const {
    std::vector<std::shared_ptr<DropdownSelectorNode>> list;
    if (deviceSelector_) list.push_back(deviceSelector_);
    if (sampleRateSelector_) list.push_back(sampleRateSelector_);
    if (bufferFramesSelector_) list.push_back(bufferFramesSelector_);
    return list;
}

float HeaderBarNode::preferredHeight(float) const {
    return 56.0f;
}

void HeaderBarNode::arrange(const D2D1_RECT_F& bounds) {
    UILayoutNode::arrange(bounds);

    const float paddingX = 14.0f;
    const float paddingY = 10.0f;
    const float groupGap = 14.0f;
    const float labelGap = 8.0f;

    const float dropdownH = std::max(24.0f, RectHeight(bounds_) - paddingY * 2.0f);
    const float yCenter = (bounds_.top + bounds_.bottom) * 0.5f;
    const float yTop = yCenter - dropdownH * 0.5f;
    const float yBottom = yCenter + dropdownH * 0.5f;

    const float bufferW = 150.0f;
    const float sampleRateW = 130.0f;
    const float deviceWDefault = 280.0f;

    const float labelWDevice = 60.0f;
    const float labelWSample = 90.0f;
    const float labelWBuffer = 110.0f;

    float right = bounds_.right - paddingX;

    const auto placeGroup = [&](float labelW, const std::wstring&,
                                float dropdownW,
                                D2D1_RECT_F& outLabelRect,
                                const std::shared_ptr<DropdownSelectorNode>& selector) {
        const float dropLeft = std::max(bounds_.left + paddingX, right - dropdownW);
        const auto dropRect = D2D1::RectF(dropLeft, yTop, right, yBottom);
        const float labelRight = dropRect.left - labelGap;
        outLabelRect = D2D1::RectF(labelRight - labelW, yTop, labelRight, yBottom);
        if (selector) {
            selector->arrange(dropRect);
        }
        right = outLabelRect.left - groupGap;
    };

    // Right side: BufferFrames, SampleRate, Device.
    placeGroup(labelWBuffer, bufferFramesLabel_, bufferW, bufferFramesLabelRect_,
               bufferFramesSelector_);
    placeGroup(labelWSample, sampleRateLabel_, sampleRateW, sampleRateLabelRect_,
               sampleRateSelector_);

    // Device group gets the remaining space, but keeps a reasonable minimum width.
    const float deviceDropMaxW = deviceWDefault;
    const float deviceDropMinW = 180.0f;
    const float remainingForDevice =
        std::max(0.0f, right - (bounds_.left + paddingX + 160.0f));
    const float deviceDropW =
        std::clamp(remainingForDevice, deviceDropMinW, deviceDropMaxW);
    placeGroup(labelWDevice, deviceLabel_, deviceDropW, deviceLabelRect_,
               deviceSelector_);

    // Left side: logo + mix info (everything left of the device label area).
    const float logoLeft = bounds_.left + paddingX;
    const float logoRight = std::max(logoLeft, deviceLabelRect_.left - groupGap);
    logoRect_ = D2D1::RectF(logoLeft, bounds_.top + paddingY, logoRight,
                            bounds_.bottom - paddingY);
    const float mixLeft = std::min(logoRect_.right, logoRect_.left + 140.0f);
    mixRect_ = D2D1::RectF(mixLeft, logoRect_.top, logoRect_.right, logoRect_.bottom);
}

void HeaderBarNode::draw(const RenderResources& resources) {
    if (!resources.target || !resources.textFormat) {
        return;
    }

    auto* bg = resources.panelBrush ? resources.panelBrush : resources.trackBrush;
    auto* border = resources.gridBrush ? resources.gridBrush : resources.trackBrush;
    auto* text = resources.textBrush ? resources.textBrush : resources.gridBrush;
    auto* accent = resources.accentBrush ? resources.accentBrush : text;
    if (!bg || !border || !text) {
        return;
    }

    resources.target->FillRectangle(bounds_, bg);
    resources.target->DrawLine(D2D1::Point2F(bounds_.left, bounds_.bottom - 0.5f),
                               D2D1::Point2F(bounds_.right, bounds_.bottom - 0.5f),
                               border, 1.0f);

    const auto drawLabel = [&](const std::wstring& label, const D2D1_RECT_F& r) {
        if (label.empty() || r.right <= r.left) {
            return;
        }
        const auto tr = D2D1::RectF(r.left, r.top + 3.0f, r.right, r.bottom - 3.0f);
        resources.target->DrawText(label.c_str(), static_cast<UINT32>(label.size()),
                                   resources.textFormat, tr, text);
    };

    if (!logoText_.empty() && logoRect_.right > logoRect_.left) {
        const auto tr =
            D2D1::RectF(logoRect_.left, logoRect_.top + 1.0f, logoRect_.right,
                        logoRect_.bottom - 1.0f);
        resources.target->DrawText(logoText_.c_str(),
                                   static_cast<UINT32>(logoText_.size()),
                                   resources.textFormat, tr, accent);
    }
    if (!mixSampleRateText_.empty() && mixRect_.right > mixRect_.left) {
        const auto tr =
            D2D1::RectF(mixRect_.left, mixRect_.top + 1.0f, mixRect_.right,
                        mixRect_.bottom - 1.0f);
        resources.target->DrawText(mixSampleRateText_.c_str(),
                                   static_cast<UINT32>(mixSampleRateText_.size()),
                                   resources.textFormat, tr, text);
    }

    drawLabel(deviceLabel_, deviceLabelRect_);
    drawLabel(sampleRateLabel_, sampleRateLabelRect_);
    drawLabel(bufferFramesLabel_, bufferFramesLabelRect_);

    if (deviceSelector_) deviceSelector_->draw(resources);
    if (sampleRateSelector_) sampleRateSelector_->draw(resources);
    if (bufferFramesSelector_) bufferFramesSelector_->draw(resources);
}

bool HeaderBarNode::onPointerDown(float x, float y) {
    bool handled = false;
    if (deviceSelector_) handled = deviceSelector_->onPointerDown(x, y) || handled;
    if (sampleRateSelector_) handled = sampleRateSelector_->onPointerDown(x, y) || handled;
    if (bufferFramesSelector_) handled = bufferFramesSelector_->onPointerDown(x, y) || handled;
    return handled;
}

bool HeaderBarNode::onPointerMove(float x, float y) {
    bool handled = false;
    if (deviceSelector_) handled = deviceSelector_->onPointerMove(x, y) || handled;
    if (sampleRateSelector_) handled = sampleRateSelector_->onPointerMove(x, y) || handled;
    if (bufferFramesSelector_) handled = bufferFramesSelector_->onPointerMove(x, y) || handled;
    return handled;
}

void HeaderBarNode::onPointerUp() {
    if (deviceSelector_) deviceSelector_->onPointerUp();
    if (sampleRateSelector_) sampleRateSelector_->onPointerUp();
    if (bufferFramesSelector_) bufferFramesSelector_->onPointerUp();
}

}  // namespace winui
