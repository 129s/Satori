#include "engine/StringParams.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace engine {

namespace {

std::string ToLower(std::string_view value) {
    std::string lowered(value);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return lowered;
}

}  // namespace

const std::vector<ParamInfo>& GetParamInfoList() {
    static const std::vector<ParamInfo> kParams = {
        {ParamId::Decay, "decay", ParamType::Float, 0.90f, 0.999f, 0.996f},
        {ParamId::Brightness, "brightness", ParamType::Float, 0.0f, 1.0f, 0.5f},
        {ParamId::PickPosition, "pickPosition", ParamType::Float, 0.05f, 0.95f, 0.5f},
        {ParamId::EnableLowpass, "enableLowpass", ParamType::Bool, 0.0f, 1.0f, 1.0f},
        {ParamId::NoiseType, "noiseType", ParamType::Enum, 0.0f, 1.0f, 0.0f},
        {ParamId::MasterGain, "masterGain", ParamType::Float, 0.0f, 2.0f, 1.0f},
        {ParamId::AmpRelease, "ampRelease", ParamType::Float, 0.01f, 5.0f, 0.35f},
    };
    return kParams;
}

const ParamInfo* GetParamInfo(ParamId id) {
    const auto& params = GetParamInfoList();
    auto it = std::find_if(params.begin(), params.end(),
                           [id](const ParamInfo& info) { return info.id == id; });
    if (it == params.end()) {
        return nullptr;
    }
    return &(*it);
}

const ParamInfo* FindParamByName(std::string_view name) {
    const auto lowered = ToLower(name);
    const auto& params = GetParamInfoList();
    auto it = std::find_if(params.begin(), params.end(),
                           [&lowered](const ParamInfo& info) {
                               return lowered == ToLower(info.name);
                           });
    if (it == params.end()) {
        return nullptr;
    }
    return &(*it);
}

float ClampToRange(const ParamInfo& info, float value) {
    return std::max(info.minValue, std::min(info.maxValue, value));
}

}  // namespace engine
