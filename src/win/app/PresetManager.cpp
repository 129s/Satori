#include "win/app/PresetManager.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <optional>
#include <sstream>

#include "dsp/RoomIrLibrary.h"
#include "engine/StringParams.h"
#include "engine/StringSynthEngine.h"

namespace winapp {

namespace {

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

std::optional<std::string> ExtractValue(const std::string& text,
                                        const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    const auto keyPos = text.find(needle);
    if (keyPos == std::string::npos) {
        return std::nullopt;
    }
    const auto colonPos = text.find(':', keyPos + needle.size());
    if (colonPos == std::string::npos) {
        return std::nullopt;
    }
    auto valueStart = text.find_first_not_of(" \t\r\n", colonPos + 1);
    if (valueStart == std::string::npos) {
        return std::nullopt;
    }
    if (text[valueStart] == '"') {
        auto end = text.find('"', valueStart + 1);
        if (end == std::string::npos) {
            return std::nullopt;
        }
        return text.substr(valueStart + 1, end - valueStart - 1);
    }

    auto valueEnd = text.find_first_of(",}\r\n", valueStart);
    if (valueEnd == std::string::npos) {
        valueEnd = text.size();
    }
    return text.substr(valueStart, valueEnd - valueStart);
}

float ParseFloat(const std::string& raw, bool& ok) {
    try {
        ok = true;
        return static_cast<float>(std::stof(raw));
    } catch (const std::exception&) {
        ok = false;
        return 0.0f;
    }
}

bool ParseBool(const std::string& raw, bool& ok) {
    const auto lower = ToLower(raw);
    if (lower.rfind("true", 0) == 0) {
        ok = true;
        return true;
    }
    if (lower.rfind("false", 0) == 0) {
        ok = true;
        return false;
    }
    ok = false;
    return false;
}

unsigned int ParseUint(const std::string& raw, bool& ok) {
    try {
        ok = true;
        return static_cast<unsigned int>(std::stoul(raw));
    } catch (const std::exception&) {
        ok = false;
        return 0;
    }
}

synthesis::ExcitationMode ParseExcitationMode(const std::string& raw, bool& ok) {
    const auto lower = ToLower(raw);
    ok = true;
    if (lower == "fixed") {
        return synthesis::ExcitationMode::FixedNoisePick;
    }
    if (lower == "random") {
        return synthesis::ExcitationMode::RandomNoisePick;
    }
    ok = false;
    return synthesis::ExcitationMode::RandomNoisePick;
}

synthesis::ExcitationType ParseExcitationType(const std::string& raw, bool& ok) {
    const auto lower = ToLower(raw);
    ok = true;
    if (lower == "pluck") {
        return synthesis::ExcitationType::Pluck;
    }
    if (lower == "hammer") {
        return synthesis::ExcitationType::Hammer;
    }
    ok = false;
    return synthesis::ExcitationType::Pluck;
}

}  // namespace

PresetManager::PresetManager(std::filesystem::path presetDir)
    : presetDir_(std::move(presetDir)) {}

std::filesystem::path PresetManager::defaultPresetPath() const {
    return presetDir_ / "default.json";
}

std::filesystem::path PresetManager::userPresetPath() const {
    return presetDir_ / "user.json";
}

bool PresetManager::load(const std::filesystem::path& path,
                         synthesis::StringConfig& config,
                         float& masterGain,
                         float& ampRelease,
                         std::wstring& errorMessage) const {
    std::ifstream stream(path);
    if (!stream.is_open()) {
        errorMessage = L"Failed to open preset file: " + path.wstring();
        return false;
    }
    std::stringstream buffer;
    buffer << stream.rdbuf();
    return parse(buffer.str(), config, masterGain, ampRelease, errorMessage);
}

bool PresetManager::save(const std::filesystem::path& path,
                         const synthesis::StringConfig& config,
                         float masterGain,
                         float ampRelease,
                         std::wstring& errorMessage) const {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream stream(path, std::ios::binary);
    if (!stream.is_open()) {
        errorMessage = L"Failed to write preset file: " + path.wstring();
        return false;
    }
    stream << serialize(config, masterGain, ampRelease);
    if (!stream.good()) {
        errorMessage = L"Failed to write preset content: " + path.wstring();
        return false;
    }
    return true;
}

bool PresetManager::parse(const std::string& content,
                          synthesis::StringConfig& config,
                          float& masterGain,
                          float& ampRelease,
                          std::wstring& errorMessage) const {
    engine::StringSynthEngine params(config);
    params.setParam(engine::ParamId::MasterGain, masterGain);
    params.setParam(engine::ParamId::AmpRelease, ampRelease);

    bool ok = false;

    auto setFloat = [&](const char* key, engine::ParamId id) -> bool {
        if (auto v = ExtractValue(content, key)) {
            const float value = ParseFloat(*v, ok);
            if (!ok) {
                errorMessage = L"Failed to parse preset float field"; 
                return false;
            }
            params.setParam(id, value);
        }
        return true;
    };

    bool hasWaveEnabled = false;
    bool hasWaveLevel = false;
    bool hasWaveformType = false;
    bool hasWaveDuty = false;
    bool hasNoiseEnabled = false;
    bool hasNoiseLevel = false;
    bool hasNoiseJitter = false;
    bool hasNoiseOverdrive = false;
    bool hasNoiseColor = false;

    auto setFloatTrack = [&](const char* key, engine::ParamId id,
                             bool& flag) -> bool {
        if (auto v = ExtractValue(content, key)) {
            const float value = ParseFloat(*v, ok);
            if (!ok) {
                errorMessage = L"Failed to parse preset float field";
                return false;
            }
            params.setParam(id, value);
            flag = true;
        }
        return true;
    };

    if (!setFloat("decay", engine::ParamId::Decay)) return false;
    if (!setFloat("brightness", engine::ParamId::Brightness)) return false;
    if (!setFloatTrack("waveEnabled", engine::ParamId::WaveEnabled, hasWaveEnabled))
        return false;
    if (!setFloatTrack("waveLevel", engine::ParamId::WaveLevel, hasWaveLevel))
        return false;
    if (!setFloatTrack("waveformType", engine::ParamId::WaveformType, hasWaveformType))
        return false;
    if (!setFloatTrack("waveDuty", engine::ParamId::WaveDuty, hasWaveDuty))
        return false;
    if (!setFloatTrack("noiseEnabled", engine::ParamId::NoiseEnabled, hasNoiseEnabled))
        return false;
    if (!setFloatTrack("noiseLevel", engine::ParamId::NoiseLevel, hasNoiseLevel))
        return false;
    if (!setFloatTrack("noiseJitter", engine::ParamId::NoiseJitter, hasNoiseJitter))
        return false;
    if (!setFloatTrack("noiseOverdrive", engine::ParamId::NoiseOverdrive, hasNoiseOverdrive))
        return false;
    if (!setFloatTrack("noiseColor", engine::ParamId::NoiseColor, hasNoiseColor))
        return false;
    if (!setFloat("dispersionAmount", engine::ParamId::DispersionAmount)) return false;
    if (!setFloat("bodyTone", engine::ParamId::BodyTone)) return false;
    if (!setFloat("bodySize", engine::ParamId::BodySize)) return false;
    if (!setFloat("pickPosition", engine::ParamId::PickPosition)) return false;
    if (!setFloat("masterGain", engine::ParamId::MasterGain)) return false;
    if (!setFloat("ampRelease", engine::ParamId::AmpRelease)) return false;

    // Room mix: prefer new field; fall back to legacy.
    if (auto mix = ExtractValue(content, "roomMix")) {
        const float value = ParseFloat(*mix, ok);
        if (!ok) {
            errorMessage = L"Failed to parse roomMix";
            return false;
        }
        params.setParam(engine::ParamId::RoomAmount, value);
    } else if (auto amount = ExtractValue(content, "roomAmount")) {
        const float value = ParseFloat(*amount, ok);
        if (!ok) {
            errorMessage = L"Failed to parse roomAmount";
            return false;
        }
        params.setParam(engine::ParamId::RoomAmount, value);
    }

    // Room IR (stable ID).
    if (auto ir = ExtractValue(content, "roomIR")) {
        const int idx = dsp::RoomIrLibrary::findIndexById(*ir);
        if (idx >= 0) {
            params.setParam(engine::ParamId::RoomIR, static_cast<float>(idx));
        }
    }

    if (auto lowpass = ExtractValue(content, "enableLowpass")) {
        const bool value = ParseBool(*lowpass, ok);
        if (!ok) {
            errorMessage = L"Failed to parse enableLowpass";
            return false;
        }
        params.setParam(engine::ParamId::EnableLowpass, value ? 1.0f : 0.0f);
    }

    // Back-compat: legacy excitation fields.
    float legacyExcitationMix = 1.0f;
    bool hasLegacyMix = false;
    if (auto mix = ExtractValue(content, "excitationMix")) {
        legacyExcitationMix = ParseFloat(*mix, ok);
        if (!ok) {
            errorMessage = L"Failed to parse excitationMix";
            return false;
        }
        hasLegacyMix = true;
    }
    if (auto legacyColor = ExtractValue(content, "excitationBrightness")) {
        const float value = ParseFloat(*legacyColor, ok);
        if (!ok) {
            errorMessage = L"Failed to parse excitationBrightness";
            return false;
        }
        if (!hasNoiseColor) {
            params.setParam(engine::ParamId::NoiseColor, value);
            hasNoiseColor = true;
        }
    }
    if (auto noise = ExtractValue(content, "noiseType")) {
        const auto lower = ToLower(*noise);
        if (!hasNoiseOverdrive) {
            params.setParam(engine::ParamId::NoiseOverdrive,
                            lower == "binary" ? 1.0f : 0.0f);
            hasNoiseOverdrive = true;
        }
    }
    if (hasLegacyMix && !hasWaveLevel && !hasNoiseLevel) {
        const float mix01 = std::clamp(legacyExcitationMix, 0.0f, 1.0f);
        params.setParam(engine::ParamId::WaveEnabled, 1.0f);
        params.setParam(engine::ParamId::WaveLevel, 1.0f - mix01);
        params.setParam(engine::ParamId::WaveformType, 4.0f);  // Semisine
        params.setParam(engine::ParamId::WaveDuty, 0.5f);
        params.setParam(engine::ParamId::NoiseEnabled, 1.0f);
        params.setParam(engine::ParamId::NoiseLevel, mix01);
        if (!hasNoiseJitter) {
            params.setParam(engine::ParamId::NoiseJitter, 1.0f);
            hasNoiseJitter = true;
        }
    }

    synthesis::StringConfig parsed = params.stringConfig();

    if (auto excitationMode = ExtractValue(content, "excitationMode")) {
        const auto mode = ParseExcitationMode(*excitationMode, ok);
        if (!ok) {
            errorMessage = L"Failed to parse excitationMode";
            return false;
        }
        parsed.excitationMode = mode;
    }
    if (auto excitationType = ExtractValue(content, "excitationType")) {
        const auto type = ParseExcitationType(*excitationType, ok);
        if (!ok) {
            errorMessage = L"Failed to parse excitationType";
            return false;
        }
        parsed.excitationType = type;
    }
    if (auto seed = ExtractValue(content, "seed")) {
        parsed.seed = ParseUint(*seed, ok);
        if (!ok) {
            errorMessage = L"Failed to parse seed";
            return false;
        }
    }

    config = parsed;
    masterGain = params.getParam(engine::ParamId::MasterGain);
    ampRelease = params.getParam(engine::ParamId::AmpRelease);
    return true;
}

std::string PresetManager::serialize(const synthesis::StringConfig& config,
                                     float masterGain,
                                     float ampRelease) {
    const auto& irList = dsp::RoomIrLibrary::list();
    std::string roomIrId = "small-room";
    if (!irList.empty()) {
        const int idx = std::clamp(config.roomIrIndex, 0,
                                   static_cast<int>(irList.size() - 1));
        roomIrId = std::string(irList[static_cast<std::size_t>(idx)].id);
    }

      std::ostringstream oss;
      oss << "{\n"
          << "  \"decay\": " << config.decay << ",\n"
          << "  \"brightness\": " << config.brightness << ",\n"
          << "  \"waveEnabled\": " << (config.waveEnabled ? 1 : 0) << ",\n"
          << "  \"waveLevel\": " << config.waveLevel << ",\n"
          << "  \"waveformType\": " << static_cast<int>(config.waveformType) << ",\n"
          << "  \"waveDuty\": " << config.waveDuty << ",\n"
          << "  \"noiseEnabled\": " << (config.noiseEnabled ? 1 : 0) << ",\n"
          << "  \"noiseLevel\": " << config.noiseLevel << ",\n"
          << "  \"noiseJitter\": " << config.noiseJitter << ",\n"
          << "  \"noiseOverdrive\": " << config.noiseOverdrive << ",\n"
          << "  \"noiseColor\": " << config.noiseColor << ",\n"
          << "  \"dispersionAmount\": " << config.dispersionAmount << ",\n"
          << "  \"bodyTone\": " << config.bodyTone << ",\n"
          << "  \"bodySize\": " << config.bodySize << ",\n"
          << "  \"roomMix\": " << config.roomAmount << ",\n"
          << "  \"roomIR\": \"" << roomIrId << "\",\n"
          // Legacy field for older presets/tools.
          << "  \"roomAmount\": " << config.roomAmount << ",\n"
          << "  \"pickPosition\": " << config.pickPosition << ",\n"
          << "  \"enableLowpass\": " << (config.enableLowpass ? "true" : "false") << ",\n"
          << "  \"excitationType\": \""
          << (config.excitationType == synthesis::ExcitationType::Hammer ? "hammer" : "pluck")
          << "\",\n"
        << "  \"excitationMode\": \""
        << (config.excitationMode == synthesis::ExcitationMode::FixedNoisePick ? "fixed" : "random")
        << "\",\n"
        << "  \"seed\": " << config.seed << ",\n"
        << "  \"masterGain\": " << masterGain << ",\n"
        << "  \"ampRelease\": " << ampRelease << "\n"
        << "}\n";
    return oss.str();
}

}  // namespace winapp
