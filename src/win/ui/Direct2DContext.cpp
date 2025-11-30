#include "win/ui/Direct2DContext.h"

#include <cwchar>
#include <d2d1helper.h>
#include <windows.h>

namespace winui {

namespace {

const wchar_t* kInstructionText =
    L"Satori Preview\n"
    L"- 使用键盘 A~K 触发音符\n"
    L"- 即将加入旋钮/UI 控件";

}  // namespace

Direct2DContext::Direct2DContext() = default;
Direct2DContext::~Direct2DContext() = default;

bool Direct2DContext::initialize(HWND hwnd) {
    hwnd_ = hwnd;
    D2D1_FACTORY_OPTIONS options{};
#if defined(_DEBUG)
    options.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                   __uuidof(ID2D1Factory), &options,
                                   &d2dFactory_);
    if (FAILED(hr)) {
        return false;
    }
    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                             __uuidof(IDWriteFactory),
                             &dwriteFactory_);
    if (FAILED(hr)) {
        return false;
    }
    hr = dwriteFactory_->CreateTextFormat(
        L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_LIGHT,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 18.0f, L"zh-CN",
        &textFormat_);
    if (FAILED(hr)) {
        return false;
    }
    textFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    textFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    return createDeviceResources();
}

void Direct2DContext::resize(UINT width, UINT height) {
    if (renderTarget_) {
        renderTarget_->Resize(D2D1::SizeU(width, height));
    }
}

void Direct2DContext::handleDeviceLost() {
    discardDeviceResources();
    createDeviceResources();
}

bool Direct2DContext::createDeviceResources() {
    if (renderTarget_) {
        return true;
    }
    RECT rc{};
    if (!GetClientRect(hwnd_, &rc)) {
        return false;
    }
    const auto size =
        D2D1::SizeU(static_cast<UINT>(rc.right - rc.left),
                    static_cast<UINT>(rc.bottom - rc.top));

    HRESULT hr = d2dFactory_->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(),
        D2D1::HwndRenderTargetProperties(hwnd_, size),
        &renderTarget_);
    if (FAILED(hr)) {
        return false;
    }

    hr = renderTarget_->CreateSolidColorBrush(
        D2D1::ColorF(0.4f, 0.7f, 0.9f, 1.0f), &accentBrush_);
    if (FAILED(hr)) {
        return false;
    }
    return true;
}

void Direct2DContext::discardDeviceResources() {
    accentBrush_.Reset();
    renderTarget_.Reset();
}

void Direct2DContext::render() {
    if (!createDeviceResources()) {
        return;
    }
    renderTarget_->BeginDraw();
    renderTarget_->SetTransform(D2D1::Matrix3x2F::Identity());
    renderTarget_->Clear(D2D1::ColorF(0.07f, 0.07f, 0.09f));

    if (accentBrush_ && textFormat_) {
        RECT rc{};
        GetClientRect(hwnd_, &rc);
        const D2D1_RECT_F layoutRect =
            D2D1::RectF(static_cast<FLOAT>(rc.left) + 20.0f,
                        static_cast<FLOAT>(rc.top) + 20.0f,
                        static_cast<FLOAT>(rc.right) - 20.0f,
                        static_cast<FLOAT>(rc.bottom) - 20.0f);
        renderTarget_->DrawText(
            kInstructionText, static_cast<UINT32>(wcslen(kInstructionText)),
            textFormat_.Get(), layoutRect, accentBrush_.Get());
    }

    HRESULT hr = renderTarget_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        handleDeviceLost();
    }
}

}  // namespace winui
