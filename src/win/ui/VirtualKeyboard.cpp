#include "win/ui/VirtualKeyboard.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cwchar>

#include <d2d1helper.h>

namespace winui {

namespace {

constexpr float kPaddingInset = 2.0f;
constexpr float kBlackWidthRatio = 0.6f;
constexpr float kBlackHeightRatio = 0.62f;
constexpr float kBlackCenterShiftRatio = 0.12f;

bool IsBlackNote(int noteIndex) {
    switch (noteIndex) {
        case 1:
        case 3:
        case 6:
        case 8:
        case 10:
            return true;
        default:
            return false;
    }
}

std::wstring MakeNoteLabel(int midiNote) {
    static const std::array<const wchar_t*, 12> kNoteNames = {
        L"C", L"C#", L"D", L"D#", L"E", L"F",
        L"F#", L"G", L"G#", L"A", L"A#", L"B"};
    const int noteIndex = (midiNote % 12 + 12) % 12;
    const int octave = midiNote / 12 - 1;
    wchar_t buffer[8] = {};
    swprintf_s(buffer, L"%s%d", kNoteNames[noteIndex], octave);
    return std::wstring(buffer);
}

D2D1_RECT_F InsetRect(const D2D1_RECT_F& rect, float inset) {
    return D2D1::RectF(rect.left + inset, rect.top + inset, rect.right - inset,
                       rect.bottom - inset);
}

}  // namespace

void VirtualKeyboard::setBounds(const D2D1_RECT_F& bounds) {
    bounds_ = bounds;
    layoutKeys();
}

void VirtualKeyboard::setCallback(Callback callback) {
    callback_ = std::move(callback);
}

void VirtualKeyboard::setPianoLayout(int baseMidiNote, int octaveCount) {
    buildPianoKeys(baseMidiNote, octaveCount);
    layoutKeys();
}

void VirtualKeyboard::setShowLabels(bool enabled) {
    showLabels_ = enabled;
}

void VirtualKeyboard::setHoverOutline(bool enabled) {
    showHoverOutline_ = enabled;
}

void VirtualKeyboard::draw(ID2D1HwndRenderTarget* target,
                           ID2D1SolidColorBrush* borderBrush,
                           ID2D1SolidColorBrush* fillBrush,
                           ID2D1SolidColorBrush* activeBrush,
                           IDWriteTextFormat* textFormat) const {
    KeyboardColors colors{};
    colors.whiteBorder = borderBrush;
    colors.blackBorder = borderBrush;
    colors.hoverOutline = borderBrush;
    colors.whiteFill = fillBrush;
    colors.blackFill = fillBrush;
    colors.whitePressed = activeBrush;
    colors.blackPressed = activeBrush;
    colors.whiteText = borderBrush;
    colors.blackText = borderBrush;
    draw(target, colors, textFormat);
}

void VirtualKeyboard::draw(ID2D1HwndRenderTarget* target,
                           const KeyboardColors& colors,
                           IDWriteTextFormat* textFormat) const {
    if (!target || !textFormat) {
        return;
    }

    auto renderKeyList = [&](const std::vector<std::size_t>& indices) {
        for (std::size_t idx : indices) {
            if (idx >= keys_.size()) {
                continue;
            }
            const Key& key = keys_[idx];
            ID2D1SolidColorBrush* fill =
                key.isBlack ? colors.blackFill : colors.whiteFill;
            ID2D1SolidColorBrush* pressed =
                key.isBlack ? colors.blackPressed : colors.whitePressed;
            ID2D1SolidColorBrush* border =
                key.isBlack ? colors.blackBorder : colors.whiteBorder;
            ID2D1SolidColorBrush* textBrush =
                key.isBlack ? colors.blackText : colors.whiteText;
            if (!fill || !border) {
                continue;
            }
            if (key.pressed && pressed) {
                fill = pressed;
            }
            target->FillRectangle(key.bounds, fill);
            target->DrawRectangle(key.bounds, border, 1.0f);
            if (showHoverOutline_ && key.hovered && colors.hoverOutline) {
                target->DrawRectangle(key.bounds, colors.hoverOutline, 1.5f);
            }
            if (showLabels_ && textBrush && !key.label.empty()) {
                target->DrawText(key.label.c_str(),
                                 static_cast<UINT32>(key.label.size()),
                                 textFormat, key.bounds, textBrush);
            }
        }
    };

    renderKeyList(whiteKeyOrder_);
    renderKeyList(blackKeyOrder_);
}

bool VirtualKeyboard::onPointerDown(float x, float y) {
    Key* key = hitTest(x, y);
    if (!key) {
        updateHoveredKey(nullptr);
        return false;
    }
    dragging_ = true;
    activeKey_ = key;
    updateHoveredKey(key);
    triggerKey(key);
    return true;
}

bool VirtualKeyboard::onPointerMove(float x, float y) {
    bool handled = false;

    if (dragging_) {
        Key* key = hitTest(x, y);
        if (key && key != activeKey_) {
            releaseKey(activeKey_);
            activeKey_ = key;
            triggerKey(key);
        }
        handled = key != nullptr;
    }

    Key* hovered = hitTest(x, y);
    updateHoveredKey(hovered);
    for (auto& key : keys_) {
        const bool inside = (&key == hovered);
        if (key.hovered != inside) {
            key.hovered = inside;
            handled = true;
        }
    }

    return handled;
}

void VirtualKeyboard::onPointerUp() {
    dragging_ = false;
    if (activeKey_) {
        releaseKey(activeKey_);
        activeKey_ = nullptr;
    }
    updateHoveredKey(nullptr);
    for (auto& key : keys_) {
        key.hovered = false;
    }
}

bool VirtualKeyboard::pressKeyByMidi(int midiNote) {
    auto it = midiToKeyIndex_.find(midiNote);
    if (it == midiToKeyIndex_.end()) {
        return false;
    }
    Key& key = keys_[it->second];
    triggerKey(&key);
    return true;
}

void VirtualKeyboard::releaseKeyByMidi(int midiNote) {
    auto it = midiToKeyIndex_.find(midiNote);
    if (it == midiToKeyIndex_.end()) {
        return;
    }
    releaseKey(&keys_[it->second]);
}

void VirtualKeyboard::releaseAllKeys() {
    for (auto& key : keys_) {
        if (key.pressed) {
            releaseKey(&key);
        }
        key.hovered = false;
    }
    dragging_ = false;
    activeKey_ = nullptr;
    hoveredKey_ = nullptr;
}

bool VirtualKeyboard::focusedKeyBounds(D2D1_RECT_F& outBounds) const {
    if (activeKey_) {
        outBounds = activeKey_->bounds;
        return true;
    }
    if (hoveredKey_) {
        outBounds = hoveredKey_->bounds;
        return true;
    }
    return false;
}

void VirtualKeyboard::buildPianoKeys(int baseMidiNote, int octaveCount) {
    keys_.clear();
    whiteKeyOrder_.clear();
    blackKeyOrder_.clear();
    midiToKeyIndex_.clear();
    activeKey_ = nullptr;
    hoveredKey_ = nullptr;
    dragging_ = false;

    baseMidiNote_ = baseMidiNote;
    octaveCount_ = std::max(0, octaveCount);
    if (octaveCount_ <= 0) {
        totalWhiteKeys_ = 0;
        return;
    }

    const int totalNotes = octaveCount_ * 12;
    int whiteSlot = 0;
    keys_.reserve(static_cast<std::size_t>(totalNotes));

    for (int i = 0; i < totalNotes; ++i) {
        const int midi = baseMidiNote_ + i;
        const int noteIndex = (midi % 12 + 12) % 12;
        Key key;
        key.label = MakeNoteLabel(midi);
        key.frequency = midiToFrequency(midi);
        key.isBlack = IsBlackNote(noteIndex);
        key.midiNote = midi;
        key.noteIndex = noteIndex;
        if (key.isBlack) {
            key.leftWhiteSlot = std::max(0, whiteSlot - 1);
            key.rightWhiteSlot = whiteSlot;
            blackKeyOrder_.push_back(keys_.size());
        } else {
            key.whiteSlot = whiteSlot;
            whiteKeyOrder_.push_back(keys_.size());
            ++whiteSlot;
        }
        midiToKeyIndex_[midi] = keys_.size();
        keys_.push_back(std::move(key));
    }

    totalWhiteKeys_ = whiteSlot;
}

void VirtualKeyboard::layoutKeys() {
    if (keys_.empty()) {
        return;
    }
    layoutPiano();
}

void VirtualKeyboard::layoutPiano() {
    if (totalWhiteKeys_ <= 0) {
        return;
    }
    const float width = bounds_.right - bounds_.left;
    const float height = bounds_.bottom - bounds_.top;
    if (width <= 0.0f || height <= 0.0f) {
        return;
    }

    const float whiteWidth = width / static_cast<float>(totalWhiteKeys_);
    const float whiteInset = std::min(kPaddingInset, whiteWidth * 0.15f);
    float x = bounds_.left;
    for (std::size_t slot = 0; slot < whiteKeyOrder_.size(); ++slot) {
        const std::size_t idx = whiteKeyOrder_[slot];
        if (idx >= keys_.size()) {
            continue;
        }
        auto& key = keys_[idx];
        key.bounds = D2D1::RectF(x + whiteInset, bounds_.top + whiteInset,
                                 x + whiteWidth - whiteInset, bounds_.bottom);
        x += whiteWidth;
    }

    const float blackWidth =
        std::min(whiteWidth * kBlackWidthRatio, whiteWidth - 2.0f);
    const float blackHalf = blackWidth * 0.5f;
    const float blackHeight = height * kBlackHeightRatio;
    const float blackTop = bounds_.top + whiteInset;
    for (std::size_t idx : blackKeyOrder_) {
        if (idx >= keys_.size()) {
            continue;
        }
        auto& key = keys_[idx];
        if (key.leftWhiteSlot < 0 || key.rightWhiteSlot > totalWhiteKeys_) {
            key.bounds = {};
            continue;
        }
        const float leftCenter =
            bounds_.left + (static_cast<float>(key.leftWhiteSlot) + 0.5f) * whiteWidth;
        const float rightCenter =
            bounds_.left + (static_cast<float>(key.rightWhiteSlot) + 0.5f) * whiteWidth;
        float center = (leftCenter + rightCenter) * 0.5f;
        if (key.noteIndex >= 0) {
            const int noteIndex = key.noteIndex % 12;
            float shiftSign = 0.0f;
            if (noteIndex == 1 || noteIndex == 6) {
                shiftSign = -1.0f;  // C#, F# 更靠左
            } else if (noteIndex == 3 || noteIndex == 10) {
                shiftSign = 1.0f;   // D#, A# 更靠右
            }  // G#=8 默认居中
            center += whiteWidth * kBlackCenterShiftRatio * shiftSign;
        }
        const float guard = whiteWidth * 0.2f;
        center = std::clamp(center, leftCenter + guard, rightCenter - guard);
        key.bounds = D2D1::RectF(center - blackHalf, blackTop,
                                 center + blackHalf, blackTop + blackHeight);
    }
}

VirtualKeyboard::Key* VirtualKeyboard::hitTest(float x, float y) {
    auto contains = [&](const Key& key) {
        return x >= key.bounds.left && x <= key.bounds.right &&
               y >= key.bounds.top && y <= key.bounds.bottom;
    };
    for (std::size_t idx : blackKeyOrder_) {
        if (idx < keys_.size() && contains(keys_[idx])) {
            return &keys_[idx];
        }
    }
    for (std::size_t idx : whiteKeyOrder_) {
        if (idx < keys_.size() && contains(keys_[idx])) {
            return &keys_[idx];
        }
    }
    return nullptr;
}

void VirtualKeyboard::triggerKey(Key* key) {
    if (!key) {
        return;
    }
    if (key->pressed) {
        return;
    }
    key->pressed = true;
    lastTriggeredLabel_ = key->label;
    lastTriggeredFrequency_ = key->frequency;
    if (callback_) {
        callback_(key->midiNote, key->frequency, true);
    }
}

void VirtualKeyboard::releaseKey(Key* key) {
    if (!key) {
        return;
    }
    if (!key->pressed) {
        return;
    }
    key->pressed = false;
    if (activeKey_ == key) {
        activeKey_ = nullptr;
    }
    if (callback_) {
        callback_(key->midiNote, key->frequency, false);
    }
}

void VirtualKeyboard::updateHoveredKey(Key* key) {
    hoveredKey_ = key;
}

double VirtualKeyboard::midiToFrequency(int midi) const {
    return 440.0 * std::pow(2.0, (midi - 69) / 12.0);
}

}  // namespace winui
