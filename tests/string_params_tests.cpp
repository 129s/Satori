#include <catch2/catch_amalgamated.hpp>

#include "engine/StringParams.h"
#include "engine/StringSynthEngine.h"

TEST_CASE("参数名称查找大小写不敏感", "[engine-params]") {
    const auto* decay = engine::FindParamByName("DECAY");
    REQUIRE(decay != nullptr);
    REQUIRE(decay->id == engine::ParamId::Decay);

    const auto* waveformType = engine::FindParamByName("waveformType");
    REQUIRE(waveformType != nullptr);
    REQUIRE(waveformType->id == engine::ParamId::WaveformType);

    const auto* dispersion = engine::FindParamByName("DISPERSIONamount");
    REQUIRE(dispersion != nullptr);
    REQUIRE(dispersion->id == engine::ParamId::DispersionAmount);

    const auto* noiseColor = engine::FindParamByName("noiseColor");
    REQUIRE(noiseColor != nullptr);
    REQUIRE(noiseColor->id == engine::ParamId::NoiseColor);

    const auto* waveEnabled = engine::FindParamByName("waveEnabled");
    REQUIRE(waveEnabled != nullptr);
    REQUIRE(waveEnabled->id == engine::ParamId::WaveEnabled);

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
    synth.setParam(engine::ParamId::WaveEnabled, 2.0f);
    synth.setParam(engine::ParamId::WaveLevel, -1.0f);
    synth.setParam(engine::ParamId::WaveformType, 99.0f);
    synth.setParam(engine::ParamId::WaveDuty, 2.0f);
    synth.setParam(engine::ParamId::NoiseEnabled, -1.0f);
    synth.setParam(engine::ParamId::NoiseLevel, 2.0f);
    synth.setParam(engine::ParamId::NoiseJitter, -1.0f);
    synth.setParam(engine::ParamId::NoiseOverdrive, 2.0f);
    synth.setParam(engine::ParamId::NoiseColor, -1.0f);
    synth.setParam(engine::ParamId::BodyTone, 2.0f);
    synth.setParam(engine::ParamId::BodySize, -2.0f);
    synth.setParam(engine::ParamId::RoomAmount, 5.0f);
    synth.setParam(engine::ParamId::PickPosition, 0.0f);
    synth.setParam(engine::ParamId::EnableLowpass, 0.0f);
    synth.setParam(engine::ParamId::MasterGain, 3.0f);

    const auto& config = synth.stringConfig();

    const auto* decayInfo = engine::GetParamInfo(engine::ParamId::Decay);
    const auto* brightnessInfo = engine::GetParamInfo(engine::ParamId::Brightness);
    const auto* dispersionInfo =
        engine::GetParamInfo(engine::ParamId::DispersionAmount);
    const auto* waveEnabledInfo = engine::GetParamInfo(engine::ParamId::WaveEnabled);
    const auto* waveLevelInfo = engine::GetParamInfo(engine::ParamId::WaveLevel);
    const auto* waveformTypeInfo =
        engine::GetParamInfo(engine::ParamId::WaveformType);
    const auto* waveDutyInfo = engine::GetParamInfo(engine::ParamId::WaveDuty);
    const auto* noiseEnabledInfo = engine::GetParamInfo(engine::ParamId::NoiseEnabled);
    const auto* noiseLevelInfo = engine::GetParamInfo(engine::ParamId::NoiseLevel);
    const auto* noiseJitterInfo = engine::GetParamInfo(engine::ParamId::NoiseJitter);
    const auto* noiseOverdriveInfo =
        engine::GetParamInfo(engine::ParamId::NoiseOverdrive);
    const auto* noiseColorInfo = engine::GetParamInfo(engine::ParamId::NoiseColor);
    const auto* bodyToneInfo = engine::GetParamInfo(engine::ParamId::BodyTone);
    const auto* bodySizeInfo = engine::GetParamInfo(engine::ParamId::BodySize);
    const auto* roomInfo = engine::GetParamInfo(engine::ParamId::RoomAmount);
    const auto* pickInfo = engine::GetParamInfo(engine::ParamId::PickPosition);
    const auto* gainInfo = engine::GetParamInfo(engine::ParamId::MasterGain);

    REQUIRE(config.decay == Catch::Approx(decayInfo->maxValue));
    REQUIRE(config.brightness == Catch::Approx(brightnessInfo->minValue));
    REQUIRE(config.dispersionAmount == Catch::Approx(dispersionInfo->maxValue));
    REQUIRE(config.waveEnabled == (waveEnabledInfo->maxValue >= 0.5f));
    REQUIRE(config.waveLevel == Catch::Approx(waveLevelInfo->minValue));
    REQUIRE(static_cast<float>(config.waveformType) ==
            Catch::Approx(waveformTypeInfo->maxValue));
    REQUIRE(config.waveDuty == Catch::Approx(waveDutyInfo->maxValue));
    REQUIRE(config.noiseEnabled == (noiseEnabledInfo->minValue >= 0.5f));
    REQUIRE(config.noiseLevel == Catch::Approx(noiseLevelInfo->maxValue));
    REQUIRE(config.noiseJitter == Catch::Approx(noiseJitterInfo->minValue));
    REQUIRE(config.noiseOverdrive == Catch::Approx(noiseOverdriveInfo->maxValue));
    REQUIRE(config.noiseColor == Catch::Approx(noiseColorInfo->minValue));
    REQUIRE(config.bodyTone == Catch::Approx(bodyToneInfo->maxValue));
    REQUIRE(config.bodySize == Catch::Approx(bodySizeInfo->minValue));
    REQUIRE(config.roomAmount == Catch::Approx(roomInfo->maxValue));
    REQUIRE(config.pickPosition == Catch::Approx(pickInfo->minValue));
    REQUIRE(config.enableLowpass == false);
    REQUIRE(synth.getParam(engine::ParamId::MasterGain) ==
            Catch::Approx(gainInfo->maxValue));
}
