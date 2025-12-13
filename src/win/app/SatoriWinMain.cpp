#include <windows.h>
#include <windowsx.h>

#include <cmath>
#include <algorithm>
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
#include "win/ui/KeyboardKeymap.h"
#include "win/ui/UIModel.h"
#include "dsp/RoomIrLibrary.h"

namespace {

const wchar_t kWindowClassName[] = L"SatoriWinClass";
const wchar_t kWindowTitle[] = L"Satori Synth (Preview)";

// 推荐窗口客户端区域尺寸（也是本迭代的最小可用尺寸）
constexpr int kMinClientWidth = 1280;
constexpr int kMinClientHeight = 720;
constexpr int kKeyboardBaseMidiNote = 48;  // C3
constexpr int kKeyboardOctaveCount = 3;

std::wstring ToWide(const std::string& text) {
    return std::wstring(text.begin(), text.end());
}

std::filesystem::path GetExecutableDir() {
    wchar_t buffer[MAX_PATH];
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    std::filesystem::path exePath(buffer);
    return exePath.remove_filename();
}

double MidiToFrequency(int midi) {
    return 440.0 * std::pow(2.0, (static_cast<double>(midi) - 69.0) / 12.0);
}

class SatoriAppState {
public:
    bool initialize(HWND hwnd);
    void shutdown();

    void onSize(int width, int height);
    void onPaint();
    bool onKeyDown(UINT vk, LPARAM lparam);
    bool onKeyUp(UINT vk);
    bool onPointerDown(float x, float y);
    bool onPointerMove(float x, float y);
    void onPointerUp();
#if SATORI_UI_DEBUG_ENABLED
    void onMouseLeave();
#endif
    void onDeactivate();

private:
    winui::UIModel buildUIModel();
    winui::FlowDiagramState buildDiagramState() const;
    void refreshUI();
    void refreshFlowDiagram();
    void updateAudioStatus(bool showDialog);
    void updatePresetStatus(const std::wstring& text);
    void syncSynthConfig();
    std::vector<float> generateWaveform(double frequency);
    std::vector<float> generateExcitationTransient(double frequency);
    void refreshWaveformPreview(double frequency = 440.0);
    void handleVirtualKeyEvent(int midiNote, double frequency, bool pressed);
    void initializeKeyBindings();
    void initializePresetSupport();
    void handleLoadPreset();
    void handleSavePreset();
    bool handleMidiKeyDown(UINT vk, LPARAM lparam);
    bool handleMidiKeyUp(UINT vk);
    void releaseAllVirtualKeys();

