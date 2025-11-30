#pragma once

#include <windows.h>

#include <memory>
#include <string>
#include <vector>

#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

namespace winui {

class ParameterSlider;

class Direct2DContext {
public:
    Direct2DContext();
    ~Direct2DContext();

    bool initialize(HWND hwnd);
    void resize(UINT width, UINT height);
    void render();
    void handleDeviceLost();

    void setSliders(std::vector<std::shared_ptr<ParameterSlider>> sliders);
    bool onPointerDown(float x, float y);
    bool onPointerMove(float x, float y);
    void onPointerUp();

private:
    bool createDeviceResources();
    void discardDeviceResources();
    void updateSliderLayout();

    HWND hwnd_ = nullptr;
    UINT width_ = 0;
    UINT height_ = 0;
    Microsoft::WRL::ComPtr<ID2D1Factory> d2dFactory_;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> renderTarget_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> accentBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> textBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> trackBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> fillBrush_;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwriteFactory_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> textFormat_;

    std::vector<std::shared_ptr<ParameterSlider>> sliders_;
    std::shared_ptr<ParameterSlider> activeSlider_;
};

}  // namespace winui
