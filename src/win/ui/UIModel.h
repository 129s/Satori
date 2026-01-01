#pragma once

#include <functional>
#include <string>
#include <vector>

namespace winui {

enum class UIMode { Play, Internal };

struct SliderDescriptor {
    std::wstring label;
    float min = 0.0f;
    float max = 1.0f;
    std::function<float()> getter;
    std::function<void(float)> setter;
};

struct StatusInfo {
    std::wstring primary;
    std::wstring secondary;
};

struct KeyboardConfig {
    int baseMidiNote = 48;
    int octaveCount = 3;
    bool showLabels = false;
    bool hoverOutline = false;
};

enum class FlowModule {
    kNone = 0,
    kExcitation,
    kString,
    kBody,
    kRoom,
};

  struct FlowDiagramState {
      float decay = 0.0f;
      float brightness = 0.0f;
      float dispersionAmount = 0.0f;
      float pickPosition = 0.0f;

      int waveformType = 0;  // 0..4 (see synthesis::WaveformType).

      float bodyTone = 0.0f;
      float bodySize = 0.0f;
      float roomAmount = 0.0f;
    int roomIrIndex = 0;
    std::vector<float> roomIrPreviewSamples;
    std::vector<float> excitationSamples;  // 激励瞬态/包络预览（用于 Excitation Scope）
    FlowModule highlightedModule = FlowModule::kNone;
};

struct ModuleParamDescriptor {
    std::wstring label;
    float min = 0.0f;
    float max = 1.0f;
    std::function<float()> getter;
    std::function<void(float)> setter;
    FlowModule module = FlowModule::kNone;
    bool isSurfaceParam = false;
};

struct ModuleUI {
    std::wstring title;
    FlowModule module = FlowModule::kNone;
    bool isShared = false;
    std::vector<ModuleParamDescriptor> params;
};

struct DropdownModel {
    std::wstring label;
    std::vector<std::wstring> items;
    int selectedIndex = 0;
    int pageSize = 6;
    std::function<void(int)> onChanged;
};

enum class MidiTransportState { Stopped, Playing, Paused };

struct MidiTransportModel {
    bool available = false;
    MidiTransportState state = MidiTransportState::Stopped;
    std::function<void()> onLoad;
    std::function<void()> onPlay;
    std::function<void()> onPause;
    std::function<void()> onStop;
};

 struct HeaderBarModel {
     std::wstring logoText = L"Satori";
     DropdownModel device;
     DropdownModel midiInput;
     DropdownModel sampleRate;
     DropdownModel bufferFrames;
     MidiTransportModel midi;
 };

struct UIModel {
    std::vector<std::wstring> instructions;
    UIMode mode = UIMode::Play;
    StatusInfo status;
    std::vector<SliderDescriptor> sliders;
    std::vector<ModuleUI> modules;
    KeyboardConfig keyboardConfig;
    std::function<void(int, double, bool, float)> keyCallback;
    std::vector<float> waveformSamples;
    bool audioOnline = false;
    float sampleRate = 0.0f;
    HeaderBarModel headerBar;
    FlowDiagramState diagram;
};

}  // namespace winui
