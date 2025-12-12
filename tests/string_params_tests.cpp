#include <catch2/catch_amalgamated.hpp>

#include "engine/StringParams.h"
#include "engine/StringSynthEngine.h"

TEST_CASE("参数名称查找大小写不敏感", "[engine-params]") {
    const auto* decay = engine::FindParamByName("DECAY");
    REQUIRE(decay != nullptr);
    REQUIRE(decay->id == engine::ParamId::Decay);

    const auto* noise = engine::FindParamByName("noiseType");
    REQUIRE(noise != nullptr);
    REQUIRE(noise->id == engine::ParamId::NoiseType);

    const auto* dispersion = engine::FindParamByName("DISPERSIONamount");
    REQUIRE(dispersion != nullptr);
    REQUIRE(dispersion->id == engine::ParamId::DispersionAmount);

    const auto* exciteColor = engine::FindParamByName("excitationBrightness");
    REQUIRE(exciteColor != nullptr);
    REQUIRE(exciteColor->id == engine::ParamId::ExcitationBrightness);

    const auto* exciteMix = engine::FindParamByName("excitationMix");
    REQUIRE(exciteMix != nullptr);
    REQUIRE(exciteMix->id == engine::ParamId::ExcitationMix);

    const auto* bodyTone = engine::FindParamByName("BODYTONE");
    REQUIRE(bodyTone != nullptr);
    REQUIRE(bodyTone->id == engine::ParamId::BodyTone);

    const auto* room = engine::FindParamByName("roomAmount");
    REQUIRE(room != nullptr);
    REQUIRE(room->id == engine::ParamId::RoomAmount);
}

TEST_CASE("StringSynthEngine 参数写入会按范围钳制", "[engine-params]") {
    engine::StringSynthEngine synth;

    synth.setParam(engine::ParamId::Decay, 2.0f);
    synth.setParam(engine::ParamId::Brightness, -1.0f);
    synth.setParam(engine::ParamId::DispersionAmount, 5.0f);
    synth.setParam(engine::ParamId::ExcitationBrightness, 2.0f);
    synth.setParam(engine::ParamId::ExcitationVelocity, -1.0f);
    synth.setParam(engine::ParamId::ExcitationMix, 2.0f);
    synth.setParam(engine::ParamId::BodyTone, 2.0f);
    synth.setParam(engine::ParamId::BodySize, -2.0f);
    synth.setParam(engine::ParamId::RoomAmount, 5.0f);
    synth.setParam(engine::ParamId::PickPosition, 0.0f);
    synth.setParam(engine::ParamId::EnableLowpass, 0.0f);
    synth.setParam(engine::ParamId::NoiseType, 1.0f);
    synth.setParam(engine::ParamId::MasterGain, 3.0f);

    const auto& config = synth.stringConfig();

    const auto* decayInfo = engine::GetParamInfo(engine::ParamId::Decay);
    const auto* brightnessInfo = engine::GetParamInfo(engine::ParamId::Brightness);
    const auto* dispersionInfo =
        engine::GetParamInfo(engine::ParamId::DispersionAmount);
    const auto* exciteColorInfo =
        engine::GetParamInfo(engine::ParamId::ExcitationBrightness);
    const auto* exciteVelInfo =
        engine::GetParamInfo(engine::ParamId::ExcitationVelocity);
    const auto* exciteMixInfo =
        engine::GetParamInfo(engine::ParamId::ExcitationMix);
    const auto* bodyToneInfo = engine::GetParamInfo(engine::ParamId::BodyTone);
    const auto* bodySizeInfo = engine::GetParamInfo(engine::ParamId::BodySize);
    const auto* roomInfo = engine::GetParamInfo(engine::ParamId::RoomAmount);
    const auto* pickInfo = engine::GetParamInfo(engine::ParamId::PickPosition);
    const auto* gainInfo = engine::GetParamInfo(engine::ParamId::MasterGain);

    REQUIRE(config.decay == Catch::Approx(decayInfo->maxValue));
    REQUIRE(config.brightness == Catch::Approx(brightnessInfo->minValue));
    REQUIRE(config.dispersionAmount == Catch::Approx(dispersionInfo->maxValue));
    REQUIRE(config.excitationBrightness == Catch::Approx(exciteColorInfo->maxValue));
    REQUIRE(config.excitationVelocity == Catch::Approx(exciteVelInfo->minValue));
    REQUIRE(config.excitationMix == Catch::Approx(exciteMixInfo->maxValue));
    REQUIRE(config.bodyTone == Catch::Approx(bodyToneInfo->maxValue));
    REQUIRE(config.bodySize == Catch::Approx(bodySizeInfo->minValue));
    REQUIRE(config.roomAmount == Catch::Approx(roomInfo->maxValue));
    REQUIRE(config.pickPosition == Catch::Approx(pickInfo->minValue));
    REQUIRE(config.enableLowpass == false);
    REQUIRE(config.noiseType == synthesis::NoiseType::Binary);
    REQUIRE(synth.getParam(engine::ParamId::MasterGain) ==
            Catch::Approx(gainInfo->maxValue));
}
