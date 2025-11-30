#include <windows.h>

#include <array>
#include <memory>

#include "win/audio/SatoriRealtimeEngine.h"

namespace {

const wchar_t kWindowClassName[] = L"SatoriWinClass";
const wchar_t kWindowTitle[] = L"Satori Synth (Preview)";

std::unique_ptr<winaudio::SatoriRealtimeEngine> g_engine;

double KeyToFrequency(WPARAM key) {
    switch (key) {
        case 'A':
            return 261.63;  // C4
        case 'S':
            return 293.66;  // D4
        case 'D':
            return 329.63;  // E4
        case 'F':
            return 349.23;  // F4
        case 'G':
            return 392.00;  // G4
        case 'H':
            return 440.00;  // A4
        case 'J':
            return 493.88;  // B4
        case 'K':
            return 523.25;  // C5
        default:
            return 0.0;
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_CREATE: {
            g_engine = std::make_unique<winaudio::SatoriRealtimeEngine>();
            if (!g_engine->initialize() || !g_engine->start()) {
                MessageBoxW(hwnd, L"无法启动实时音频引擎", kWindowTitle,
                            MB_ICONERROR | MB_OK);
                PostQuitMessage(-1);
            }
            return 0;
        }
        case WM_KEYDOWN: {
            if (!g_engine) {
                return 0;
            }
            const double freq = KeyToFrequency(wparam);
            if (freq > 0.0) {
                g_engine->triggerNote(freq, 2.0);
            }
            return 0;
        }
        case WM_DESTROY:
            if (g_engine) {
                g_engine->stop();
                g_engine->shutdown();
                g_engine.reset();
            }
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProc(hwnd, msg, wparam, lparam);
    }
}

ATOM RegisterSatoriWindowClass(HINSTANCE instance) {
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

HWND CreateSatoriWindow(HINSTANCE instance) {
    return CreateWindowExW(
        0, kWindowClassName, kWindowTitle,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        960, 600,
        nullptr, nullptr, instance, nullptr);
}

}  // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int show) {
    if (!RegisterSatoriWindowClass(instance)) {
        return -1;
    }
    HWND hwnd = CreateSatoriWindow(instance);
    if (!hwnd) {
        return -1;
    }
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return static_cast<int>(msg.wParam);
}

int APIENTRY WinMain(HINSTANCE instance, HINSTANCE prev, LPSTR cmd, int show) {
    (void)cmd;
    return wWinMain(instance, prev, GetCommandLineW(), show);
}
