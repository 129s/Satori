#include "win/ui/nodes/HeaderBarNode.h"

#include <algorithm>

#include <cstddef>
#include <d2d1helper.h>
#include <wrl/client.h>

#include "win/ui/nodes/DropdownSelectorNode.h"

namespace winui {

namespace {
float RectHeight(const D2D1_RECT_F& r) { return r.bottom - r.top; }
bool Contains(const D2D1_RECT_F& r, float x, float y) {
    return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
}

struct TextAlignmentGuard {
    explicit TextAlignmentGuard(IDWriteTextFormat* format,
                                DWRITE_TEXT_ALIGNMENT textAlignment,
                                DWRITE_PARAGRAPH_ALIGNMENT paragraphAlignment)
        : format_(format),
          oldText_(format ? format->GetTextAlignment() : DWRITE_TEXT_ALIGNMENT_LEADING),
          oldPara_(format ? format->GetParagraphAlignment() : DWRITE_PARAGRAPH_ALIGNMENT_NEAR) {
        if (format_) {
            (void)format_->SetTextAlignment(textAlignment);
            (void)format_->SetParagraphAlignment(paragraphAlignment);
        }
    }
    ~TextAlignmentGuard() {
        if (format_) {
            (void)format_->SetTextAlignment(oldText_);
            (void)format_->SetParagraphAlignment(oldPara_);
        }
    }

    TextAlignmentGuard(const TextAlignmentGuard&) = delete;
    TextAlignmentGuard& operator=(const TextAlignmentGuard&) = delete;

private:
    IDWriteTextFormat* format_ = nullptr;
    DWRITE_TEXT_ALIGNMENT oldText_{};
    DWRITE_PARAGRAPH_ALIGNMENT oldPara_{};
};
}  // namespace

HeaderBarNode::HeaderBarNode()
    : deviceSelector_(std::make_shared<DropdownSelectorNode>()),
      sampleRateSelector_(std::make_shared<DropdownSelectorNode>()),
      bufferFramesSelector_(std::make_shared<DropdownSelectorNode>()) {
    midiButtons_.push_back(MidiButtonState{MidiButtonId::Load});
    midiButtons_.push_back(MidiButtonState{MidiButtonId::Play});
    midiButtons_.push_back(MidiButtonState{MidiButtonId::Pause});
    midiButtons_.push_back(MidiButtonState{MidiButtonId::Stop});
}       

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

    const bool midiAvailable = model.midi.available;
    const bool isPlaying = model.midi.state == MidiTransportState::Playing;
    const bool isStopped = model.midi.state == MidiTransportState::Stopped;

    auto setMidiButton = [&](MidiButtonId id, bool enabled,
                             std::function<void()> onClick) {
        for (std::size_t i = 0; i < midiButtons_.size(); ++i) {
            auto& button = midiButtons_[i];
            if (button.id != id) {
                continue;
            }
            button.enabled = enabled;
            button.onClick = std::move(onClick);
            if (!button.enabled) {
                button.pressed = false;
                if (activeMidiButton_ == static_cast<int>(i)) {
                    activeMidiButton_ = -1;
                }
            }
            return;
        }
    };

    setMidiButton(MidiButtonId::Load, static_cast<bool>(model.midi.onLoad),
                  model.midi.onLoad);
    setMidiButton(MidiButtonId::Play,
                  midiAvailable && static_cast<bool>(model.midi.onPlay) &&
                      (!isPlaying),
                  model.midi.onPlay);
    setMidiButton(MidiButtonId::Pause,
                  midiAvailable && static_cast<bool>(model.midi.onPause) &&
                      isPlaying,
                  model.midi.onPause);
    setMidiButton(MidiButtonId::Stop,
                  midiAvailable && static_cast<bool>(model.midi.onStop) &&
                      (!isStopped),
                  model.midi.onStop);

