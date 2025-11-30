#pragma once

#include <windows.h>

#include <string>

#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

namespace winui {

class Direct2DContext {
public:
    Direct2DContext();
    ~Direct2DContext();

    bool initialize(HWND hwnd);
    void resize(UINT width, UINT height);
    void render();
    void handleDeviceLost();

private:
    bool createDeviceResources();
    void discardDeviceResources();

    HWND hwnd_ = nullptr;
    Microsoft::WRL::ComPtr<ID2D1Factory> d2dFactory_;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> renderTarget_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> accentBrush_;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwriteFactory_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> textFormat_;
};

}  // namespace winui
