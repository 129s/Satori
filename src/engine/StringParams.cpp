#include "engine/StringParams.h"

#include <algorithm>
#include <cctype>
#include <string>

#include "dsp/RoomIrLibrary.h"

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
    static const std::vector<ParamInfo> kParams = [] {
        const float maxIr =
            std::max(0.0f,
                     static_cast<float>(std::max<std::size_t>(1, dsp::RoomIrLibrary::list().size()) - 1));
        return std::vector<ParamInfo>{
            {ParamId::Decay, "decay", ParamType::Float, 0.90f, 0.999f, 0.996f},
            {ParamId::Brightness, "brightness", ParamType::Float, 0.0f, 1.0f, 0.5f},
            {ParamId::DispersionAmount, "dispersionAmount", ParamType::Float, 0.0f, 1.0f,
             0.12f},
            {ParamId::ExcitationBrightness, "excitationBrightness", ParamType::Float, 0.0f, 1.0f,
             0.6f},
            {ParamId::ExcitationVelocity, "excitationVelocity", ParamType::Float, 0.0f, 1.0f,
             0.5f},
            {ParamId::ExcitationMix, "excitationMix", ParamType::Float, 0.0f, 1.0f, 1.0f},
            {ParamId::BodyTone, "bodyTone", ParamType::Float, 0.0f, 1.0f, 0.5f},
            {ParamId::BodySize, "bodySize", ParamType::Float, 0.0f, 1.0f, 0.5f},
            {ParamId::RoomAmount, "roomAmount", ParamType::Float, 0.0f, 1.0f, 0.0f},
            // Discrete IR selection for the convolution reverb.
            {ParamId::RoomIR, "roomIR", ParamType::Enum, 0.0f, maxIr, 0.0f},
            {ParamId::PickPosition, "pickPosition", ParamType::Float, 0.05f, 0.95f, 0.5f},
            {ParamId::EnableLowpass, "enableLowpass", ParamType::Bool, 0.0f, 1.0f, 1.0f},
            {ParamId::NoiseType, "noiseType", ParamType::Enum, 0.0f, 1.0f, 0.0f},
            {ParamId::MasterGain, "masterGain", ParamType::Float, 0.0f, 2.0f, 1.0f},
            {ParamId::AmpRelease, "ampRelease", ParamType::Float, 0.01f, 5.0f, 0.35f},
        };
    }();
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
