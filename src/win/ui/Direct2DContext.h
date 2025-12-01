#pragma once

#include <windows.h>

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

namespace winui {

class ParameterSlider;
class WaveformView;
class VirtualKeyboard;

class Direct2DContext {
public:
    Direct2DContext();
    ~Direct2DContext();

    bool initialize(HWND hwnd);
    void resize(UINT width, UINT height);
    void render();
    void handleDeviceLost();

    void setSliders(std::vector<std::shared_ptr<ParameterSlider>> sliders);
    void setWaveformSamples(const std::vector<float>& samples);
    void setKeyboardCallback(std::function<void(double)> callback);
    void setPresetCallbacks(std::function<void()> onLoad,
                            std::function<void()> onSave);
    void setKeyboardKeys(const std::vector<std::pair<std::wstring, double>>& keys);
    void setStatusText(std::wstring status);
    bool onPointerDown(float x, float y);
    bool onPointerMove(float x, float y);
    void onPointerUp();

private:
    bool createDeviceResources();
    void discardDeviceResources();
    void updateLayout();
    bool hitButton(float x, float y);

    HWND hwnd_ = nullptr;
    UINT width_ = 0;
    UINT height_ = 0;
    Microsoft::WRL::ComPtr<ID2D1Factory> d2dFactory_;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> renderTarget_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> accentBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> textBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> trackBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> fillBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> panelBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> gridBrush_;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwriteFactory_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> textFormat_;

    std::vector<std::shared_ptr<ParameterSlider>> sliders_;
    std::shared_ptr<ParameterSlider> activeSlider_;
    std::shared_ptr<WaveformView> waveformView_;
    std::shared_ptr<VirtualKeyboard> keyboard_;

    struct Button {
        std::wstring label;
        D2D1_RECT_F bounds{};
        std::function<void()> onClick;
        bool pressed = false;
    };
    std::vector<Button> buttons_;
    Button* activeButton_ = nullptr;
    std::wstring statusText_;
};

}  // namespace winui
