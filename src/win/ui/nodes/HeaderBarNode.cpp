#include "win/ui/nodes/HeaderBarNode.h"

#include <algorithm>

#include <cstddef>
#include <d2d1helper.h>
#include <string_view>
#include <wrl/client.h>

#include "win/ui/nodes/DropdownSelectorNode.h"

namespace winui {

namespace {
float RectHeight(const D2D1_RECT_F& r) { return r.bottom - r.top; }
bool Contains(const D2D1_RECT_F& r, float x, float y) {
    return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
}

std::wstring ToSingleLine(std::wstring_view text) {
    std::wstring out;
    out.reserve(text.size());
    bool lastSpace = true;
    for (wchar_t ch : text) {
        if (ch == L'\r' || ch == L'\n' || ch == L'\t') {
            ch = L' ';
        }
        const bool isSpace = ch == L' ';
        if (isSpace) {
            if (lastSpace) {
                continue;
            }
            lastSpace = true;
        } else {
            lastSpace = false;
        }
        out.push_back(ch);
    }
    while (!out.empty() && out.front() == L' ') {
        out.erase(out.begin());
    }
    while (!out.empty() && out.back() == L' ') {
        out.pop_back();
    }
    return out;
}

struct WordWrappingGuard {
    explicit WordWrappingGuard(IDWriteTextFormat* format,
                               DWRITE_WORD_WRAPPING wrapping)
        : format_(format),
          oldWrapping_(format ? format->GetWordWrapping()
                              : DWRITE_WORD_WRAPPING_WRAP) {
        if (format_) {
            (void)format_->SetWordWrapping(wrapping);
        }
    }
    ~WordWrappingGuard() {
        if (format_) {
            (void)format_->SetWordWrapping(oldWrapping_);
        }
    }

