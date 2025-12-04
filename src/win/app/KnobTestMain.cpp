#include <windows.h>
#include <windowsx.h>

#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <dwrite_3.h>
#include <wrl/client.h>

#include <cmath>
#include <optional>
#include <vector>

#include <algorithm>
#include <memory>
#include <string>

#include "win/ui/D2DHelpers.h"
#include "win/ui/DebugOverlay.h"
#include "win/ui/NunitoFont.h"
#include "win/ui/ParameterKnob.h"

// 测试程序：仅在窗口中央绘制一个 ParameterKnob，用于独立观察 UI/UX。

using Microsoft::WRL::ComPtr;

namespace {

const wchar_t kWindowClassName[] = L"SatoriKnobTestClass";
const wchar_t kWindowTitle[] = L"Satori Knob Test";

HWND g_mainWindow = nullptr;

ComPtr<ID2D1Factory> g_d2dFactory;
ComPtr<IDWriteFactory> g_dwriteFactory;
ComPtr<ID2D1HwndRenderTarget> g_renderTarget;
ComPtr<ID2D1SolidColorBrush> g_backgroundBrush;
ComPtr<ID2D1SolidColorBrush> g_baseBrush;
ComPtr<ID2D1SolidColorBrush> g_fillBrush;
ComPtr<ID2D1SolidColorBrush> g_accentBrush;
ComPtr<ID2D1SolidColorBrush> g_textBrush;
ComPtr<IDWriteTextFormat> g_textFormat;
ComPtr<IDWriteFontCollection1> g_nunitoFontCollection;

const winui::DebugOverlayPalette kSandboxOverlayPalette =
    winui::MakeUnifiedDebugOverlayPalette();

std::unique_ptr<winui::ParameterKnob> g_knob;
winui::DebugOverlayMode g_debugOverlayMode = winui::DebugOverlayMode::kOff;
winui::DebugBoxRenderer g_debugRenderer;
std::optional<winui::DebugBoxModel> g_hoverDebugModel;
bool g_trackingMouseLeave = false;
bool g_hasPointerPosition = false;
float g_lastPointerX = 0.0f;
float g_lastPointerY = 0.0f;

bool DebugModelsEqual(const winui::DebugBoxModel& lhs,
                      const winui::DebugBoxModel& rhs) {
    if (lhs.segments.size() != rhs.segments.size()) {
        return false;
    }
    const float kTolerance = 0.25f;
    auto rectClose = [&](const D2D1_RECT_F& a, const D2D1_RECT_F& b) {
        return std::fabs(a.left - b.left) < kTolerance &&
               std::fabs(a.top - b.top) < kTolerance &&
               std::fabs(a.right - b.right) < kTolerance &&
               std::fabs(a.bottom - b.bottom) < kTolerance;
    };
    for (std::size_t i = 0; i < lhs.segments.size(); ++i) {
        const auto& a = lhs.segments[i];
        const auto& b = rhs.segments[i];
        if (a.layer != b.layer || !rectClose(a.rect, b.rect)) {
            return false;
        }
    }
    return true;
}

bool UpdateHoverDebugSelection(float x, float y) {
    if (!g_knob || g_debugOverlayMode != winui::DebugOverlayMode::kBoxModel) {
        if (g_hoverDebugModel) {
            g_hoverDebugModel.reset();
            return true;
        }
        return false;
    }
    winui::DebugBoxModel next{};
    bool shouldHighlight = false;
    if (g_knob->isDragging() || g_knob->contains(x, y)) {
        next = g_knob->debugBoxModel();
        shouldHighlight = !next.segments.empty();
    }
    if (!shouldHighlight) {
        if (g_hoverDebugModel) {
            g_hoverDebugModel.reset();
            return true;
        }
        return false;
    }
    if (g_hoverDebugModel && DebugModelsEqual(*g_hoverDebugModel, next)) {
        return false;
    }
    g_hoverDebugModel = std::move(next);
    return true;
}

bool ClearHoverDebugSelection() {
    if (!g_hoverDebugModel) {
        return false;
    }
    g_hoverDebugModel.reset();
    return true;
}

IDWriteFontCollection* EnsureNunitoFontCollection() {
    if (!g_dwriteFactory) {
        return nullptr;
    }
    if (!g_nunitoFontCollection) {
        if (auto collection =
                winui::CreateNunitoFontCollection(g_dwriteFactory.Get())) {
            g_nunitoFontCollection = collection;
        }
    }
    return g_nunitoFontCollection.Get();
}

// 根据当前旋钮外框尺寸估算圆盘半径，并动态调整文本字号，使圆盘:文字≈3:1。
static void UpdateTextFormatForBounds(float knobBoxSize) {
    if (!g_dwriteFactory) {
        return;  // DWrite factory not ready
    }
    // Keep constants consistent with ParameterKnob::draw
    const float outerPaddingX = 8.0f;
    const float sideMargin = 4.0f;

    const float contentWidth = knobBoxSize - outerPaddingX * 2.0f;
    if (contentWidth <= 0.0f) {
        return;
    }
    const float outerRadius = contentWidth * 0.5f - sideMargin;
    if (outerRadius <= 0.0f) {
        return;
    }
    const float radius = outerRadius * (5.0f / 8.0f);  // ratio 5:1:2

    // Target: circle:text ≈ 3:1
    const float targetLineHeight = (2.0f * radius) / 3.0f;

    // Fit TextLayout to target single-line height/baseline
    float fontSize = std::clamp(targetLineHeight, 12.0f, 96.0f);
    const bool nunitoAvailable = winui::EnsureNunitoFontLoaded();
    IDWriteFontCollection* fontCollection = EnsureNunitoFontCollection();
    if (!nunitoAvailable && !fontCollection) {
        return;
    }
    { Microsoft::WRL::ComPtr<IDWriteFontCollection> coll; g_dwriteFactory->GetSystemFontCollection(&coll, TRUE); }
    for (int i = 0; i < 4; ++i) {
        Microsoft::WRL::ComPtr<IDWriteTextFormat> tf;
        HRESULT hr = g_dwriteFactory->CreateTextFormat(
            winui::NunitoFontFamily(), fontCollection, DWRITE_FONT_WEIGHT_REGULAR,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, fontSize,
            L"zh-CN", &tf);
        if (FAILED(hr) || !tf) {
            break;
        }
        winui::ApplyChineseFontFallback(g_dwriteFactory.Get(), tf.Get());
        tf->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        tf->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        tf->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        DWRITE_TRIMMING trimming{};
        trimming.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER;
        Microsoft::WRL::ComPtr<IDWriteInlineObject> ellipsis;
        if (SUCCEEDED(g_dwriteFactory->CreateEllipsisTrimmingSign(tf.Get(), &ellipsis))) {
            tf->SetTrimming(&trimming, ellipsis.Get());
        }

        Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
        hr = g_dwriteFactory->CreateTextLayout(L"Hg", 2, tf.Get(), 10000.0f, 10000.0f, &layout);
        if (FAILED(hr) || !layout) {
            break;
        }
        layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        if (ellipsis) {
            layout->SetTrimming(&trimming, ellipsis.Get());
        }

        DWRITE_TEXT_METRICS m{};
        hr = layout->GetMetrics(&m);
        if (FAILED(hr)) {
            break;
        }
        const float measured = m.height;  // line height
        if (measured <= 0.0f) {
            break;
        }
        const float scale = targetLineHeight / measured;
        const float next = std::clamp(fontSize * scale, 10.0f, 128.0f);
        // Iterate to converge
        if (std::fabs(next - fontSize) < 0.5f) {
            fontSize = next;

            // Set baseline using layout metrics
            UINT32 lineCount = 0;
            layout->GetLineMetrics(nullptr, 0, &lineCount);
            float baselineRatio = 0.8f;
            if (lineCount > 0) {
                std::vector<DWRITE_LINE_METRICS> lines(lineCount);
                if (SUCCEEDED(layout->GetLineMetrics(lines.data(), lineCount, &lineCount)) && m.height > 0.0f) {
                    baselineRatio = lines[0].baseline / m.height;
                }
            }
            const float baseline = targetLineHeight * baselineRatio;
            tf->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, targetLineHeight, baseline);

            g_textFormat = std::move(tf);
            break;
        }
        fontSize = next;
        if (i == 3) {
            UINT32 lineCount = 0;
            layout->GetLineMetrics(nullptr, 0, &lineCount);
            float baselineRatio = 0.8f;
            if (lineCount > 0) {
                std::vector<DWRITE_LINE_METRICS> lines(lineCount);
                if (SUCCEEDED(layout->GetLineMetrics(lines.data(), lineCount, &lineCount)) && m.height > 0.0f) {
                    baselineRatio = lines[0].baseline / m.height;
                }
            }
            const float baseline = targetLineHeight * baselineRatio;
            tf->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, targetLineHeight, baseline);
            g_textFormat = std::move(tf);
        }
    }
}
void LayoutKnob(float width, float height) {
    if (!g_knob) {
        return;
    }
    // 保证旋钮始终在窗口中央，占用较大但不溢出的区域。
    const float size = static_cast<float>(std::min(width, height)) * 0.55f;
    const float left = (width - size) * 0.5f;
    const float top = (height - size) * 0.5f;
    const auto bounds =
        D2D1::RectF(left, top, left + size, top + size);
    g_knob->setBounds(bounds);

    // 根据当前外框尺寸动态更新文本字号，达成圆盘:文字≈3:1。
    UpdateTextFormatForBounds(size);
}

