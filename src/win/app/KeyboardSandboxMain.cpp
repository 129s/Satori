#include <windows.h>
#include <windowsx.h>

#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <dwrite_3.h>
#include <wrl/client.h>

#include <cmath>
#include <cwchar>
#include <optional>
#include <string>
#include <unordered_map>

#include "win/ui/D2DHelpers.h"
#include "win/ui/DebugOverlay.h"
#include "win/ui/KeyboardKeymap.h"
#include "win/ui/NunitoFont.h"
#include "win/ui/VirtualKeyboard.h"

using Microsoft::WRL::ComPtr;

namespace {

const wchar_t kWindowClassName[] = L"SatoriKeyboardSandboxClass";
const wchar_t kWindowTitle[] = L"Satori Keyboard Sandbox";

constexpr int kDefaultOctaveCount = 3;
constexpr int kBaseMidiNote = 48;  // C3

HWND g_mainWindow = nullptr;

ComPtr<ID2D1Factory> g_d2dFactory;
ComPtr<IDWriteFactory> g_dwriteFactory;
ComPtr<IDWriteFontCollection1> g_nunitoFontCollection;
ComPtr<ID2D1HwndRenderTarget> g_renderTarget;
ComPtr<ID2D1SolidColorBrush> g_backgroundBrush;
ComPtr<ID2D1SolidColorBrush> g_whiteFillBrush;
ComPtr<ID2D1SolidColorBrush> g_whitePressedBrush;
ComPtr<ID2D1SolidColorBrush> g_whiteTextBrush;
ComPtr<ID2D1SolidColorBrush> g_blackFillBrush;
ComPtr<ID2D1SolidColorBrush> g_blackPressedBrush;
ComPtr<ID2D1SolidColorBrush> g_blackTextBrush;
ComPtr<ID2D1SolidColorBrush> g_borderBrush;
ComPtr<ID2D1SolidColorBrush> g_hoverBrush;
ComPtr<IDWriteTextFormat> g_textFormat;

winui::VirtualKeyboard g_keyboard;
winui::KeyboardColors g_keyboardColors;
winui::DebugOverlayMode g_debugOverlayMode = winui::DebugOverlayMode::kOff;
winui::DebugOverlayPalette g_overlayPalette = winui::MakeUnifiedDebugOverlayPalette();
winui::DebugBoxRenderer g_debugRenderer;
std::optional<winui::DebugBoxModel> g_debugModel;

bool g_trackingMouseLeave = false;
bool g_hasPointerPosition = false;
float g_lastPointerX = 0.0f;
float g_lastPointerY = 0.0f;

std::unordered_map<UINT, int> g_virtualKeyToMidi;
std::unordered_map<UINT, int> g_activeVirtualKeys;

winui::DebugBoxModel MakeDebugModel(const D2D1_RECT_F& bounds) {
    winui::DebugBoxModel model;
    model.segments.push_back({winui::DebugBoxLayer::kBorder, bounds});
    const float inset = 4.0f;
    model.segments.push_back(
        {winui::DebugBoxLayer::kContent,
         D2D1::RectF(bounds.left + inset, bounds.top + inset, bounds.right - inset,
                     bounds.bottom - inset)});
    return model;
}

bool DebugModelsEqual(const std::optional<winui::DebugBoxModel>& lhs,
                      const winui::DebugBoxModel& rhs) {
    if (!lhs) {
        return false;
    }
    if (lhs->segments.size() != rhs.segments.size()) {
        return false;
    }
    const float kTolerance = 0.5f;
    auto rectClose = [&](const D2D1_RECT_F& a, const D2D1_RECT_F& b) {
        return std::fabs(a.left - b.left) < kTolerance &&
               std::fabs(a.top - b.top) < kTolerance &&
               std::fabs(a.right - b.right) < kTolerance &&
               std::fabs(a.bottom - b.bottom) < kTolerance;
    };
    for (std::size_t i = 0; i < lhs->segments.size(); ++i) {
        if (lhs->segments[i].layer != rhs.segments[i].layer ||
            !rectClose(lhs->segments[i].rect, rhs.segments[i].rect)) {
            return false;
        }
    }
    return true;
}

void InitializeKeyBindings() {
    g_virtualKeyToMidi =
        winui::MakeKeyboardKeymap(kBaseMidiNote, kDefaultOctaveCount);
}

void LogTriggeredFrequency(int midiNote, double frequency, bool pressed) {
    const auto& label = g_keyboard.lastTriggeredLabel();
    wchar_t buffer[160];
    swprintf_s(buffer, L"[KeyboardSandbox] %s (MIDI %d) - %.2f Hz %s\n", label.c_str(),
               midiNote, frequency, pressed ? L"on" : L"off");
    OutputDebugStringW(buffer);
}

void LayoutKeyboard(float width, float height) {
    constexpr float margin = 32.0f;
    const auto bounds =
        D2D1::RectF(margin, margin, width - margin, height - margin);
    g_keyboard.setBounds(bounds);
    g_keyboard.layoutKeys();
}

bool UpdateDebugModel() {
    if (g_debugOverlayMode != winui::DebugOverlayMode::kBoxModel) {
        if (g_debugModel) {
            g_debugModel.reset();
            return true;
        }
        return false;
    }
    D2D1_RECT_F bounds{};
    if (!g_keyboard.focusedKeyBounds(bounds)) {
        if (g_debugModel) {
            g_debugModel.reset();
            return true;
        }
        return false;
    }
    auto next = MakeDebugModel(bounds);
    if (DebugModelsEqual(g_debugModel, next)) {
        return false;
    }
    g_debugModel = std::move(next);
    return true;
}

HRESULT CreateDeviceIndependentResources() {
    if (g_d2dFactory && g_dwriteFactory) {
        return S_OK;
    }
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
    return S_OK;
}

HRESULT CreateTextFormat() {
    if (!g_dwriteFactory) {
        return E_FAIL;
    }
    const bool nunitoAvailable = winui::EnsureNunitoFontLoaded();
    if (!g_nunitoFontCollection) {
        g_nunitoFontCollection =
            winui::CreateNunitoFontCollection(g_dwriteFactory.Get());
    }
    if (!nunitoAvailable && !g_nunitoFontCollection) {
        return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    }
    IDWriteFontCollection* fontCollection =
        g_nunitoFontCollection ? g_nunitoFontCollection.Get() : nullptr;
    HRESULT hr = g_dwriteFactory->CreateTextFormat(
        winui::NunitoFontFamily(), fontCollection, DWRITE_FONT_WEIGHT_MEDIUM,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 16.0f,
        L"zh-CN", &g_textFormat);
    if (FAILED(hr)) {
        return hr;
    }
    winui::ApplyChineseFontFallback(g_dwriteFactory.Get(), g_textFormat.Get());
    g_textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    g_textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    return S_OK;
}

HRESULT CreateDeviceResources(HWND hwnd) {
    if (g_renderTarget) {
        return S_OK;
    }
    const HRESULT hrFactory = CreateDeviceIndependentResources();
    if (FAILED(hrFactory)) {
        return hrFactory;
    }
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const D2D1_SIZE_U size =
        D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);
    HRESULT hr = g_d2dFactory->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(),
        D2D1::HwndRenderTargetProperties(hwnd, size),
        &g_renderTarget);
    if (FAILED(hr)) {
        return hr;
    }

    g_renderTarget->CreateSolidColorBrush(
        D2D1::ColorF(0.02f, 0.02f, 0.04f, 1.0f), &g_backgroundBrush);
    g_renderTarget->CreateSolidColorBrush(
        D2D1::ColorF(0.94f, 0.95f, 0.98f, 1.0f), &g_whiteFillBrush);
    g_renderTarget->CreateSolidColorBrush(
        D2D1::ColorF(0.78f, 0.82f, 0.9f, 1.0f), &g_whitePressedBrush);
    g_renderTarget->CreateSolidColorBrush(
        D2D1::ColorF(0.08f, 0.08f, 0.1f, 1.0f), &g_blackFillBrush);
    g_renderTarget->CreateSolidColorBrush(
        D2D1::ColorF(0.2f, 0.25f, 0.35f, 1.0f), &g_blackPressedBrush);
    g_renderTarget->CreateSolidColorBrush(
        D2D1::ColorF(0.15f, 0.17f, 0.22f, 1.0f), &g_borderBrush);
    g_renderTarget->CreateSolidColorBrush(
        D2D1::ColorF(0.1f, 0.12f, 0.16f, 1.0f), &g_whiteTextBrush);
    g_renderTarget->CreateSolidColorBrush(
        D2D1::ColorF(0.98f, 0.98f, 0.99f, 1.0f), &g_blackTextBrush);
    g_renderTarget->CreateSolidColorBrush(
        D2D1::ColorF(0.85f, 0.46f, 0.18f, 1.0f), &g_hoverBrush);

    hr = CreateTextFormat();
    if (FAILED(hr)) {
        return hr;
    }

    g_keyboardColors.whiteFill = g_whiteFillBrush.Get();
    g_keyboardColors.whitePressed = g_whitePressedBrush.Get();
    g_keyboardColors.whiteBorder = g_borderBrush.Get();
    g_keyboardColors.whiteText = g_whiteTextBrush.Get();
    g_keyboardColors.blackFill = g_blackFillBrush.Get();
    g_keyboardColors.blackPressed = g_blackPressedBrush.Get();
    g_keyboardColors.blackBorder = g_borderBrush.Get();
    g_keyboardColors.blackText = g_blackTextBrush.Get();
    g_keyboardColors.hoverOutline = g_hoverBrush.Get();

    g_debugRenderer.setPalette(g_overlayPalette);
    return S_OK;
}