    if (!midiAvailable) {
        for (auto& button : midiButtons_) {
            if (button.id == MidiButtonId::Load) {
                continue;
            }
            button.hovered = false;
        }
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

    const float transportGap = 10.0f;
    const float midiIconSize = std::min(32.0f, dropdownH);
    const float midiIconGap = 0.0f;
    const float midiGroupW = static_cast<float>(midiButtons_.size()) * midiIconSize;

    const float transportRight =
        std::max(logoLeft, deviceLabelRect_.left - groupGap);
    const float transportLeft = std::max(logoLeft, transportRight - midiGroupW);
    midiRect_ = D2D1::RectF(transportLeft, yTop, transportRight, yBottom);

    const float logoRight = std::max(logoLeft, transportLeft - transportGap);
    logoRect_ = D2D1::RectF(logoLeft, bounds_.top + paddingY, logoRight,
                            bounds_.bottom - paddingY);
    const float mixLeft = std::min(logoRect_.right, logoRect_.left + 96.0f);
    mixRect_ =
        D2D1::RectF(mixLeft, logoRect_.top, logoRect_.right, logoRect_.bottom);

    float x = midiRect_.left;
    for (auto& button : midiButtons_) {
        button.bounds = D2D1::RectF(x, yTop, x + midiIconSize, yBottom);
        x += midiIconSize + midiIconGap;
    }
}

void HeaderBarNode::draw(const RenderResources& resources) {
    if (!resources.target || !resources.textFormat) {
        return;
    }

    auto* bg = resources.panelBrush ? resources.panelBrush : resources.trackBrush;
    auto* text = resources.textBrush ? resources.textBrush : resources.gridBrush;
    auto* accent = resources.accentBrush ? resources.accentBrush : text;
    if (!bg || !text) {
        return;
    }

    resources.target->FillRectangle(bounds_, bg);

    const auto drawLabel = [&](const std::wstring& label, const D2D1_RECT_F& r) {
        if (label.empty() || r.right <= r.left) {
            return;
        }
        const auto tr = D2D1::RectF(r.left, r.top, r.right, r.bottom);
        const float original = text->GetOpacity();
        text->SetOpacity(original * 0.75f);
        const TextAlignmentGuard align(resources.textFormat,
                                       DWRITE_TEXT_ALIGNMENT_CENTER,
                                       DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        resources.target->DrawText(label.c_str(), static_cast<UINT32>(label.size()),
                                   resources.textFormat, tr, text);
        text->SetOpacity(original);
    };

    const auto brandRect =
        D2D1::RectF(logoRect_.left, logoRect_.top, mixRect_.left, logoRect_.bottom);
    if (!logoText_.empty() && brandRect.right > brandRect.left) {
        const auto tr =
            D2D1::RectF(brandRect.left, brandRect.top, brandRect.right, brandRect.bottom);
        const TextAlignmentGuard align(resources.textFormat,
                                       DWRITE_TEXT_ALIGNMENT_CENTER,
                                       DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        resources.target->DrawText(logoText_.c_str(),
                                   static_cast<UINT32>(logoText_.size()),
                                   resources.textFormat, tr, accent);
    }
    if (!mixSampleRateText_.empty() && mixRect_.right > mixRect_.left) {
        const auto lineBreak = mixSampleRateText_.find(L'\n');
        if (lineBreak != std::wstring::npos) {
            const std::wstring topLine = mixSampleRateText_.substr(0, lineBreak);
            const std::wstring bottomLine = mixSampleRateText_.substr(lineBreak + 1);

            const float h = mixRect_.bottom - mixRect_.top;
            const float lineH = std::max(12.0f, h * 0.5f);
            const auto trTop =
                D2D1::RectF(mixRect_.left, mixRect_.top, mixRect_.right,
                            std::min(mixRect_.bottom, mixRect_.top + lineH));
            const auto trBottom = D2D1::RectF(
                mixRect_.left, std::min(mixRect_.bottom, mixRect_.top + lineH - 2.0f),
                mixRect_.right, mixRect_.bottom);

            const float original = text->GetOpacity();
            text->SetOpacity(original * 0.75f);
            const TextAlignmentGuard align(resources.textFormat,
                                           DWRITE_TEXT_ALIGNMENT_CENTER,
                                           DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            resources.target->DrawText(topLine.c_str(), static_cast<UINT32>(topLine.size()),
                                       resources.textFormat, trTop, text);
            text->SetOpacity(original);
            const TextAlignmentGuard align2(resources.textFormat,
                                            DWRITE_TEXT_ALIGNMENT_CENTER,
                                            DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            resources.target->DrawText(bottomLine.c_str(),
                                       static_cast<UINT32>(bottomLine.size()),
                                       resources.textFormat, trBottom, text);
        } else {
            const auto tr =
                D2D1::RectF(mixRect_.left, mixRect_.top, mixRect_.right, mixRect_.bottom);
            const TextAlignmentGuard align(resources.textFormat,
                                           DWRITE_TEXT_ALIGNMENT_CENTER,
                                           DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            resources.target->DrawText(mixSampleRateText_.c_str(),
                                       static_cast<UINT32>(mixSampleRateText_.size()),
                                       resources.textFormat, tr, text);
        }
    }

    drawLabel(deviceLabel_, deviceLabelRect_);
    drawLabel(sampleRateLabel_, sampleRateLabelRect_);
    drawLabel(bufferFramesLabel_, bufferFramesLabelRect_);

    if (deviceSelector_) deviceSelector_->draw(resources);
    if (sampleRateSelector_) sampleRateSelector_->draw(resources);
    if (bufferFramesSelector_) bufferFramesSelector_->draw(resources);

    // MIDI transport icons.
    if (!midiButtons_.empty() && midiRect_.right > midiRect_.left &&
        midiRect_.bottom > midiRect_.top) {
        Microsoft::WRL::ComPtr<ID2D1Factory> factory;
        resources.target->GetFactory(&factory);

        ID2D1SolidColorBrush* groupBg =
            resources.trackBrush ? resources.trackBrush : resources.panelBrush;
        if (groupBg) {
            resources.target->FillRectangle(midiRect_, groupBg);
        }

        for (const auto& button : midiButtons_) {
            const auto& r = button.bounds;
            if (r.right <= r.left || r.bottom <= r.top) {
                continue;
            }

            ID2D1SolidColorBrush* iconBrush =
                resources.textBrush ? resources.textBrush : resources.gridBrush;
            if (!iconBrush) {
                continue;
            }

            const float originalIconOpacity = iconBrush->GetOpacity();

            if (!button.enabled) {
                iconBrush->SetOpacity(originalIconOpacity * 0.35f);
            } else if (button.pressed || button.hovered) {
                    ID2D1SolidColorBrush* overlay =
                        resources.accentBrush ? resources.accentBrush : iconBrush;
                    if (overlay) {
                        const float original = overlay->GetOpacity();
                        overlay->SetOpacity(original * (button.pressed ? 0.22f : 0.14f));
                        resources.target->FillRectangle(
                            D2D1::RectF(r.left + 0.5f, r.top + 0.5f, r.right - 0.5f,
                                       r.bottom - 0.5f),
                            overlay);
                        overlay->SetOpacity(original);
                    }
                }

            const float w = r.right - r.left;
            const float h = r.bottom - r.top;
            const float pad = std::max(4.0f, std::min(w, h) * 0.25f);
            const auto iconR = D2D1::RectF(r.left + pad, r.top + pad,
                                           r.right - pad, r.bottom - pad);

            switch (button.id) {
                case MidiButtonId::Load: {
                    const float midX = (iconR.left + iconR.right) * 0.5f;
                    const float midY = (iconR.top + iconR.bottom) * 0.55f;
                    const float trayY = iconR.bottom - 2.0f;
                    resources.target->DrawLine(D2D1::Point2F(iconR.left, trayY),
                                               D2D1::Point2F(iconR.right, trayY),
                                               iconBrush, 2.0f);
                    resources.target->DrawLine(D2D1::Point2F(midX, iconR.top),
                                               D2D1::Point2F(midX, midY),
                                               iconBrush, 2.0f);
                    resources.target->DrawLine(D2D1::Point2F(midX, midY),
                                               D2D1::Point2F(midX - 5.0f, midY - 5.0f),
                                               iconBrush, 2.0f);
                    resources.target->DrawLine(D2D1::Point2F(midX, midY),
                                               D2D1::Point2F(midX + 5.0f, midY - 5.0f),
                                               iconBrush, 2.0f);
                    break;
                }
                case MidiButtonId::Play: {
                    if (factory) {
                        Microsoft::WRL::ComPtr<ID2D1PathGeometry> geometry;
                        if (SUCCEEDED(factory->CreatePathGeometry(&geometry)) &&
                            geometry) {
                            Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
                            if (SUCCEEDED(geometry->Open(&sink)) && sink) {
                                const auto p0 =
                                    D2D1::Point2F(iconR.left, iconR.top);
                                const auto p1 = D2D1::Point2F(
                                    iconR.right, (iconR.top + iconR.bottom) * 0.5f);
                                const auto p2 =
                                    D2D1::Point2F(iconR.left, iconR.bottom);
                                sink->BeginFigure(p0, D2D1_FIGURE_BEGIN_FILLED);
                                sink->AddLine(p1);
                                sink->AddLine(p2);
                                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                                (void)sink->Close();
                                resources.target->FillGeometry(geometry.Get(),
                                                               iconBrush);
                            }
                        }
                    }
                    break;
                }
                case MidiButtonId::Pause: {
                    const float barW =
                        std::max(2.0f, (iconR.right - iconR.left) * 0.25f);
                    const float gap = barW * 0.6f;
                    const float leftX = iconR.left;
                    const float rightX = leftX + barW + gap;
                    resources.target->FillRectangle(
                        D2D1::RectF(leftX, iconR.top, leftX + barW, iconR.bottom),
                        iconBrush);
                    resources.target->FillRectangle(
                        D2D1::RectF(rightX, iconR.top, rightX + barW, iconR.bottom),
                        iconBrush);
                    break;
                }
                case MidiButtonId::Stop: {
                    const float s = std::min(iconR.right - iconR.left,
                                             iconR.bottom - iconR.top);
                    const float left = (iconR.left + iconR.right - s) * 0.5f;
                    const float top = (iconR.top + iconR.bottom - s) * 0.5f;
                    resources.target->FillRectangle(
                        D2D1::RectF(left, top, left + s, top + s), iconBrush);
                    break;
                }
                }

            iconBrush->SetOpacity(originalIconOpacity);
        }
    }
}

bool HeaderBarNode::onPointerDown(float x, float y) {
    for (std::size_t i = 0; i < midiButtons_.size(); ++i) {
        auto& button = midiButtons_[i];
        if (!button.enabled) {
            continue;
        }
        if (Contains(button.bounds, x, y)) {
            button.pressed = true;
            button.hovered = true;
            activeMidiButton_ = static_cast<int>(i);
            return true;
        }
    }

    bool handled = false;
    if (deviceSelector_) handled = deviceSelector_->onPointerDown(x, y) || handled;
    if (sampleRateSelector_) handled = sampleRateSelector_->onPointerDown(x, y) || handled;
    if (bufferFramesSelector_) handled = bufferFramesSelector_->onPointerDown(x, y) || handled;
    return handled;
}

bool HeaderBarNode::onPointerMove(float x, float y) {
    bool handled = false;

    if (activeMidiButton_ >= 0 &&
        static_cast<std::size_t>(activeMidiButton_) < midiButtons_.size()) {
        auto& active = midiButtons_[static_cast<std::size_t>(activeMidiButton_)];
        const bool inside = Contains(active.bounds, x, y);
        active.pressed = inside;
        active.hovered = inside;
        handled = true;
    }

    for (std::size_t i = 0; i < midiButtons_.size(); ++i) {
        auto& button = midiButtons_[i];
        if (static_cast<int>(i) == activeMidiButton_) {
            continue;
        }
        const bool inside = button.enabled && Contains(button.bounds, x, y);
        if (button.hovered != inside) {
            button.hovered = inside;
            handled = true;
        }
    }

    if (deviceSelector_) handled = deviceSelector_->onPointerMove(x, y) || handled;
    if (sampleRateSelector_) handled = sampleRateSelector_->onPointerMove(x, y) || handled;
    if (bufferFramesSelector_) handled = bufferFramesSelector_->onPointerMove(x, y) || handled;
    return handled;
}

void HeaderBarNode::onPointerUp() {
    if (activeMidiButton_ >= 0 &&
        static_cast<std::size_t>(activeMidiButton_) < midiButtons_.size()) {
        auto& active = midiButtons_[static_cast<std::size_t>(activeMidiButton_)];
        const bool execute = active.pressed;
        active.pressed = false;
        auto callback = active.onClick;
        activeMidiButton_ = -1;
        if (execute && callback) {
            callback();
        }
    }
    if (deviceSelector_) deviceSelector_->onPointerUp();
    if (sampleRateSelector_) sampleRateSelector_->onPointerUp();
    if (bufferFramesSelector_) bufferFramesSelector_->onPointerUp();
}

}  // namespace winui