HRESULT CreateDeviceIndependentResources() {
    D2D1_FACTORY_OPTIONS options{};
#if defined(_DEBUG)
    options.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                   __uuidof(ID2D1Factory), &options,
                                   &g_d2dFactory);
    if (FAILED(hr)) {
        return hr;
    }

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                             __uuidof(IDWriteFactory),
                             &g_dwriteFactory);
    if (FAILED(hr)) {
        return hr;
    }

    // 文本格式：使用 Nunito 私有字体
    const bool nunitoAvailable = winui::EnsureNunitoFontLoaded();
    g_nunitoFontCollection = winui::CreateNunitoFontCollection(g_dwriteFactory.Get());
    if (!nunitoAvailable && !g_nunitoFontCollection) {
        return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    }
    { Microsoft::WRL::ComPtr<IDWriteFontCollection> coll; g_dwriteFactory->GetSystemFontCollection(&coll, TRUE); }
    hr = g_dwriteFactory->CreateTextFormat(winui::NunitoFontFamily(), g_nunitoFontCollection.Get(), DWRITE_FONT_WEIGHT_REGULAR,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 18.0f,
        L"zh-CN", &g_textFormat);
    if (FAILED(hr)) {
        return hr;
    }
    winui::ApplyChineseFontFallback(g_dwriteFactory.Get(), g_textFormat.Get());
    g_textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    g_textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    return S_OK;
}