void DiscardDeviceResources() {
    g_renderTarget.Reset();
    g_backgroundBrush.Reset();
    g_whiteFillBrush.Reset();
    g_whitePressedBrush.Reset();
    g_whiteTextBrush.Reset();
    g_blackFillBrush.Reset();
    g_blackPressedBrush.Reset();
    g_blackTextBrush.Reset();
    g_borderBrush.Reset();
    g_hoverBrush.Reset();
    g_textFormat.Reset();
}

void OnRender() {
    if (!g_mainWindow) {
        return;
    }
    if (FAILED(CreateDeviceResources(g_mainWindow))) {
        return;
    }
    if (!g_renderTarget || !g_textFormat) {
        return;
    }

    const auto size = g_renderTarget->GetSize();
    LayoutKeyboard(size.width, size.height);

    g_renderTarget->BeginDraw();
    g_renderTarget->SetTransform(D2D1::Matrix3x2F::Identity());
    g_renderTarget->Clear(D2D1::ColorF(0.02f, 0.02f, 0.04f, 1.0f));
    if (g_backgroundBrush) {
        g_renderTarget->FillRectangle(
            D2D1::RectF(0.0f, 0.0f, size.width, size.height),
            g_backgroundBrush.Get());
    }

    g_keyboard.draw(g_renderTarget.Get(), g_keyboardColors, g_textFormat.Get());

    if (g_debugOverlayMode == winui::DebugOverlayMode::kBoxModel && g_debugModel) {
        winui::ScopedAntialiasMode scoped(g_renderTarget.Get(),
                                          D2D1_ANTIALIAS_MODE_ALIASED);
        g_debugRenderer.render(g_renderTarget.Get(), *g_debugModel, true);
    }

    const HRESULT hr = g_renderTarget->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        DiscardDeviceResources();
    }
}