    HWND window_ = nullptr;
    std::unique_ptr<winaudio::SatoriRealtimeEngine> engine_;
    std::unique_ptr<winui::Direct2DContext> d2d_;
    std::unique_ptr<winapp::PresetManager> presetManager_;
    synthesis::StringConfig synthConfig_{};
    float masterGain_ = 1.0f;
    float ampRelease_ = 0.35f;
    winui::UIMode uiMode_ = winui::UIMode::Play;
    bool audioReady_ = false;
    std::wstring audioStatus_ = L"音频：未初始化";
    std::wstring presetStatus_ = L"预设：默认";
    std::vector<float> waveformSamples_;
    std::vector<float> excitationSamples_;
#if SATORI_UI_DEBUG_ENABLED
    bool trackingMouseLeave_ = false;
#endif
    std::unordered_map<UINT, int> virtualKeyToMidi_;
    std::unordered_map<UINT, int> activeVirtualKeys_;
};

bool SatoriAppState::initialize(HWND hwnd) {
    window_ = hwnd;
    engine_ = std::make_unique<winaudio::SatoriRealtimeEngine>();
    const bool initialized = engine_->initialize();
    audioReady_ = initialized && engine_->start();

    d2d_ = std::make_unique<winui::Direct2DContext>();
    if (!d2d_->initialize(hwnd)) {
        MessageBoxW(hwnd, L"初始化 Direct2D 失败", kWindowTitle,
                    MB_ICONERROR | MB_OK);
        PostQuitMessage(-1);
        return false;
    }

    synthConfig_ = engine_->synthConfig();
    masterGain_ = engine_->masterGain();
    ampRelease_ = engine_->getParam(engine::ParamId::AmpRelease);
    initializeKeyBindings();
    initializePresetSupport();
    refreshWaveformPreview();
    refreshUI();
    updateAudioStatus(!audioReady_);
    return true;
}

void SatoriAppState::shutdown() {
    if (engine_) {
        engine_->stop();
        engine_->shutdown();
        engine_.reset();
    }
    releaseAllVirtualKeys();
    d2d_.reset();
    presetManager_.reset();
    window_ = nullptr;
}

winui::FlowDiagramState SatoriAppState::buildDiagramState() const {
    winui::FlowDiagramState diagram{};
    diagram.decay = synthConfig_.decay;
    diagram.brightness = synthConfig_.brightness;
    diagram.dispersionAmount = synthConfig_.dispersionAmount;
    diagram.pickPosition = synthConfig_.pickPosition;
    diagram.bodyTone = synthConfig_.bodyTone;
    diagram.bodySize = synthConfig_.bodySize;
    diagram.roomAmount = synthConfig_.roomAmount;
    diagram.roomIrIndex = synthConfig_.roomIrIndex;
    diagram.noiseType =
        (synthConfig_.noiseType == synthesis::NoiseType::Binary) ? 1 : 0;
    diagram.excitationSamples = excitationSamples_;
    diagram.roomIrPreviewSamples =
        dsp::RoomIrLibrary::previewMono(synthConfig_.roomIrIndex, 1024);
    diagram.highlightedModule = winui::FlowModule::kNone;
    return diagram;
}

void SatoriAppState::refreshFlowDiagram() {
    if (d2d_) {
        d2d_->updateDiagramState(buildDiagramState());
    }
}

winui::UIModel SatoriAppState::buildUIModel() {
    winui::UIModel model;
    model.mode = uiMode_;
    model.status.primary = audioStatus_;
    model.status.secondary = presetStatus_;
    model.waveformSamples = waveformSamples_;
    model.audioOnline = audioReady_;
    model.sampleRate = static_cast<float>(synthConfig_.sampleRate);
    model.diagram = buildDiagramState();

    auto makeModule = [](std::wstring title, winui::FlowModule module,
                         bool shared) {
        winui::ModuleUI ui;
        ui.title = std::move(title);
        ui.module = module;
        ui.isShared = shared;
        return ui;
    };

    auto makeParam = [&](const std::wstring& label, float min, float max,
                         std::function<float()> getter,
                         std::function<void(float)> setter,
                         winui::FlowModule module, bool surface) {
        winui::ModuleParamDescriptor desc;
        desc.label = label;
        desc.min = min;
        desc.max = max;
        desc.getter = std::move(getter);
        desc.setter = std::move(setter);
        desc.module = module;
        desc.isSurfaceParam = surface;
        return desc;
    };

    std::vector<winui::ModuleUI> modules;

    winui::ModuleUI excitation =
        makeModule(L"EXCITATION", winui::FlowModule::kExcitation, false);
    excitation.params.push_back(
        makeParam(L"Noise Type", 0.0f, 1.0f,
                  [this]() {
                      return synthConfig_.noiseType == synthesis::NoiseType::Binary
                                 ? 1.0f
                                 : 0.0f;
                  },
                  [this](float value) {
                      synthConfig_.noiseType = (value >= 0.5f)
                                                   ? synthesis::NoiseType::Binary
                                                   : synthesis::NoiseType::White;
                      syncSynthConfig();
                      refreshWaveformPreview();
                  },
                  winui::FlowModule::kExcitation, true));
    excitation.params.push_back(
        makeParam(L"Hardness", 0.0f, 1.0f,
                  [this]() { return synthConfig_.excitationBrightness; },
                  [this](float value) {
                      synthConfig_.excitationBrightness = value;
                      syncSynthConfig();
                      refreshWaveformPreview();
                  },
                  winui::FlowModule::kExcitation, true));
    excitation.params.push_back(
        makeParam(L"Mix", 0.0f, 1.0f,
                  [this]() { return synthConfig_.excitationMix; },
                  [this](float value) {
                      synthConfig_.excitationMix = value;
                      syncSynthConfig();
                      refreshWaveformPreview();
                  },
                  winui::FlowModule::kExcitation, true));
    excitation.params.push_back(
        makeParam(L"Vel. Sens", 0.0f, 1.0f,
                  [this]() { return synthConfig_.excitationVelocity; },
                  [this](float value) {
                      synthConfig_.excitationVelocity = value;
                      syncSynthConfig();
                      refreshWaveformPreview();
                  },
                  winui::FlowModule::kExcitation, true));
    excitation.params.push_back(
        makeParam(L"Position", 0.05f, 0.95f,
                  [this]() { return synthConfig_.pickPosition; },
                  [this](float value) {
                      synthConfig_.pickPosition = value;
                      syncSynthConfig();
                      refreshWaveformPreview();
                  },
                  winui::FlowModule::kExcitation, false));
    excitation.params.push_back(
        makeParam(L"Excite Mode", 0.0f, 1.0f,
                  [this]() {
                      return synthConfig_.excitationMode ==
                                     synthesis::ExcitationMode::FixedNoisePick
                                 ? 1.0f
                                 : 0.0f;
                  },
                  [this](float value) {
                      synthConfig_.excitationMode =
                          (value >= 0.5f)
                              ? synthesis::ExcitationMode::FixedNoisePick
                              : synthesis::ExcitationMode::RandomNoisePick;
                      syncSynthConfig();
                      refreshWaveformPreview();
                  },
                  winui::FlowModule::kExcitation, false));
    excitation.params.push_back(
        makeParam(L"Excite Seed", 0.0f, 10000.0f,
                  [this]() { return static_cast<float>(synthConfig_.seed); },
                  [this](float value) {
                      value = std::clamp(value, 0.0f, 10000.0f);
                      synthConfig_.seed =
                          static_cast<unsigned int>(std::round(value));
                      syncSynthConfig();
                      refreshWaveformPreview();
                  },
                  winui::FlowModule::kExcitation, false));
    modules.push_back(std::move(excitation));

    winui::ModuleUI stringModule =
        makeModule(L"STRING LOOP", winui::FlowModule::kString, false);
    stringModule.params.push_back(
        makeParam(L"Decay", 0.90f, 0.999f,
                  [this]() { return synthConfig_.decay; },
                  [this](float value) {
                      synthConfig_.decay = value;
                      syncSynthConfig();
                      refreshWaveformPreview();
                  },
                  winui::FlowModule::kString, true));
    stringModule.params.push_back(
        makeParam(L"Brightness", 0.0f, 1.0f,
                  [this]() { return synthConfig_.brightness; },
                  [this](float value) {
                      synthConfig_.brightness = value;
                      syncSynthConfig();
                      refreshWaveformPreview();
                  },
                  winui::FlowModule::kString, true));
    stringModule.params.push_back(
        makeParam(L"Dispersion", 0.0f, 1.0f,
                  [this]() { return synthConfig_.dispersionAmount; },
                  [this](float value) {
                      synthConfig_.dispersionAmount = value;
                      syncSynthConfig();
                      refreshWaveformPreview();
                  },
                  winui::FlowModule::kString, true));
    stringModule.params.push_back(
        makeParam(L"Lowpass", 0.0f, 1.0f,
                  [this]() { return synthConfig_.enableLowpass ? 1.0f : 0.0f; },
                  [this](float value) {
                      synthConfig_.enableLowpass = value >= 0.5f;
                      syncSynthConfig();
                      refreshWaveformPreview();
                  },
                  winui::FlowModule::kString, false));
    modules.push_back(std::move(stringModule));

    winui::ModuleUI bodyModule =
        makeModule(L"BODY", winui::FlowModule::kBody, true);
    bodyModule.params.push_back(
        makeParam(L"Body Tone", 0.0f, 1.0f,
                  [this]() { return synthConfig_.bodyTone; },
                  [this](float value) {
                      synthConfig_.bodyTone = value;
                      syncSynthConfig();
                      refreshWaveformPreview();
                  },
                  winui::FlowModule::kBody, true));
    bodyModule.params.push_back(
        makeParam(L"Body Size", 0.0f, 1.0f,
                  [this]() { return synthConfig_.bodySize; },
                  [this](float value) {
                      synthConfig_.bodySize = value;
                      syncSynthConfig();
                      refreshWaveformPreview();
                  },
                  winui::FlowModule::kBody, true));
    modules.push_back(std::move(bodyModule));

    winui::ModuleUI roomModule =
        makeModule(L"ROOM", winui::FlowModule::kRoom, true);
    roomModule.params.push_back(
        makeParam(L"Mix", 0.0f, 1.0f,
                  [this]() { return synthConfig_.roomAmount; },
                  [this](float value) {
                      synthConfig_.roomAmount = value;
                      syncSynthConfig();
                      refreshWaveformPreview();
                  },
                  winui::FlowModule::kRoom, true));
    {
        const float maxIr =
            std::max(0.0f,
                     static_cast<float>(std::max<std::size_t>(1, dsp::RoomIrLibrary::list().size()) - 1));
        roomModule.params.push_back(
            makeParam(L"IR", 0.0f, maxIr,
                      [this]() { return static_cast<float>(synthConfig_.roomIrIndex); },
                      [this](float value) {
                          const int idx = static_cast<int>(std::lround(value));
                          synthConfig_.roomIrIndex = std::max(0, idx);
                          syncSynthConfig();
                          refreshWaveformPreview();
                      },
                      winui::FlowModule::kRoom, false));
    }
    roomModule.params.push_back(
        makeParam(L"Master Gain", 0.0f, 2.0f,
                  [this]() { return masterGain_; },
                  [this](float value) {
                      masterGain_ = value;
                      syncSynthConfig();
                      refreshWaveformPreview();
                  },
                  winui::FlowModule::kRoom, false));
    roomModule.params.push_back(
        makeParam(L"Amp Release", 0.01f, 5.0f,
                  [this]() { return ampRelease_; },
                  [this](float value) {
                      ampRelease_ = value;
                      syncSynthConfig();
                      refreshWaveformPreview();
                  },
                  winui::FlowModule::kRoom, false));
    modules.push_back(std::move(roomModule));

    model.modules = std::move(modules);

    model.keyboardConfig.baseMidiNote = kKeyboardBaseMidiNote;
    model.keyboardConfig.octaveCount = kKeyboardOctaveCount;
    model.keyboardConfig.showLabels = false;
    model.keyboardConfig.hoverOutline = false;
    model.keyCallback = [this](int midi, double freq, bool pressed) {
        handleVirtualKeyEvent(midi, freq, pressed);
    };

    return model;
}

void SatoriAppState::refreshUI() {
    if (!d2d_) {
        return;
    }
    d2d_->setModel(buildUIModel());
}

void SatoriAppState::updateAudioStatus(bool showDialog) {
    if (audioReady_) {
        std::wstringstream ss;
        ss << L"????? (" << static_cast<int>(synthConfig_.sampleRate) << L" Hz)";
        audioStatus_ = ss.str();
    } else {
        std::wstring message = L"?????";
        if (engine_) {
            const auto& lastError = engine_->lastError();
            if (!lastError.empty()) {
                message += L"\n";
                message += ToWide(lastError);
            }
        }
        audioStatus_ = message;
        if (showDialog && window_) {
            MessageBoxW(window_, message.c_str(), kWindowTitle,
                        MB_ICONWARNING | MB_OK);
        }
    }
    refreshUI();
}

void SatoriAppState::updatePresetStatus(const std::wstring& text) {
    presetStatus_ = text;
    refreshUI();
}

void SatoriAppState::syncSynthConfig() {
    if (engine_) {
        engine_->setSynthConfig(synthConfig_);
        engine_->setMasterGain(masterGain_);
        engine_->setParam(engine::ParamId::AmpRelease, ampRelease_);
        synthConfig_ = engine_->synthConfig();
        masterGain_ = engine_->masterGain();
        ampRelease_ = engine_->getParam(engine::ParamId::AmpRelease);
    }
    refreshFlowDiagram();
}

std::vector<float> SatoriAppState::generateWaveform(double frequency) {
    const double duration = 0.5;
    synthesis::KarplusStrongString string(synthConfig_);
    auto samples = string.pluck(frequency, duration);
    const std::size_t maxSamples = 2048;
    if (samples.size() > maxSamples) {
        samples.resize(maxSamples);
    }
    const float gain = masterGain_;
    for (auto& sample : samples) {
        sample *= gain;
    }
    return samples;
}

std::vector<float> SatoriAppState::generateExcitationTransient(double frequency) {
    if (frequency <= 0.0) {
        return {};
    }
    synthesis::KarplusStrongString string(synthConfig_);
    string.start(frequency, 1.0f);
    auto samples = string.excitationBufferPreview(1024);
    const float gain = masterGain_;
    for (auto& sample : samples) {
        sample *= gain;
    }
    return samples;
}

void SatoriAppState::refreshWaveformPreview(double frequency) {
    waveformSamples_ = generateWaveform(frequency);
    excitationSamples_ = generateExcitationTransient(frequency);
    if (d2d_) {
        d2d_->updateWaveformSamples(waveformSamples_);
        d2d_->updateDiagramState(buildDiagramState());
    }
    if (window_) {
        InvalidateRect(window_, nullptr, FALSE);
    }
}

void SatoriAppState::handleVirtualKeyEvent(int midiNote, double frequency,
                                           bool pressed) {
    if (frequency <= 0.0) {
        return;
    }
    if (engine_ && audioReady_) {
        if (pressed) {
            engine_->noteOn(midiNote, frequency);
        } else {
            engine_->noteOff(midiNote);
        }
    }
    if (pressed) {
        refreshWaveformPreview(frequency);
    }
}

void SatoriAppState::initializeKeyBindings() {
    virtualKeyToMidi_ =
        winui::MakeKeyboardKeymap(kKeyboardBaseMidiNote, kKeyboardOctaveCount);
}

void SatoriAppState::initializePresetSupport() {
    const auto presetDir = GetExecutableDir() / L"presets";
    presetManager_ = std::make_unique<winapp::PresetManager>(presetDir);
    const auto defaultPreset = presetManager_->defaultPresetPath();
    if (std::filesystem::exists(defaultPreset)) {
        std::wstring error;
        if (presetManager_->load(defaultPreset, synthConfig_, masterGain_, ampRelease_, error)) {
            syncSynthConfig();
            if (d2d_) {
                d2d_->syncSliders();
            }
            refreshWaveformPreview();
            updatePresetStatus(L"预设：" + defaultPreset.filename().wstring());
        } else if (window_) {
            MessageBoxW(window_, error.c_str(), kWindowTitle,
                        MB_ICONWARNING | MB_OK);
        }
    }
}

void SatoriAppState::handleLoadPreset() {
    if (!presetManager_) {
        return;
    }
    auto path = presetManager_->userPresetPath();
    if (!std::filesystem::exists(path)) {
        path = presetManager_->defaultPresetPath();
    }
    if (!std::filesystem::exists(path)) {
        MessageBoxW(window_, L"未找到可用预设文件", kWindowTitle,
                    MB_ICONWARNING | MB_OK);
        return;
    }
    std::wstring error;
    float loadedGain = masterGain_;
    float loadedRelease = ampRelease_;
    if (!presetManager_->load(path, synthConfig_, loadedGain, loadedRelease, error)) {
        MessageBoxW(window_, error.c_str(), kWindowTitle,
                    MB_ICONWARNING | MB_OK);
        return;
    }
    masterGain_ = loadedGain;
    ampRelease_ = loadedRelease;
    syncSynthConfig();
    if (d2d_) {
        d2d_->syncSliders();
    }
    refreshWaveformPreview();
    updatePresetStatus(L"预设：" + path.filename().wstring());
}

void SatoriAppState::handleSavePreset() {
    if (!presetManager_) {
        return;
    }
    std::wstring error;
    const auto path = presetManager_->userPresetPath();
    if (!presetManager_->save(path, synthConfig_, masterGain_, ampRelease_, error)) {
        MessageBoxW(window_, error.c_str(), kWindowTitle,
                    MB_ICONWARNING | MB_OK);
        return;
    }
    updatePresetStatus(L"预设：已保存到 " + path.filename().wstring());
}

bool SatoriAppState::handleMidiKeyDown(UINT vk, LPARAM lparam) {
    if ((lparam & (1 << 30)) != 0) {  // autorepeat
        return virtualKeyToMidi_.find(vk) != virtualKeyToMidi_.end();
    }
    auto it = virtualKeyToMidi_.find(vk);
    if (it == virtualKeyToMidi_.end()) {
        return false;
    }
    if (activeVirtualKeys_.count(vk)) {
        return true;
    }
    const int midi = it->second;
    if (d2d_ && d2d_->pressKeyboardKey(midi)) {
        activeVirtualKeys_.emplace(vk, midi);
        if (window_) {
            InvalidateRect(window_, nullptr, FALSE);
        }
        return true;
    }
    return false;
}

bool SatoriAppState::handleMidiKeyUp(UINT vk) {
    auto it = activeVirtualKeys_.find(vk);
    if (it == activeVirtualKeys_.end()) {
        return false;
    }
    if (d2d_) {
        d2d_->releaseKeyboardKey(it->second);
    }
    activeVirtualKeys_.erase(it);
    if (window_) {
        InvalidateRect(window_, nullptr, FALSE);
    }
    return true;
}

void SatoriAppState::releaseAllVirtualKeys() {
    if (engine_ && audioReady_) {
        for (const auto& kv : activeVirtualKeys_) {
            engine_->noteOff(kv.second);
        }
    }
    if (d2d_) {
        d2d_->releaseAllKeyboardKeys();
    }
    activeVirtualKeys_.clear();
    if (window_) {
        InvalidateRect(window_, nullptr, FALSE);
    }
}

void SatoriAppState::onSize(int width, int height) {
    if (d2d_) {
        d2d_->resize(width, height);
    }
}

void SatoriAppState::onPaint() {
    if (d2d_) {
        d2d_->render();
    }
}

bool SatoriAppState::onKeyDown(UINT vk, LPARAM lparam) {
#if SATORI_UI_DEBUG_ENABLED
    if (vk == VK_F12) {
        if (d2d_) {
            d2d_->toggleDebugOverlay();
            InvalidateRect(window_, nullptr, FALSE);
        }
        return true;
    }
#endif
    if (vk == VK_F11) {
        if (d2d_) {
            d2d_->dumpLayoutDebugInfo();
        }
        return true;
    }
    return handleMidiKeyDown(vk, lparam);
}

bool SatoriAppState::onKeyUp(UINT vk) {
    return handleMidiKeyUp(vk);
}

bool SatoriAppState::onPointerDown(float x, float y) {
    if (d2d_ && d2d_->onPointerDown(x, y)) {
        if (d2d_->hasPointerCapture()) {
            SetCapture(window_);
        }
        InvalidateRect(window_, nullptr, FALSE);
        return true;
    }
    return false;
}

bool SatoriAppState::onPointerMove(float x, float y) {
#if SATORI_UI_DEBUG_ENABLED
    if (!trackingMouseLeave_) {
        TRACKMOUSEEVENT tme{};
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = window_;
        if (TrackMouseEvent(&tme)) {
            trackingMouseLeave_ = true;
        }
    }
#endif
    if (d2d_ && d2d_->onPointerMove(x, y)) {
        InvalidateRect(window_, nullptr, FALSE);
        return true;
    }
    return false;
}

void SatoriAppState::onPointerUp() {
    if (d2d_) {
        d2d_->onPointerUp();
    }
    ReleaseCapture();
    InvalidateRect(window_, nullptr, FALSE);
}

#if SATORI_UI_DEBUG_ENABLED
void SatoriAppState::onMouseLeave() {
    trackingMouseLeave_ = false;
    if (d2d_ && d2d_->onPointerLeave()) {
        InvalidateRect(window_, nullptr, FALSE);
    }
}
#endif

void SatoriAppState::onDeactivate() {
    releaseAllVirtualKeys();
    if (d2d_) {
        d2d_->onPointerUp();
    }
}

SatoriAppState* GetAppState(HWND hwnd) {
    return reinterpret_cast<SatoriAppState*>(
        GetWindowLongPtr(hwnd, GWLP_USERDATA));
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    auto* state = GetAppState(hwnd);
    switch (msg) {
        case WM_CREATE: {
            auto* newState = new SatoriAppState();
            SetWindowLongPtr(hwnd, GWLP_USERDATA,
                             reinterpret_cast<LONG_PTR>(newState));
            if (!newState->initialize(hwnd)) {
                newState->shutdown();
                delete newState;
                SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
            }
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
            if (state) {
                state->onSize(LOWORD(lparam), HIWORD(lparam));
            }
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            if (state) {
                state->onPaint();
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_KEYDOWN: {
            if (state &&
                state->onKeyDown(static_cast<UINT>(wparam), lparam)) {
                return 0;
            }
            break;
        }
        case WM_KEYUP: {
            if (state && state->onKeyUp(static_cast<UINT>(wparam))) {
                return 0;
            }
            break;
        }
        case WM_LBUTTONDOWN: {
            if (state &&
                state->onPointerDown(static_cast<float>(GET_X_LPARAM(lparam)),
                                     static_cast<float>(GET_Y_LPARAM(lparam)))) {
                return 0;
            }
            break;
        }
        case WM_MOUSEMOVE: {
            if (state &&
                state->onPointerMove(static_cast<float>(GET_X_LPARAM(lparam)),
                                     static_cast<float>(GET_Y_LPARAM(lparam)))) {
                return 0;
            }
            break;
        }
        case WM_LBUTTONUP: {
            if (state) {
                state->onPointerUp();
            }
            return 0;
        }
        case WM_MOUSELEAVE: {
#if SATORI_UI_DEBUG_ENABLED
            if (state) {
                state->onMouseLeave();
            }
#endif
            return 0;
        }
        case WM_KILLFOCUS:
        case WM_CANCELMODE:
        case WM_ACTIVATE: {
            if (msg != WM_ACTIVATE || LOWORD(wparam) == WA_INACTIVE) {
                if (state) {
                    state->onDeactivate();
                }
            }
            break;
        }
        case WM_DESTROY:
            if (state) {
                state->shutdown();
                delete state;
                SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
            }
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
