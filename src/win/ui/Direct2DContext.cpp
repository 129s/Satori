#include "win/ui/Direct2DContext.h"

#include <algorithm>
#include <cwchar>
#include <d2d1helper.h>
#include <windows.h>

#include "win/ui/ParameterSlider.h"
#include "win/ui/VirtualKeyboard.h"
#include "win/ui/WaveformView.h"

namespace winui {

namespace {

const wchar_t* kInstructionText =
    L"Satori Preview\n"
    L"- 使用键盘 A~K 或下方虚拟键触发音符\n"
    L"- 利用滑块/预设调节音色\n"
    L"- 波形视图展示最近音符的包络";

}  // namespace

Direct2DContext::Direct2DContext() = default;
Direct2DContext::~Direct2DContext() = default;

bool Direct2DContext::initialize(HWND hwnd) {
    hwnd_ = hwnd;
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    width_ = static_cast<UINT>(rc.right - rc.left);
    height_ = static_cast<UINT>(rc.bottom - rc.top);
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
    waveformView_ = std::make_shared<WaveformView>();
    keyboard_ = std::make_shared<VirtualKeyboard>();
    return createDeviceResources();
}

void Direct2DContext::resize(UINT width, UINT height) {
    width_ = width;
    height_ = height;
    if (renderTarget_) {
        renderTarget_->Resize(D2D1::SizeU(width, height));
    }
    updateLayout();
}

void Direct2DContext::handleDeviceLost() {
    discardDeviceResources();
    createDeviceResources();
    updateLayout();
}

void Direct2DContext::setSliders(
    std::vector<std::shared_ptr<ParameterSlider>> sliders) {
    sliders_ = std::move(sliders);
    updateLayout();
}

void Direct2DContext::setWaveformSamples(const std::vector<float>& samples) {
    if (!waveformView_) {
        waveformView_ = std::make_shared<WaveformView>();
    }
    waveformView_->setSamples(samples);
}

void Direct2DContext::setKeyboardCallback(std::function<void(double)> callback) {
    if (!keyboard_) {
        keyboard_ = std::make_shared<VirtualKeyboard>();
    }
    keyboard_->setCallback(std::move(callback));
}

void Direct2DContext::setPresetCallbacks(std::function<void()> onLoad,
                                         std::function<void()> onSave) {
    buttons_.clear();
    if (onLoad) {
        buttons_.push_back(Button{L"加载预设", {}, std::move(onLoad), false});
    }
    if (onSave) {
        buttons_.push_back(Button{L"保存预设", {}, std::move(onSave), false});
    }
    updateLayout();
}

void Direct2DContext::setStatusText(std::wstring status) {
    statusText_ = std::move(status);
}

void Direct2DContext::setKeyboardKeys(
    const std::vector<std::pair<std::wstring, double>>& keys) {
    if (!keyboard_) {
        keyboard_ = std::make_shared<VirtualKeyboard>();
    }
    keyboard_->setKeys(keys);
    updateLayout();
}

bool Direct2DContext::onPointerDown(float x, float y) {
    for (auto& slider : sliders_) {
        if (slider->onPointerDown(x, y)) {
            activeSlider_ = slider;
            return true;
        }
    }
    if (keyboard_ && keyboard_->onPointerDown(x, y)) {
        return true;
    }
    if (hitButton(x, y)) {
        return true;
    }
    return false;
}

bool Direct2DContext::onPointerMove(float x, float y) {
    if (activeSlider_) {
        return activeSlider_->onPointerMove(x, y);
    }
    if (keyboard_ && keyboard_->onPointerMove(x, y)) {
        return true;
    }
    if (activeButton_) {
        const bool inside = x >= activeButton_->bounds.left &&
                            x <= activeButton_->bounds.right &&
                            y >= activeButton_->bounds.top &&
                            y <= activeButton_->bounds.bottom;
        activeButton_->pressed = inside;
        return inside;
    }
    return false;
}

void Direct2DContext::onPointerUp() {
    if (activeSlider_) {
        activeSlider_->onPointerUp();
        activeSlider_.reset();
    }
    if (keyboard_) {
        keyboard_->onPointerUp();
    }
    if (activeButton_) {
        const bool execute = activeButton_->pressed;
        activeButton_->pressed = false;
        auto callback = activeButton_->onClick;
        activeButton_ = nullptr;
        if (execute && callback) {
            callback();
        }
    }
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
    hr = renderTarget_->CreateSolidColorBrush(D2D1::ColorF(0.9f, 0.9f, 0.95f, 1.0f),
                                              &textBrush_);
    if (FAILED(hr)) {
        return false;
    }
    hr = renderTarget_->CreateSolidColorBrush(D2D1::ColorF(0.15f, 0.15f, 0.2f, 1.0f),
                                              &trackBrush_);
    if (FAILED(hr)) {
        return false;
    }
    hr = renderTarget_->CreateSolidColorBrush(D2D1::ColorF(0.25f, 0.55f, 0.75f, 1.0f),
                                              &fillBrush_);
    if (FAILED(hr)) {
        return false;
    }
    hr = renderTarget_->CreateSolidColorBrush(D2D1::ColorF(0.12f, 0.15f, 0.2f, 1.0f),
                                              &panelBrush_);
    if (FAILED(hr)) {
        return false;
    }
    hr = renderTarget_->CreateSolidColorBrush(D2D1::ColorF(0.35f, 0.4f, 0.45f, 1.0f),
                                              &gridBrush_);
    if (FAILED(hr)) {
        return false;
    }
    return true;
}

void Direct2DContext::discardDeviceResources() {
    accentBrush_.Reset();
    textBrush_.Reset();
    trackBrush_.Reset();
    fillBrush_.Reset();
    panelBrush_.Reset();
    gridBrush_.Reset();
    renderTarget_.Reset();
}

void Direct2DContext::render() {
    if (!createDeviceResources()) {
        return;
    }
    renderTarget_->BeginDraw();
    renderTarget_->SetTransform(D2D1::Matrix3x2F::Identity());
    renderTarget_->Clear(D2D1::ColorF(0.07f, 0.07f, 0.09f));

    if (panelBrush_) {
        RECT rc{};
        GetClientRect(hwnd_, &rc);
        const auto bgRect =
            D2D1::RectF(static_cast<float>(rc.left) + 12.0f,
                        static_cast<float>(rc.top) + 12.0f,
                        static_cast<float>(rc.right) - 12.0f,
                        static_cast<float>(rc.bottom) - 12.0f);
        renderTarget_->FillRectangle(bgRect, panelBrush_.Get());
    }

    if (textBrush_ && textFormat_) {
        RECT rc{};
        GetClientRect(hwnd_, &rc);
        const float margin = 24.0f;
        const float instructionWidth = 360.0f;
        const float instructionHeight = 120.0f;
        const D2D1_RECT_F layoutRect =
            D2D1::RectF(static_cast<FLOAT>(rc.left) + margin,
                        static_cast<FLOAT>(rc.top) + margin,
                        static_cast<FLOAT>(rc.left) + margin + instructionWidth,
                        static_cast<FLOAT>(rc.top) + margin + instructionHeight);
        renderTarget_->DrawText(
            kInstructionText, static_cast<UINT32>(wcslen(kInstructionText)),
            textFormat_.Get(), layoutRect, textBrush_.Get());
        if (!statusText_.empty()) {
            const D2D1_RECT_F statusRect =
                D2D1::RectF(static_cast<FLOAT>(rc.right) - margin - instructionWidth,
                            static_cast<FLOAT>(rc.top) + margin,
                            static_cast<FLOAT>(rc.right) - margin,
                            static_cast<FLOAT>(rc.top) + margin + 96.0f);
            renderTarget_->DrawText(statusText_.c_str(),
                                    static_cast<UINT32>(statusText_.size()),
                                    textFormat_.Get(), statusRect,
                                    textBrush_.Get());
        }
    }

    if (!buttons_.empty() && textFormat_ && fillBrush_ && accentBrush_) {
        for (const auto& button : buttons_) {
            auto* brush = button.pressed ? accentBrush_.Get() : fillBrush_.Get();
            renderTarget_->FillRectangle(button.bounds, brush);
            renderTarget_->DrawRectangle(button.bounds, accentBrush_.Get(), 1.5f);
            renderTarget_->DrawText(
                button.label.c_str(), static_cast<UINT32>(button.label.size()),
                textFormat_.Get(), button.bounds, textBrush_.Get());
        }
    }

    if (waveformView_ && panelBrush_ && gridBrush_ && accentBrush_) {
        waveformView_->draw(renderTarget_.Get(), panelBrush_.Get(), gridBrush_.Get(),
                            accentBrush_.Get());
    }

    if (!sliders_.empty() && trackBrush_ && fillBrush_ && accentBrush_ &&
        textBrush_ && textFormat_) {
        for (const auto& slider : sliders_) {
            slider->draw(renderTarget_.Get(), trackBrush_.Get(), fillBrush_.Get(),
                         accentBrush_.Get(), textBrush_.Get(), textFormat_.Get());
        }
    }

    if (keyboard_ && accentBrush_ && textFormat_ && panelBrush_) {
        keyboard_->draw(renderTarget_.Get(), accentBrush_.Get(), panelBrush_.Get(),
                        fillBrush_.Get(), textFormat_.Get());
    }

    HRESULT hr = renderTarget_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        handleDeviceLost();
    }
}