void EnsureKeyboardInitialized() {
    static bool initialized = false;
    if (initialized) {
        return;
    }
    g_keyboard.setCallback(LogTriggeredFrequency);
    g_keyboard.setPianoLayout(kBaseMidiNote, kDefaultOctaveCount);
    g_keyboard.setShowLabels(false);
    g_keyboard.setHoverOutline(false);
    InitializeKeyBindings();
    initialized = true;
}

bool HandleMidiKeyDown(UINT vk, LPARAM lparam) {
    if ((lparam & (1 << 30)) != 0) {  // autorepeat
        return g_virtualKeyToMidi.find(vk) != g_virtualKeyToMidi.end();
    }
    auto it = g_virtualKeyToMidi.find(vk);
    if (it == g_virtualKeyToMidi.end()) {
        return false;
    }
    if (g_activeVirtualKeys.count(vk)) {
        return true;
    }
    if (g_keyboard.pressKeyByMidi(it->second)) {
        g_activeVirtualKeys.emplace(vk, it->second);
        UpdateDebugModel();
        InvalidateRect(g_mainWindow, nullptr, FALSE);
        return true;
    }
    return false;
}

bool HandleMidiKeyUp(UINT vk) {
    auto it = g_activeVirtualKeys.find(vk);
    if (it == g_activeVirtualKeys.end()) {
        return false;
    }
    g_keyboard.releaseKeyByMidi(it->second);
    g_activeVirtualKeys.erase(it);
    UpdateDebugModel();
    InvalidateRect(g_mainWindow, nullptr, FALSE);
    return true;
}