HRESULT CreateDeviceResources(HWND hwnd) {
    if (g_renderTarget) {
        return S_OK;
    }
    if (!g_d2dFactory) {
        const HRESULT hr = CreateDeviceIndependentResources();
        if (FAILED(hr)) {
            return hr;
        }
    }

    RECT rc{};
    GetClientRect(hwnd, &rc);
    const UINT width = static_cast<UINT>(rc.right - rc.left);
    const UINT height = static_cast<UINT>(rc.bottom - rc.top);

    const D2D1_SIZE_U size = D2D1::SizeU(width, height);
    HRESULT hr = g_d2dFactory->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(),
        D2D1::HwndRenderTargetProperties(hwnd, size),
        &g_renderTarget);
    if (FAILED(hr)) {
        return hr;
    }

    // 背景与旋钮配色偏向与主程序一致，便于对比体验。
    hr = g_renderTarget->CreateSolidColorBrush(
        D2D1::ColorF(0.05f, 0.06f, 0.08f, 1.0f),
        &g_backgroundBrush);
    if (FAILED(hr)) {
        return hr;
    }
    hr = g_renderTarget->CreateSolidColorBrush(
        D2D1::ColorF(0.15f, 0.18f, 0.24f, 1.0f),
        &g_baseBrush);
    if (FAILED(hr)) {
        return hr;
    }
    hr = g_renderTarget->CreateSolidColorBrush(
        D2D1::ColorF(0.28f, 0.56f, 0.86f, 1.0f),
        &g_fillBrush);
    if (FAILED(hr)) {
        return hr;
    }
    hr = g_renderTarget->CreateSolidColorBrush(
        D2D1::ColorF(0.95f, 0.98f, 1.0f, 1.0f),
        &g_accentBrush);
    if (FAILED(hr)) {
        return hr;
    }
    hr = g_renderTarget->CreateSolidColorBrush(
        D2D1::ColorF(0.92f, 0.94f, 0.98f, 1.0f),
        &g_textBrush);
    if (FAILED(hr)) {
        return hr;
    }

    g_debugRenderer.setPalette(kSandboxOverlayPalette);

    LayoutKnob(static_cast<float>(width), static_cast<float>(height));
    return S_OK;
}

