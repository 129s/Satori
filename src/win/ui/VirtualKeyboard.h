#pragma once

#include <functional>
#include <string>
#include <vector>

#include <d2d1.h>
#include <dwrite.h>

namespace winui {

class VirtualKeyboard {
public:
    using Callback = std::function<void(double)>;

    void setBounds(const D2D1_RECT_F& bounds);
    void setCallback(Callback callback);
    void setKeys(const std::vector<std::pair<std::wstring, double>>& keys);
    void layoutKeys();

    void draw(ID2D1HwndRenderTarget* target,
              ID2D1SolidColorBrush* borderBrush,
              ID2D1SolidColorBrush* fillBrush,
              ID2D1SolidColorBrush* activeBrush,
              IDWriteTextFormat* textFormat) const;

    bool onPointerDown(float x, float y);
    bool onPointerMove(float x, float y);
    void onPointerUp();

private:
    struct Key {
        std::wstring label;
        double frequency = 0.0;
        D2D1_RECT_F bounds{};
        bool pressed = false;
    };

    Key* hitTest(float x, float y);
    void triggerKey(Key* key);

    D2D1_RECT_F bounds_{};
    std::vector<Key> keys_;
    Callback callback_;
    Key* activeKey_ = nullptr;
    bool dragging_ = false;
};

}  // namespace winui
