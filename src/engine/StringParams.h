#pragma once

#include <string_view>
#include <vector>

namespace engine {

enum class ParamId {
    Decay,
    Brightness,
    DispersionAmount,
    ExcitationBrightness,
    ExcitationVelocity,
    ExcitationMix,
    BodyTone,
    BodySize,
    RoomAmount,
    RoomIR,
    PickPosition,
    EnableLowpass,
    NoiseType,
    MasterGain,
    AmpRelease,
};

enum class ParamType { Float, Bool, Enum };

struct ParamInfo {
    ParamId id;
    const char* name;
    ParamType type;
    float minValue;
    float maxValue;
    float defaultValue;
};

const std::vector<ParamInfo>& GetParamInfoList();
const ParamInfo* GetParamInfo(ParamId id);
const ParamInfo* FindParamByName(std::string_view name);
float ClampToRange(const ParamInfo& info, float value);

}  // namespace engine
