#include <windows.h>
#include <windowsx.h>

#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "synthesis/KarplusStrongString.h"
#include "win/app/PresetManager.h"
#include "win/audio/SatoriRealtimeEngine.h"
#include "win/ui/Direct2DContext.h"
#include "win/ui/UIModel.h"

namespace {

const wchar_t kWindowClassName[] = L"SatoriWinClass";
const wchar_t kWindowTitle[] = L"Satori Synth (Preview)";

// 推荐窗口客户端区域尺寸（也是本迭代的最小可用尺寸）
constexpr int kMinClientWidth = 1280;
constexpr int kMinClientHeight = 720;
constexpr int kKeyboardBaseMidiNote = 48;  // C3
constexpr int kKeyboardOctaveCount = 3;

std::unique_ptr<winaudio::SatoriRealtimeEngine> g_engine;
std::unique_ptr<winui::Direct2DContext> g_d2d;
std::unique_ptr<winapp::PresetManager> g_presetManager;
synthesis::StringConfig g_synthConfig{};
HWND g_mainWindow = nullptr;
bool g_audioReady = false;
std::wstring g_audioStatus = L"音频：未初始化";
std::wstring g_presetStatus = L"预设：默认";
std::vector<float> g_waveformSamples;
#if SATORI_UI_DEBUG_ENABLED
bool g_trackingMouseLeave = false;
#endif
std::unordered_map<UINT, int> g_virtualKeyToMidi;
std::unordered_map<UINT, int> g_activeVirtualKeys;

std::wstring ToWide(const std::string& text) {
    return std::wstring(text.begin(), text.end());
}

std::filesystem::path GetExecutableDir() {
    wchar_t buffer[MAX_PATH];
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    std::filesystem::path exePath(buffer);
    return exePath.remove_filename();
}

void RefreshUI();
winui::UIModel BuildUIModel();

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
    RefreshUI();
}