void DiscardDeviceResources() {
    g_renderTarget.Reset();
    g_backgroundBrush.Reset();
    g_baseBrush.Reset();
    g_fillBrush.Reset();
    g_accentBrush.Reset();
    g_textBrush.Reset();
    g_debugRenderer.setPalette(kSandboxOverlayPalette);
}

void OnResize(UINT width, UINT height) {
    if (g_renderTarget) {
        g_renderTarget->Resize(D2D1::SizeU(width, height));
    }
    LayoutKnob(static_cast<float>(width), static_cast<float>(height));
}

void OnRender() {
    if (!g_mainWindow) {
        return;
    }
    if (FAILED(CreateDeviceResources(g_mainWindow))) {
        return;
    }
    if (!g_renderTarget || !g_knob || !g_textFormat) {
        return;
    }

    const auto size = g_renderTarget->GetSize();
    LayoutKnob(size.width, size.height);

    g_renderTarget->BeginDraw();
    g_renderTarget->SetTransform(D2D1::Matrix3x2F::Identity());

    // 背景整体填充为深色，突出中间的旋钮。
    g_renderTarget->Clear(D2D1::ColorF(0.03f, 0.03f, 0.05f, 1.0f));
    if (g_backgroundBrush) {
        const auto rect = D2D1::RectF(
            0.0f, 0.0f, size.width, size.height);
        g_renderTarget->FillRectangle(rect, g_backgroundBrush.Get());
    }

    g_knob->draw(g_renderTarget.Get(), g_baseBrush.Get(), g_fillBrush.Get(),
                 g_accentBrush.Get(), g_textBrush.Get(), g_textFormat.Get());
    if (g_debugOverlayMode == winui::DebugOverlayMode::kBoxModel &&
        g_hoverDebugModel) {
        winui::ScopedAntialiasMode scoped(g_renderTarget.Get(),
                                          D2D1_ANTIALIAS_MODE_ALIASED);
        g_debugRenderer.render(g_renderTarget.Get(), *g_hoverDebugModel, true);
    }

    const HRESULT hr = g_renderTarget->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        DiscardDeviceResources();
    }
}

void EnsureKnob() {
    if (g_knob) {
        return;
    }
    // 这里的回调仅用于调试：滚动时将数值打印到调试输出。
    g_knob = std::make_unique<winui::ParameterKnob>(
        L"Test Knob", 0.0f, 1.0f, 0.5f,
        [](float value) {
            wchar_t buffer[64];
            swprintf_s(buffer, L"Knob value: %.3f\n", value);
            OutputDebugStringW(buffer);
        });
}

