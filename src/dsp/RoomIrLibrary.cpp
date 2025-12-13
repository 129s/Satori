#include "dsp/RoomIrLibrary.h"

#include <algorithm>
#include <cmath>

#include "room_ir/RoomIrData.h"

namespace dsp {

namespace {

struct BuiltInsView {
    const room_ir::Item* items = nullptr;
    std::size_t count = 0;
};

BuiltInsView BuiltIns() {
    BuiltInsView v;
    v.items = room_ir::items(&v.count);
    return v;
}

}  // namespace

const std::vector<RoomIrInfo>& RoomIrLibrary::list() {
    static const std::vector<RoomIrInfo> kList = [] {
        const auto built = BuiltIns();
        std::vector<RoomIrInfo> out;
        out.reserve(built.count);
        for (std::size_t i = 0; i < built.count; ++i) {
            RoomIrInfo info;
            info.id = built.items[i].id;
            info.displayName = built.items[i].displayName;
            info.sampleRate = built.items[i].sampleRate;
            out.push_back(info);
        }
        return out;
    }();
    return kList;
}

int RoomIrLibrary::findIndexById(std::string_view id) {
    const auto& l = list();
    for (std::size_t i = 0; i < l.size(); ++i) {
        if (l[i].id == id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

const float* RoomIrLibrary::samplesMono(int index,
                                        std::size_t* outCount,
                                        int* outSampleRate) {
    const auto built = BuiltIns();
    if (index < 0 || static_cast<std::size_t>(index) >= built.count) {
        if (outCount) *outCount = 0;
        if (outSampleRate) *outSampleRate = 0;
        return nullptr;
    }
    const auto& it = built.items[static_cast<std::size_t>(index)];
    if (outCount) *outCount = it.sampleCount;
    if (outSampleRate) *outSampleRate = it.sampleRate;
    return it.samples;
}

std::vector<float> RoomIrLibrary::previewMono(int index, std::size_t maxSamples) {
    if (maxSamples == 0) {
        return {};
    }
    const auto built = BuiltIns();
    if (index < 0 || static_cast<std::size_t>(index) >= built.count) {
        return {};
    }

    const auto& it = built.items[static_cast<std::size_t>(index)];
    if (!it.preview || it.previewCount == 0) {
        // Fallback: downsample raw samples if preview wasn't generated.
        std::size_t count = 0;
        int sr = 0;
        const float* s = samplesMono(index, &count, &sr);
        (void)sr;
        if (!s || count == 0) {
            return {};
        }
        const std::size_t outCount = std::min(maxSamples, count);
        std::vector<float> out;
        out.reserve(outCount);
        if (outCount == count) {
            out.assign(s, s + count);
            return out;
        }
        const float step = static_cast<float>(count - 1) / static_cast<float>(outCount - 1);
        for (std::size_t i = 0; i < outCount; ++i) {
            const std::size_t idx =
                std::min(count - 1,
                         static_cast<std::size_t>(std::lround(step * static_cast<float>(i))));
            out.push_back(s[idx]);
        }
        return out;
    }

    const std::size_t n = std::min(maxSamples, it.previewCount);
    return std::vector<float>(it.preview, it.preview + n);
}

}  // namespace dsp