void ReleaseAllVirtualKeys() {
    for (const auto& entry : g_activeVirtualKeys) {
        g_keyboard.releaseKeyByMidi(entry.second);
    }
    g_activeVirtualKeys.clear();
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_CREATE: {
            g_mainWindow = hwnd;
            EnsureKeyboardInitialized();
            RECT rc{};
            GetClientRect(hwnd, &rc);
            LayoutKeyboard(static_cast<float>(rc.right - rc.left),
                           static_cast<float>(rc.bottom - rc.top));
            SetFocus(hwnd);
            return 0;
        }
        case WM_SIZE: {
            if (g_renderTarget) {
                g_renderTarget->Resize(
                    D2D1::SizeU(LOWORD(lparam), HIWORD(lparam)));
            }
            LayoutKeyboard(static_cast<float>(LOWORD(lparam)),
                           static_cast<float>(HIWORD(lparam)));
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
            const float x = static_cast<float>(GET_X_LPARAM(lparam));
            const float y = static_cast<float>(GET_Y_LPARAM(lparam));
            g_hasPointerPosition = true;
            g_lastPointerX = x;
            g_lastPointerY = y;
            if (g_keyboard.onPointerDown(x, y)) {
                SetCapture(hwnd);
            }
            UpdateDebugModel();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_MOUSEMOVE: {
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
            if (g_keyboard.onPointerMove(x, y)) {
                UpdateDebugModel();
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            UpdateDebugModel();
            return 0;
        }
        case WM_LBUTTONUP: {
            g_keyboard.onPointerUp();
            ReleaseCapture();
            UpdateDebugModel();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_MOUSELEAVE: {
            g_trackingMouseLeave = false;
            g_hasPointerPosition = false;
            g_keyboard.onPointerUp();
            UpdateDebugModel();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_KEYDOWN: {
            if (wparam == VK_F12) {
                g_debugOverlayMode =
                    (g_debugOverlayMode == winui::DebugOverlayMode::kOff)
                        ? winui::DebugOverlayMode::kBoxModel
                        : winui::DebugOverlayMode::kOff;
                UpdateDebugModel();
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (HandleMidiKeyDown(static_cast<UINT>(wparam), lparam)) {
                return 0;
            }
            break;
        }
        case WM_KEYUP: {
            if (HandleMidiKeyUp(static_cast<UINT>(wparam))) {
                return 0;
            }
            break;
        }
        case WM_KILLFOCUS:
        case WM_CANCELMODE:
        case WM_ACTIVATE: {
            if (msg == WM_ACTIVATE && LOWORD(wparam) != WA_INACTIVE) {
                break;
            }
            ReleaseAllVirtualKeys();
            g_keyboard.onPointerUp();
            UpdateDebugModel();
            InvalidateRect(hwnd, nullptr, FALSE);
            break;
        }
        case WM_DESTROY: {
            ReleaseAllVirtualKeys();
            g_keyboard.releaseAllKeys();
            DiscardDeviceResources();
            g_mainWindow = nullptr;
            PostQuitMessage(0);
            return 0;
        }
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
    wc.lpszClassName = kWindowClassName;
    return RegisterClassExW(&wc);
}

HWND CreateMainWindow(HINSTANCE instance) {
    RECT rect{0, 0, 960, 360};
    AdjustWindowRectEx(&rect, WS_OVERLAPPEDWINDOW, FALSE, 0);
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    return CreateWindowExW(0, kWindowClassName, kWindowTitle,
                           WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                           width, height, nullptr, nullptr, instance, nullptr);
}

int RunKeyboardSandbox(HINSTANCE instance, int show) {
    const HRESULT hrCom = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hrCom)) {
        MessageBoxW(nullptr, L"无法初始化 COM 环境", kWindowTitle,
                    MB_ICONERROR | MB_OK);
        return -1;
    }
    if (!RegisterWindowClass(instance)) {
        MessageBoxW(nullptr, L"注册窗口类失败", kWindowTitle,
                    MB_ICONERROR | MB_OK);
        CoUninitialize();
        return -1;
    }
    HWND hwnd = CreateMainWindow(instance);
    if (!hwnd) {
        MessageBoxW(nullptr, L"创建沙箱窗口失败", kWindowTitle,
                    MB_ICONERROR | MB_OK);
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

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int show) {
    return RunKeyboardSandbox(instance, show);
}

int APIENTRY WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int show) {
    return RunKeyboardSandbox(instance, show);
}
