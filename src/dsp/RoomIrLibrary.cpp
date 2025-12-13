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
            info.channels = built.items[i].channels;
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

RoomIrLibrary::Samples RoomIrLibrary::samples(int index) {
    const auto built = BuiltIns();
    if (index < 0 || static_cast<std::size_t>(index) >= built.count) {
        return {};
    }
    const auto& it = built.items[static_cast<std::size_t>(index)];
    Samples out;
    out.sampleRate = it.sampleRate;
    out.channels = it.channels;
    out.left = it.samplesL;
    out.right = it.samplesR;
    out.frameCount = it.frameCount;
    return out;
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
    if (outCount) *outCount = it.frameCount;
    if (outSampleRate) *outSampleRate = it.sampleRate;
    return it.samplesL;
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
        const auto s = samples(index);
        if (!s.left || s.frameCount == 0) {
            return {};
        }
        const std::size_t outCount = std::min(maxSamples, s.frameCount);
        std::vector<float> out;
        out.reserve(outCount);
        const bool stereo = (s.channels == 2 && s.right);
        if (outCount == s.frameCount) {
            if (!stereo) {
                out.assign(s.left, s.left + s.frameCount);
                return out;
            }
            for (std::size_t i = 0; i < s.frameCount; ++i) {
                out.push_back(0.5f * (s.left[i] + s.right[i]));
            }
            return out;
        }
        const float step =
            static_cast<float>(s.frameCount - 1) / static_cast<float>(outCount - 1);
        for (std::size_t i = 0; i < outCount; ++i) {
            const std::size_t idx =
                std::min(s.frameCount - 1,
                         static_cast<std::size_t>(std::lround(step * static_cast<float>(i))));
            if (!stereo) {
                out.push_back(s.left[idx]);
            } else {
                out.push_back(0.5f * (s.left[idx] + s.right[idx]));
            }
        }
        return out;
    }

    const std::size_t n = std::min(maxSamples, it.previewCount);
    return std::vector<float>(it.preview, it.preview + n);
}

}  // namespace dsp