void Direct2DContext::updateLayout() {
    if (width_ == 0 || height_ == 0) {
        return;
    }
    if (!waveformView_) {
        waveformView_ = std::make_shared<WaveformView>();
    }
    if (!keyboard_) {
        keyboard_ = std::make_shared<VirtualKeyboard>();
    }
    const float margin = 40.0f;
    const float instructionHeight = 120.0f;
    const float availableWidth = static_cast<float>(width_) - 2.0f * margin;
    const float buttonHeight = 36.0f;

    float buttonX = margin;
    const float buttonTop = margin + instructionHeight + 12.0f;
    for (auto& button : buttons_) {
        button.bounds = D2D1::RectF(buttonX, buttonTop, buttonX + 120.0f,
                                    buttonTop + buttonHeight);
        buttonX += 132.0f;
    }

    const float keyboardHeight = 110.0f;
    const float keyboardTop =
        static_cast<float>(height_) - margin - keyboardHeight;
    keyboard_->setBounds(D2D1::RectF(margin, keyboardTop, margin + availableWidth,
                                     keyboardTop + keyboardHeight));

    const float contentTop = buttonTop + buttonHeight + 20.0f;
    float contentBottom = keyboardTop - 20.0f;
    if (contentBottom <= contentTop + 40.0f) {
        contentBottom = contentTop + 40.0f;
    }
    const float contentHeight = contentBottom - contentTop;

    float waveformHeight = std::max(100.0f, contentHeight * 0.6f);
    float sliderAreaHeight = contentHeight - waveformHeight;
    if (sliderAreaHeight < 80.0f) {
        sliderAreaHeight = 80.0f;
        waveformHeight = std::max(60.0f, contentHeight - sliderAreaHeight);
    }
    if (waveformHeight < 80.0f) {
        waveformHeight = 80.0f;
        sliderAreaHeight = std::max(40.0f, contentHeight - waveformHeight);
    }
    const float waveformTop = contentTop;
    const float waveformBottom =
        std::min(contentBottom - sliderAreaHeight, waveformTop + waveformHeight);
    waveformView_->setBounds(
        D2D1::RectF(margin, waveformTop, margin + availableWidth, waveformBottom));

    float sliderAreaTop = waveformBottom + 20.0f;
    float sliderAreaBottom = contentBottom;
    if (sliderAreaBottom - sliderAreaTop < 40.0f) {
        sliderAreaTop = sliderAreaBottom - 40.0f;
    }
    float sliderAreaHeightFinal = sliderAreaBottom - sliderAreaTop;

    if (!sliders_.empty()) {
        const float spacing = 16.0f;
        const std::size_t sliderCount = sliders_.size();
        const float totalSpacing =
            spacing * static_cast<float>(sliderCount > 0 ? sliderCount - 1 : 0);
        const float eachHeight =
            std::max(40.0f,
                     (sliderAreaHeightFinal - totalSpacing) /
                         static_cast<float>(std::max<std::size_t>(1, sliderCount)));
        float y = sliderAreaTop;
        for (auto& slider : sliders_) {
            if (!slider) {
                continue;
            }
            if (y >= sliderAreaBottom) {
                break;
            }
            const float bottom = std::min(y + eachHeight, sliderAreaBottom);
            slider->setBounds(
                D2D1::RectF(margin, y, margin + availableWidth, bottom));
            y = bottom + spacing;
        }
    }

    if (keyboard_) {
        keyboard_->layoutKeys();
    }
}

bool Direct2DContext::hitButton(float x, float y) {
    for (auto& button : buttons_) {
        if (x >= button.bounds.left && x <= button.bounds.right &&
            y >= button.bounds.top && y <= button.bounds.bottom) {
            button.pressed = true;
            activeButton_ = &button;
            return true;
        }
    }
    return false;
}

}  // namespace winui