    WordWrappingGuard(const WordWrappingGuard&) = delete;
    WordWrappingGuard& operator=(const WordWrappingGuard&) = delete;

private:
    IDWriteTextFormat* format_ = nullptr;
    DWRITE_WORD_WRAPPING oldWrapping_{};
};

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
      midiInputSelector_(std::make_shared<DropdownSelectorNode>()),
      sampleRateSelector_(std::make_shared<DropdownSelectorNode>()),
      bufferFramesSelector_(std::make_shared<DropdownSelectorNode>()) {
    midiButtons_.push_back(MidiButtonState{MidiButtonId::Load});
    midiButtons_.push_back(MidiButtonState{MidiButtonId::Play});
    midiButtons_.push_back(MidiButtonState{MidiButtonId::Pause});
    midiButtons_.push_back(MidiButtonState{MidiButtonId::Stop});
}       

void HeaderBarNode::setModel(const HeaderBarModel& model) {
    logoText_ = model.logoText.empty() ? L"Satori" : model.logoText;
    logoText_ = ToSingleLine(logoText_);

    deviceLabel_ = model.device.label.empty() ? L"Device" : model.device.label;
    midiInputLabel_ =
        model.midiInput.label.empty() ? L"Input" : model.midiInput.label;
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
    if (midiInputSelector_) {
        midiInputSelector_->setOnChanged({});
        midiInputSelector_->setItems(model.midiInput.items);
        midiInputSelector_->setPageSize(model.midiInput.pageSize);
        midiInputSelector_->setSelectedIndex(model.midiInput.selectedIndex);
        midiInputSelector_->setOnChanged(model.midiInput.onChanged);
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
    if (midiInputSelector_) list.push_back(midiInputSelector_);
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

    const float sampleRateDropMinW = 130.0f;
    const float sampleRateDropMaxW = 220.0f;
    const float midiInDropMinW = 160.0f;
    const float midiInDropMaxW = 320.0f;
    const float deviceDropMinW = 180.0f;
    const float deviceDropMaxW = 520.0f;

    const float labelWDeviceMin = 60.0f;
    const float labelWMidiMin = 70.0f;
    const float labelWSampleMin = 110.0f;
    const float labelWBufferMin = 110.0f;
    const float labelWDeviceMax = 140.0f;
    const float labelWMidiMax = 140.0f;
    const float labelWSampleMax = 180.0f;
    const float labelWBufferMax = 200.0f;

    const auto estimateLabelW = [](const std::wstring& text, float minW,
                                   float maxW) {
        const float kPadding = 12.0f;
        const float kPerChar = 8.5f;
        const float w = kPadding + kPerChar * static_cast<float>(text.size());
        return std::clamp(w, minW, maxW);
    };

    const float labelWDevice =
        estimateLabelW(deviceLabel_, labelWDeviceMin, labelWDeviceMax);
    const float labelWMidi =
        estimateLabelW(midiInputLabel_, labelWMidiMin, labelWMidiMax);
    const float labelWSample =
        estimateLabelW(sampleRateLabel_, labelWSampleMin, labelWSampleMax);
    const float labelWBuffer =
        estimateLabelW(bufferFramesLabel_, labelWBufferMin, labelWBufferMax);

    const float logoLeft = bounds_.left + paddingX;
    const float transportGap = 10.0f;
    const float midiIconSize = std::min(32.0f, dropdownH);
    const float midiIconGap = 0.0f;
    const float midiGroupW =
        static_cast<float>(midiButtons_.size()) * midiIconSize +
        std::max(0.0f, static_cast<float>(midiButtons_.size() - 1)) * midiIconGap;
    const float logoMinW = 110.0f;
    const float leftMinW = logoMinW + transportGap + midiGroupW;
    const float rightGroupLeftLimit = logoLeft + leftMinW + groupGap;

    const float boundsW = std::max(0.0f, bounds_.right - bounds_.left);
    const float rightBudget =
        std::max(0.0f, boundsW - paddingX * 2.0f - leftMinW);
    const float rightMinNeed =
        (labelWBuffer + labelGap + bufferW) + groupGap +
        (labelWSample + labelGap + sampleRateDropMinW) + groupGap +
        (labelWMidi + labelGap + midiInDropMinW) + groupGap +
        (labelWDevice + labelGap + deviceDropMinW);
    float extra = std::max(0.0f, rightBudget - rightMinNeed);

    float deviceDropW = deviceDropMinW;
    const float deviceExtraCap = std::max(0.0f, deviceDropMaxW - deviceDropMinW);
    const float deviceExtra = std::min(extra, deviceExtraCap);
    deviceDropW += deviceExtra;
    extra -= deviceExtra;

    float sampleRateDropW = sampleRateDropMinW;
    const float sampleExtraCap =
        std::max(0.0f, sampleRateDropMaxW - sampleRateDropMinW);
    const float sampleExtra = std::min(extra, sampleExtraCap);
    sampleRateDropW += sampleExtra;
    extra -= sampleExtra;

    float midiInDropW = midiInDropMinW;
    const float midiExtraCap = std::max(0.0f, midiInDropMaxW - midiInDropMinW);
    const float midiExtra = std::min(extra, midiExtraCap);
    midiInDropW += midiExtra;
    extra -= midiExtra;

    float right = bounds_.right - paddingX;

    const auto placeGroup = [&](float labelW, const std::wstring&,
                                float dropdownW,
                                D2D1_RECT_F& outLabelRect,
                                const std::shared_ptr<DropdownSelectorNode>& selector) {
        const float minLeft = rightGroupLeftLimit;
        if (right <= minLeft) {
            outLabelRect = D2D1::RectF(minLeft, yTop, minLeft, yBottom);
            if (selector) {
                selector->arrange(D2D1::RectF(minLeft, yTop, minLeft, yBottom));
            }
            right = minLeft - groupGap;
            return;
        }

        // Prefer keeping the label visible. If space is tight, shrink the
        // dropdown first (its text is ellipsized).
        const float groupLeftWanted = right - (labelW + labelGap + dropdownW);
        const float groupLeft = std::max(minLeft, groupLeftWanted);

        const float labelLeft = groupLeft;
        const float labelRight = std::min(right, labelLeft + labelW);
        outLabelRect = D2D1::RectF(labelLeft, yTop, labelRight, yBottom);

        const float dropLeft = std::min(right, labelRight + labelGap);
        const auto dropRect = D2D1::RectF(dropLeft, yTop, right, yBottom);
        if (selector) {
            selector->arrange(dropRect);
        }
        right = groupLeft - groupGap;
    };

    // Right side: BufferFrames, SampleRate, Device.
    placeGroup(labelWBuffer, bufferFramesLabel_, bufferW, bufferFramesLabelRect_,
               bufferFramesSelector_);
    placeGroup(labelWSample, sampleRateLabel_, sampleRateDropW, sampleRateLabelRect_,
               sampleRateSelector_);

    placeGroup(labelWMidi, midiInputLabel_, midiInDropW, midiInputLabelRect_,
               midiInputSelector_);

    placeGroup(labelWDevice, deviceLabel_, deviceDropW, deviceLabelRect_,
               deviceSelector_);

    // Left side: logo + transport (everything left of the device label area).
    const float transportRight =
        std::max(logoLeft, deviceLabelRect_.left - groupGap);
    const float transportLeft = std::max(logoLeft, transportRight - midiGroupW);
    midiRect_ = D2D1::RectF(transportLeft, yTop, transportRight, yBottom);

    const float logoRight = std::max(logoLeft, transportLeft - transportGap);
    logoRect_ = D2D1::RectF(logoLeft, bounds_.top + paddingY, logoRight,
                            bounds_.bottom - paddingY);

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
            resources.target->PushAxisAlignedClip(
                tr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            const WordWrappingGuard wrap(resources.textFormat,
                                         DWRITE_WORD_WRAPPING_NO_WRAP);
            const TextAlignmentGuard align(resources.textFormat,
                                           DWRITE_TEXT_ALIGNMENT_CENTER,
                                           DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            resources.target->DrawText(label.c_str(), static_cast<UINT32>(label.size()),
                                       resources.textFormat, tr, text);
            resources.target->PopAxisAlignedClip();
            text->SetOpacity(original);
        };

        if (!logoText_.empty() && logoRect_.right > logoRect_.left) {
            const auto tr = D2D1::RectF(logoRect_.left, logoRect_.top, logoRect_.right,
                                        logoRect_.bottom);
            resources.target->PushAxisAlignedClip(
                tr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            const WordWrappingGuard wrap(resources.textFormat,
                                         DWRITE_WORD_WRAPPING_NO_WRAP);
            const TextAlignmentGuard align(resources.textFormat,
                                           DWRITE_TEXT_ALIGNMENT_CENTER,
                                           DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            resources.target->DrawText(logoText_.c_str(),
                                       static_cast<UINT32>(logoText_.size()),
                                       resources.textFormat, tr, accent);
            resources.target->PopAxisAlignedClip();
        }

    drawLabel(deviceLabel_, deviceLabelRect_);
    drawLabel(midiInputLabel_, midiInputLabelRect_);
    drawLabel(sampleRateLabel_, sampleRateLabelRect_);
    drawLabel(bufferFramesLabel_, bufferFramesLabelRect_);

    if (deviceSelector_) deviceSelector_->draw(resources);
    if (midiInputSelector_) midiInputSelector_->draw(resources);
    if (sampleRateSelector_) sampleRateSelector_->draw(resources);
    if (bufferFramesSelector_) bufferFramesSelector_->draw(resources);

    // MIDI transport icons.
    if (!midiButtons_.empty() && midiRect_.right > midiRect_.left &&
        midiRect_.bottom > midiRect_.top) {
        Microsoft::WRL::ComPtr<ID2D1Factory> factory;
        resources.target->GetFactory(&factory);

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
        if (deviceSelector_)
            handled = deviceSelector_->onPointerDown(x, y) || handled;
        if (midiInputSelector_)
            handled = midiInputSelector_->onPointerDown(x, y) || handled;
        if (sampleRateSelector_)
            handled = sampleRateSelector_->onPointerDown(x, y) || handled;
        if (bufferFramesSelector_)
            handled = bufferFramesSelector_->onPointerDown(x, y) || handled;
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
        if (midiInputSelector_)
            handled = midiInputSelector_->onPointerMove(x, y) || handled;
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
        if (midiInputSelector_) midiInputSelector_->onPointerUp();
        if (sampleRateSelector_) sampleRateSelector_->onPointerUp();
        if (bufferFramesSelector_) bufferFramesSelector_->onPointerUp();
    }

}  // namespace winui
