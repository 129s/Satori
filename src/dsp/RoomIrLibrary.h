#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace dsp {

struct RoomIrInfo {
    std::string_view id;           // stable ID for presets
    std::string_view displayName;  // user-facing name
    int sampleRate = 44100;
    int channels = 1;             // 1=mono, 2=stereo
};

// Built-in IRs compiled into the program (no runtime file IO).
class RoomIrLibrary {
public:
    struct Samples {
        int sampleRate = 0;
        int channels = 0;  // 1 or 2
        const float* left = nullptr;
        const float* right = nullptr;  // null if mono
        std::size_t frameCount = 0;
    };

    // Stable list of available IRs.
    static const std::vector<RoomIrInfo>& list();

    // Returns -1 if not found.
    static int findIndexById(std::string_view id);

    // Returns raw IR samples in [-1,1] (non-interleaved).
    // The returned pointer remains valid for the program lifetime.
    static Samples samples(int index);

    // Compatibility: returns the left channel (or mono).
    static const float* samplesMono(int index, std::size_t* outCount, int* outSampleRate);

    // Returns a downsampled preview (<= maxSamples) normalized to [-1,1].
    static std::vector<float> previewMono(int index, std::size_t maxSamples);
};

}  // namespace dsp