LRESULT CALLBACK WindowProc(HWND hwnd,
                            UINT msg,
                            WPARAM wparam,
                            LPARAM lparam) {
    switch (msg) {
        case WM_CREATE: {
            g_mainWindow = hwnd;
            EnsureKnob();
            RECT rc{};
            GetClientRect(hwnd, &rc);
            LayoutKnob(static_cast<float>(rc.right - rc.left),
                       static_cast<float>(rc.bottom - rc.top));
            return 0;
        }
        case WM_SIZE: {
            const UINT width = LOWORD(lparam);
            const UINT height = HIWORD(lparam);
            OnResize(width, height);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            OnRender();
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            EnsureKnob();
            const float x = static_cast<float>(GET_X_LPARAM(lparam));
            const float y = static_cast<float>(GET_Y_LPARAM(lparam));
            g_hasPointerPosition = true;
            g_lastPointerX = x;
            g_lastPointerY = y;
            bool handled = g_knob && g_knob->onPointerDown(x, y);
            bool selectionChanged = UpdateHoverDebugSelection(x, y);
            if (handled) {
                SetCapture(hwnd);
            }
            if (handled || selectionChanged) {
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            break;
        }
        case WM_MOUSEMOVE: {
            if (!g_knob) {
                break;
            }
            if (!g_trackingMouseLeave) {
                TRACKMOUSEEVENT tme{};
                tme.cbSize = sizeof(tme);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hwnd;
                if (TrackMouseEvent(&tme)) {
                    g_trackingMouseLeave = true;
                }
            }
            const float x = static_cast<float>(GET_X_LPARAM(lparam));
            const float y = static_cast<float>(GET_Y_LPARAM(lparam));
            g_hasPointerPosition = true;
            g_lastPointerX = x;
            g_lastPointerY = y;
            bool handled = g_knob->onPointerMove(x, y);
            bool selectionChanged = UpdateHoverDebugSelection(x, y);
            if (handled || selectionChanged) {
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            break;
        }
        case WM_LBUTTONUP: {
            if (g_knob) {
                g_knob->onPointerUp();
            }
            const float x = static_cast<float>(GET_X_LPARAM(lparam));
            const float y = static_cast<float>(GET_Y_LPARAM(lparam));
            g_hasPointerPosition = true;
            g_lastPointerX = x;
            g_lastPointerY = y;
            UpdateHoverDebugSelection(x, y);
            ReleaseCapture();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_KEYDOWN: {
            if (wparam == VK_F12) {
                // F12：切换开发者调试模式，显示类似“盒模型”的边框。
                g_debugOverlayMode =
                    (g_debugOverlayMode == winui::DebugOverlayMode::kOff)
                        ? winui::DebugOverlayMode::kBoxModel
                        : winui::DebugOverlayMode::kOff;
                if (g_hasPointerPosition) {
                    UpdateHoverDebugSelection(g_lastPointerX, g_lastPointerY);
                } else {
                    ClearHoverDebugSelection();
                }
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            break;
        }
        case WM_MOUSELEAVE: {
            g_trackingMouseLeave = false;
            g_hasPointerPosition = false;
            if (ClearHoverDebugSelection()) {
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_DESTROY:
            DiscardDeviceResources();
            g_knob.reset();
            g_mainWindow = nullptr;
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProc(hwnd, msg, wparam, lparam);
}

ATOM RegisterWindowClass(HINSTANCE instance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kWindowClassName;
    return RegisterClassExW(&wc);
}

HWND CreateMainWindow(HINSTANCE instance) {
    // 以较方的工作区展示单旋钮，更便于观察圆形控件细节。
    RECT rect{0, 0, 640, 640};
    AdjustWindowRectEx(&rect, WS_OVERLAPPEDWINDOW, FALSE, 0);
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;

    return CreateWindowExW(
        0, kWindowClassName, kWindowTitle,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        width, height,
        nullptr, nullptr, instance, nullptr);
}

int RunKnobTestApp(HINSTANCE instance, int show) {
    const HRESULT hrCom =
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hrCom)) {
        MessageBoxW(nullptr, L"无法初始化 COM 环境",
                    kWindowTitle, MB_ICONERROR | MB_OK);
        return -1;
    }

    if (!RegisterWindowClass(instance)) {
        MessageBoxW(nullptr, L"注册窗口类失败",
                    kWindowTitle, MB_ICONERROR | MB_OK);
        CoUninitialize();
        return -1;
    }

    HWND hwnd = CreateMainWindow(instance);
    if (!hwnd) {
        MessageBoxW(nullptr, L"创建测试窗口失败",
                    kWindowTitle, MB_ICONERROR | MB_OK);
        CoUninitialize();
        return -1;
    }

    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    CoUninitialize();
    return static_cast<int>(msg.wParam);
}

}  // namespace

int APIENTRY wWinMain(HINSTANCE instance,
                      HINSTANCE,
                      LPWSTR,
                      int show) {
    return RunKnobTestApp(instance, show);
}

int APIENTRY WinMain(HINSTANCE instance,
                     HINSTANCE,
                     LPSTR,
                     int show) {
    return RunKnobTestApp(instance, show);
}
