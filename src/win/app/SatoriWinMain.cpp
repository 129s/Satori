#include <windows.h>
#include <windowsx.h>

#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "synthesis/KarplusStrongString.h"
#include "win/app/PresetManager.h"
#include "win/audio/SatoriRealtimeEngine.h"
#include "win/ui/Direct2DContext.h"
#include "win/ui/ParameterSlider.h"

namespace {

const wchar_t kWindowClassName[] = L"SatoriWinClass";
const wchar_t kWindowTitle[] = L"Satori Synth (Preview)";

std::unique_ptr<winaudio::SatoriRealtimeEngine> g_engine;
std::unique_ptr<winui::Direct2DContext> g_d2d;
std::unique_ptr<winapp::PresetManager> g_presetManager;
synthesis::StringConfig g_synthConfig{};
HWND g_mainWindow = nullptr;
bool g_audioReady = false;
std::wstring g_audioStatus = L"音频：未初始化";
std::wstring g_presetStatus = L"预设：默认";

struct SliderBinding {
    std::shared_ptr<winui::ParameterSlider> slider;
    float* value = nullptr;
};
std::vector<SliderBinding> g_sliderBindings;

const std::vector<std::pair<std::wstring, double>> kVirtualKeys = {
    {L"C4", 261.63}, {L"D4", 293.66}, {L"E4", 329.63}, {L"F4", 349.23},
    {L"G4", 392.00}, {L"A4", 440.00}, {L"B4", 493.88}, {L"C5", 523.25}};

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

std::wstring ToWide(const std::string& text) {
    return std::wstring(text.begin(), text.end());
}

std::filesystem::path GetExecutableDir() {
    wchar_t buffer[MAX_PATH];
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    std::filesystem::path exePath(buffer);
    return exePath.remove_filename();
}

void RefreshStatusText() {
    if (!g_d2d) {
        return;
    }
    std::wstring combined = g_audioStatus;
    if (!g_presetStatus.empty()) {
        combined += L"\n" + g_presetStatus;
    }
    g_d2d->setStatusText(combined);
    if (g_mainWindow) {
        InvalidateRect(g_mainWindow, nullptr, FALSE);
    }
}

void UpdateAudioStatus(HWND hwnd, bool showDialog) {
    if (g_audioReady) {
        std::wstringstream ss;
        ss << L"音频：在线 (" << static_cast<int>(g_synthConfig.sampleRate) << L" Hz)";
        g_audioStatus = ss.str();
    } else {
        std::wstring message = L"音频：离线";
        if (g_engine) {
            const auto& lastError = g_engine->lastError();
            if (!lastError.empty()) {
                message += L"\n";
                message += ToWide(lastError);
            }
        }
        g_audioStatus = message;
        if (showDialog && hwnd) {
            MessageBoxW(hwnd, message.c_str(), kWindowTitle,
                        MB_ICONWARNING | MB_OK);
        }
    }
    RefreshStatusText();
}

void UpdatePresetStatus(const std::wstring& text) {
    g_presetStatus = text;
    RefreshStatusText();
}

void SyncSynthConfig() {
    if (g_engine) {
        g_engine->setSynthConfig(g_synthConfig);
    }
}

std::vector<float> GenerateWaveform(double frequency) {
    const double duration = 0.5;
    synthesis::KarplusStrongString string(g_synthConfig);
    auto samples = string.pluck(frequency, duration);
    const std::size_t maxSamples = 2048;
    if (samples.size() > maxSamples) {
        samples.resize(maxSamples);
    }
    return samples;
}

void ApplyConfigToSliders() {
    for (const auto& binding : g_sliderBindings) {
        if (binding.slider && binding.value) {
            binding.slider->syncValue(*binding.value);
        }
    }
}

void TriggerFrequency(double frequency) {
    if (frequency <= 0.0) {
        return;
    }
    if (g_engine && g_audioReady) {
        g_engine->triggerNote(frequency, 2.0);
    }
    if (g_d2d) {
        g_d2d->setWaveformSamples(GenerateWaveform(frequency));
    }
    if (g_mainWindow) {
        InvalidateRect(g_mainWindow, nullptr, FALSE);
    }
}

void HandleLoadPreset();
void HandleSavePreset();

void InitializePresetSupport(HWND hwnd) {
    const auto presetDir = GetExecutableDir() / L"presets";
    g_presetManager = std::make_unique<winapp::PresetManager>(presetDir);
    if (g_d2d) {
        g_d2d->setPresetCallbacks(HandleLoadPreset, HandleSavePreset);
    }
    const auto defaultPreset = g_presetManager->defaultPresetPath();
    if (std::filesystem::exists(defaultPreset)) {
        std::wstring error;
        if (g_presetManager->load(defaultPreset, g_synthConfig, error)) {
            SyncSynthConfig();
            ApplyConfigToSliders();
            UpdatePresetStatus(L"预设：" + defaultPreset.filename().wstring());
        } else if (hwnd) {
            MessageBoxW(hwnd, error.c_str(), kWindowTitle, MB_ICONWARNING | MB_OK);
        }
    }
}

void HandleLoadPreset() {
    if (!g_presetManager) {
        return;
    }
    auto path = g_presetManager->userPresetPath();
    if (!std::filesystem::exists(path)) {
        path = g_presetManager->defaultPresetPath();
    }
    if (!std::filesystem::exists(path)) {
        MessageBoxW(g_mainWindow, L"未找到可用预设文件", kWindowTitle,
                    MB_ICONWARNING | MB_OK);
        return;
    }
    std::wstring error;
    if (!g_presetManager->load(path, g_synthConfig, error)) {
        MessageBoxW(g_mainWindow, error.c_str(), kWindowTitle,
                    MB_ICONWARNING | MB_OK);
        return;
    }
    SyncSynthConfig();
    ApplyConfigToSliders();
    UpdatePresetStatus(L"预设：" + path.filename().wstring());
}

void HandleSavePreset() {
    if (!g_presetManager) {
        return;
    }
    std::wstring error;
    const auto path = g_presetManager->userPresetPath();
    if (!g_presetManager->save(path, g_synthConfig, error)) {
        MessageBoxW(g_mainWindow, error.c_str(), kWindowTitle,
                    MB_ICONWARNING | MB_OK);
        return;
    }
    UpdatePresetStatus(L"预设：已保存到 " + path.filename().wstring());
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_CREATE: {
            g_mainWindow = hwnd;
            g_engine = std::make_unique<winaudio::SatoriRealtimeEngine>();
            const bool initialized = g_engine->initialize();
            g_audioReady = initialized && g_engine->start();

            g_d2d = std::make_unique<winui::Direct2DContext>();
            if (!g_d2d->initialize(hwnd)) {
                MessageBoxW(hwnd, L"初始化 Direct2D 失败", kWindowTitle,
                            MB_ICONERROR | MB_OK);
                PostQuitMessage(-1);
                return 0;
            }

            g_synthConfig = g_engine->synthConfig();
            g_sliderBindings.clear();
            std::vector<std::shared_ptr<winui::ParameterSlider>> sliders;
            auto bindSlider = [&](const std::wstring& label, float min, float max,
                                  float& field) {
                float* valuePtr = &field;
                auto slider = std::make_shared<winui::ParameterSlider>(
                    label, min, max, field,
                    [valuePtr](float value) {
                        *valuePtr = value;
                        SyncSynthConfig();
                    });
                g_sliderBindings.push_back(SliderBinding{slider, valuePtr});
                sliders.push_back(std::move(slider));
            };
            bindSlider(L"Decay", 0.90f, 0.999f, g_synthConfig.decay);
            bindSlider(L"Brightness", 0.0f, 1.0f, g_synthConfig.brightness);
            bindSlider(L"Pick Position", 0.05f, 0.95f, g_synthConfig.pickPosition);
            g_d2d->setSliders(sliders);
            g_d2d->setKeyboardKeys(kVirtualKeys);
            g_d2d->setKeyboardCallback(
                [](double frequency) { TriggerFrequency(frequency); });
            InitializePresetSupport(hwnd);
            UpdateAudioStatus(hwnd, !g_audioReady);
            RefreshStatusText();
            return 0;
        }
        case WM_SIZE: {
            if (g_d2d) {
                g_d2d->resize(LOWORD(lparam), HIWORD(lparam));
            }
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            if (g_d2d) {
                g_d2d->render();
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_KEYDOWN: {
            const double freq = KeyToFrequency(wparam);
            if (freq > 0.0) {
                TriggerFrequency(freq);
            }
            return 0;
        }
        case WM_LBUTTONDOWN: {
            if (g_d2d && g_d2d->onPointerDown(static_cast<float>(GET_X_LPARAM(lparam)),
                                              static_cast<float>(GET_Y_LPARAM(lparam)))) {
                SetCapture(hwnd);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            break;
        }
        case WM_MOUSEMOVE: {
            if ((wparam & MK_LBUTTON) && g_d2d &&
                g_d2d->onPointerMove(static_cast<float>(GET_X_LPARAM(lparam)),
                                     static_cast<float>(GET_Y_LPARAM(lparam)))) {
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            break;
        }
        case WM_LBUTTONUP: {
            if (g_d2d) {
                g_d2d->onPointerUp();
            }
            ReleaseCapture();
            return 0;
        }
        case WM_DESTROY:
            if (g_engine) {
                g_engine->stop();
                g_engine->shutdown();
                g_engine.reset();
            }
            g_d2d.reset();
            g_presetManager.reset();
            g_mainWindow = nullptr;
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }

    return DefWindowProc(hwnd, msg, wparam, lparam);
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
        960, 640,
        nullptr, nullptr, instance, nullptr);
}

int RunSatoriApp(HINSTANCE instance, int show) {
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        MessageBoxW(nullptr, L"无法初始化 COM 环境", kWindowTitle,
                    MB_ICONERROR | MB_OK);
        return -1;
    }

    if (!RegisterSatoriWindowClass(instance)) {
        MessageBoxW(nullptr, L"注册窗口类失败", kWindowTitle, MB_ICONERROR | MB_OK);
        CoUninitialize();
        return -1;
    }
    HWND hwnd = CreateSatoriWindow(instance);
    if (!hwnd) {
        MessageBoxW(nullptr, L"创建窗口失败", kWindowTitle, MB_ICONERROR | MB_OK);
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
    return RunSatoriApp(instance, show);
}

int APIENTRY WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int show) {
    return RunSatoriApp(instance, show);
}
