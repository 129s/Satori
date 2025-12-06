#pragma once

#include <functional>
#include <string>
#include <vector>

namespace winui {

struct ButtonDescriptor {
    std::wstring label;
    std::function<void()> onClick;
};

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

struct FlowDiagramState {
    float decay = 0.0f;
    float brightness = 0.0f;
    float dispersionAmount = 0.0f;
    float pickPosition = 0.0f;
    float bodyTone = 0.0f;
    float roomAmount = 0.0f;
    int noiseType = 0;  // 0 = White, 1 = Binary（或项目内部约定）
};

struct UIModel {
    std::vector<std::wstring> instructions;
    StatusInfo status;
    std::vector<ButtonDescriptor> buttons;
    std::vector<SliderDescriptor> sliders;
    KeyboardConfig keyboardConfig;
    std::function<void(int, double, bool)> keyCallback;
    std::vector<float> waveformSamples;
    bool audioOnline = false;
    float sampleRate = 0.0f;
    FlowDiagramState diagram;
};

}  // namespace winui
