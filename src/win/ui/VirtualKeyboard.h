#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <d2d1.h>
#include <dwrite.h>

namespace winui {

struct KeyboardColors {
    ID2D1SolidColorBrush* whiteFill = nullptr;
    ID2D1SolidColorBrush* whitePressed = nullptr;
    ID2D1SolidColorBrush* whiteBorder = nullptr;
    ID2D1SolidColorBrush* whiteText = nullptr;

    ID2D1SolidColorBrush* blackFill = nullptr;
    ID2D1SolidColorBrush* blackPressed = nullptr;
    ID2D1SolidColorBrush* blackBorder = nullptr;
    ID2D1SolidColorBrush* blackText = nullptr;

    ID2D1SolidColorBrush* hoverOutline = nullptr;
};

class VirtualKeyboard {
public:
    using Callback = std::function<void(double)>;

    void setBounds(const D2D1_RECT_F& bounds);
    void setCallback(Callback callback);
    void setKeys(const std::vector<std::pair<std::wstring, double>>& keys);
    void setPianoLayout(int baseMidiNote, int octaveCount);
    void setShowLabels(bool enabled);
    void setHoverOutline(bool enabled);
    void layoutKeys();

    void draw(ID2D1HwndRenderTarget* target,
              ID2D1SolidColorBrush* borderBrush,
              ID2D1SolidColorBrush* fillBrush,
              ID2D1SolidColorBrush* activeBrush,
              IDWriteTextFormat* textFormat) const;
    void draw(ID2D1HwndRenderTarget* target,
              const KeyboardColors& colors,
              IDWriteTextFormat* textFormat) const;

    bool onPointerDown(float x, float y);
    bool onPointerMove(float x, float y);
    void onPointerUp();
    bool pressKeyByMidi(int midiNote);
    void releaseKeyByMidi(int midiNote);
    void releaseAllKeys();

    bool focusedKeyBounds(D2D1_RECT_F& outBounds) const;
    const std::wstring& lastTriggeredLabel() const { return lastTriggeredLabel_; }
    double lastTriggeredFrequency() const { return lastTriggeredFrequency_; }

private:
    struct Key {
        std::wstring label;
        double frequency = 0.0;
        D2D1_RECT_F bounds{};
        bool pressed = false;
        bool hovered = false;
        bool isBlack = false;
        int midiNote = -1;
        int noteIndex = -1;
        int whiteSlot = -1;
        int leftWhiteSlot = -1;
        int rightWhiteSlot = -1;
    };

    enum class LayoutMode { kLinear, kPiano };

    void buildLinearKeys(const std::vector<std::pair<std::wstring, double>>& keys);
    void buildPianoKeys(int baseMidiNote, int octaveCount);
    void layoutLinear();
    void layoutPiano();
    Key* hitTest(float x, float y);
    void triggerKey(Key* key);
    void releaseKey(Key* key);
    void updateHoveredKey(Key* key);
    double midiToFrequency(int midi) const;

    D2D1_RECT_F bounds_{};
    std::vector<Key> keys_;
    Callback callback_;
    Key* activeKey_ = nullptr;
    Key* hoveredKey_ = nullptr;
    bool dragging_ = false;
    LayoutMode layoutMode_ = LayoutMode::kLinear;
    int baseMidiNote_ = 60;
    int octaveCount_ = 0;
    int totalWhiteKeys_ = 0;
    std::vector<std::size_t> whiteKeyOrder_;
    std::vector<std::size_t> blackKeyOrder_;
    std::unordered_map<int, std::size_t> midiToKeyIndex_;
    std::wstring lastTriggeredLabel_;
    double lastTriggeredFrequency_ = 0.0;
    bool showLabels_ = true;
    bool showHoverOutline_ = true;
};

}  // namespace winui
