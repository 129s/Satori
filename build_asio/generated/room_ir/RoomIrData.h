#pragma once

#include <cstddef>

namespace dsp::room_ir {

struct Item {
    const char* id;
    const char* displayName;
    int sampleRate;
    int channels;
    const float* samplesL;
    const float* samplesR;
    std::size_t frameCount;
    const float* preview;
    std::size_t previewCount;
};

const Item* items(std::size_t* outCount);

}  // namespace dsp::room_ir