void UpdatePresetStatus(const std::wstring& text) {
    g_presetStatus = text;
    RefreshUI();
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

void HandleLoadPreset();
void HandleSavePreset();

void RefreshWaveformPreview(double frequency = 440.0) {
    g_waveformSamples = GenerateWaveform(frequency);
    if (g_d2d) {
        g_d2d->updateWaveformSamples(g_waveformSamples);
    }
    if (g_mainWindow) {
        InvalidateRect(g_mainWindow, nullptr, FALSE);
    }
}

void TriggerFrequency(double frequency) {
    if (frequency <= 0.0) {
        return;
    }
    if (g_engine && g_audioReady) {
        g_engine->triggerNote(frequency, 2.0);
    }
    RefreshWaveformPreview(frequency);
}

void InitializeKeyBindings();
bool HandleMidiKeyDown(UINT vk, LPARAM lparam);
bool HandleMidiKeyUp(UINT vk);
void ReleaseAllVirtualKeys();

winui::UIModel BuildUIModel() {
    winui::UIModel model;
    model.status.primary = g_audioStatus;
    model.waveformSamples = g_waveformSamples;
    model.audioOnline = g_audioReady;
    model.sampleRate = static_cast<float>(g_synthConfig.sampleRate);
    model.diagram.decay = g_synthConfig.decay;
    model.diagram.brightness = g_synthConfig.brightness;
    model.diagram.pickPosition = g_synthConfig.pickPosition;
    model.diagram.noiseType =
        (g_synthConfig.noiseType == synthesis::NoiseType::Binary) ? 1 : 0;

    auto addSlider = [&](const std::wstring& label, float min, float max,
                         float& field) {
        winui::SliderDescriptor desc;
        desc.label = label;
        desc.min = min;
        desc.max = max;
        desc.getter = [&field]() { return field; };
        desc.setter = [&field](float value) {
            field = value;
            SyncSynthConfig();
            RefreshWaveformPreview();
        };
        model.sliders.push_back(std::move(desc));
    };
    addSlider(L"Decay", 0.90f, 0.999f, g_synthConfig.decay);
    addSlider(L"Brightness", 0.0f, 1.0f, g_synthConfig.brightness);
    addSlider(L"Pick Position", 0.05f, 0.95f, g_synthConfig.pickPosition);

    model.keyboardConfig.baseMidiNote = kKeyboardBaseMidiNote;
    model.keyboardConfig.octaveCount = kKeyboardOctaveCount;
    model.keyboardConfig.showLabels = false;
    model.keyboardConfig.hoverOutline = false;
    model.keyCallback = [](double freq) { TriggerFrequency(freq); };

    return model;
}

void RefreshUI() {
    if (!g_d2d) {
        return;
    }
    g_d2d->setModel(BuildUIModel());
}

void InitializePresetSupport(HWND hwnd) {
    const auto presetDir = GetExecutableDir() / L"presets";
    g_presetManager = std::make_unique<winapp::PresetManager>(presetDir);
    const auto defaultPreset = g_presetManager->defaultPresetPath();
    if (std::filesystem::exists(defaultPreset)) {
        std::wstring error;
        if (g_presetManager->load(defaultPreset, g_synthConfig, error)) {
            SyncSynthConfig();
            if (g_d2d) {
                g_d2d->syncSliders();
            }
            RefreshWaveformPreview();
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
    if (g_d2d) {
        g_d2d->syncSliders();
    }
    RefreshWaveformPreview();
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

void InitializeKeyBindings() {
    g_virtualKeyToMidi.clear();
    struct KeyBinding {
        UINT vk = 0;
        int semitoneOffset = 0;
    };
    static const KeyBinding kWhiteBindings[] = {
        {'A', 0}, {'S', 2}, {'D', 4}, {'F', 5},
        {'G', 7}, {'H', 9}, {'J', 11},
    };
    static const KeyBinding kBlackBindings[] = {
        {'W', 1}, {'E', 3}, {'T', 6}, {'Y', 8}, {'U', 10},
    };
    for (const auto& binding : kWhiteBindings) {
        g_virtualKeyToMidi[binding.vk] =
            kKeyboardBaseMidiNote + binding.semitoneOffset;
    }
    for (const auto& binding : kBlackBindings) {
        g_virtualKeyToMidi[binding.vk] =
            kKeyboardBaseMidiNote + binding.semitoneOffset;
    }
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
    const int midi = it->second;
    if (g_d2d && g_d2d->pressKeyboardKey(midi)) {
        g_activeVirtualKeys.emplace(vk, midi);
        if (g_mainWindow) {
            InvalidateRect(g_mainWindow, nullptr, FALSE);
        }
        return true;
    }
    return false;
}

bool HandleMidiKeyUp(UINT vk) {
    auto it = g_activeVirtualKeys.find(vk);
    if (it == g_activeVirtualKeys.end()) {
        return false;
    }
    if (g_d2d) {
        g_d2d->releaseKeyboardKey(it->second);
    }
    g_activeVirtualKeys.erase(it);
    if (g_mainWindow) {
        InvalidateRect(g_mainWindow, nullptr, FALSE);
    }
    return true;
}

void ReleaseAllVirtualKeys() {
    if (g_d2d) {
        g_d2d->releaseAllKeyboardKeys();
    }
    g_activeVirtualKeys.clear();
    if (g_mainWindow) {
        InvalidateRect(g_mainWindow, nullptr, FALSE);
    }
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
            InitializeKeyBindings();
            InitializePresetSupport(hwnd);
            RefreshWaveformPreview();
            RefreshUI();
            UpdateAudioStatus(hwnd, !g_audioReady);
            return 0;
        }
        case WM_GETMINMAXINFO: {
            // 约束窗口最小尺寸，保证客户端区域不小于 1280x720 左右
            auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
            RECT rect{0, 0, kMinClientWidth, kMinClientHeight};
            AdjustWindowRectEx(&rect, WS_OVERLAPPEDWINDOW, FALSE, 0);
            info->ptMinTrackSize.x = rect.right - rect.left;
            info->ptMinTrackSize.y = rect.bottom - rect.top;
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
#if SATORI_UI_DEBUG_ENABLED
            if (wparam == VK_F12) {
                // F12：切换调试叠加（布局边界 + 旋钮盒模型）
                if (g_d2d) {
                    g_d2d->toggleDebugOverlay();
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            }
#endif
            if (wparam == VK_F11) {
                // F11：导出一次布局信息到调试输出，便于脚本/比对
                if (g_d2d) {
                    g_d2d->dumpLayoutDebugInfo();
                }
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
        case WM_LBUTTONDOWN: {
            if (g_d2d && g_d2d->onPointerDown(static_cast<float>(GET_X_LPARAM(lparam)),
                                              static_cast<float>(GET_Y_LPARAM(lparam)))) {
                if (g_d2d->hasPointerCapture()) {
                    SetCapture(hwnd);
                }
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            break;
        }
        case WM_MOUSEMOVE: {
#if SATORI_UI_DEBUG_ENABLED
            if (!g_trackingMouseLeave) {
                TRACKMOUSEEVENT tme{};
                tme.cbSize = sizeof(tme);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hwnd;
                if (TrackMouseEvent(&tme)) {
                    g_trackingMouseLeave = true;
                }
            }
#endif
            if (g_d2d &&
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
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_MOUSELEAVE: {
#if SATORI_UI_DEBUG_ENABLED
            g_trackingMouseLeave = false;
            if (g_d2d && g_d2d->onPointerLeave()) {
                InvalidateRect(hwnd, nullptr, FALSE);
            }
#endif
            return 0;
        }
        case WM_KILLFOCUS:
        case WM_CANCELMODE:
        case WM_ACTIVATE: {
            if (msg != WM_ACTIVATE || LOWORD(wparam) == WA_INACTIVE) {
                ReleaseAllVirtualKeys();
                if (g_d2d) {
                    g_d2d->onPointerUp();
                }
            }
            break;
        }
        case WM_DESTROY:
            if (g_engine) {
                g_engine->stop();
                g_engine->shutdown();
                g_engine.reset();
            }
            ReleaseAllVirtualKeys();
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
    // 计算给定客户端区域（1280x720）对应的窗口外框尺寸
    RECT rect{0, 0, kMinClientWidth, kMinClientHeight};
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
