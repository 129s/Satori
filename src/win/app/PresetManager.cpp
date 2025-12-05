#include "win/app/PresetManager.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <optional>
#include <sstream>

#include "engine/StringParams.h"
#include "engine/StringSynthEngine.h"

namespace winapp {

namespace {

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

std::optional<std::string> ExtractValue(const std::string& text, const std::string& key) {
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
                         std::wstring& errorMessage) const {
    std::ifstream stream(path);
    if (!stream.is_open()) {
        errorMessage = L"无法打开预设文件: " + path.wstring();
        return false;
    }
    std::stringstream buffer;
    buffer << stream.rdbuf();
    return parse(buffer.str(), config, masterGain, errorMessage);
}

bool PresetManager::save(const std::filesystem::path& path,
                         const synthesis::StringConfig& config,
                         float masterGain,
                         std::wstring& errorMessage) const {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream stream(path, std::ios::binary);
    if (!stream.is_open()) {
        errorMessage = L"无法写入预设文件: " + path.wstring();
        return false;
    }
    stream << serialize(config, masterGain);
    if (!stream.good()) {
        errorMessage = L"写入预设内容失败: " + path.wstring();
        return false;
    }
    return true;
}

bool PresetManager::parse(const std::string& content,
                          synthesis::StringConfig& config,
                          float& masterGain,
                          std::wstring& errorMessage) const {
    engine::StringSynthEngine params(config);
    params.setParam(engine::ParamId::MasterGain, masterGain);

    bool ok = false;
    if (auto decay = ExtractValue(content, "decay")) {
        const float value = ParseFloat(*decay, ok);
        if (!ok) {
            errorMessage = L"解析 decay 失败";
            return false;
        }
        params.setParam(engine::ParamId::Decay, value);
    }
    if (auto brightness = ExtractValue(content, "brightness")) {
        const float value = ParseFloat(*brightness, ok);
        if (!ok) {
            errorMessage = L"解析 brightness 失败";
            return false;
        }
        params.setParam(engine::ParamId::Brightness, value);
    }
    if (auto pickPos = ExtractValue(content, "pickPosition")) {
        const float value = ParseFloat(*pickPos, ok);
        if (!ok) {
            errorMessage = L"解析 pickPosition 失败";
            return false;
        }
        params.setParam(engine::ParamId::PickPosition, value);
    }
    if (auto lowpass = ExtractValue(content, "enableLowpass")) {
        const bool value = ParseBool(*lowpass, ok);
        if (!ok) {
            errorMessage = L"解析 enableLowpass 失败";
            return false;
        }
        params.setParam(engine::ParamId::EnableLowpass, value ? 1.0f : 0.0f);
    }
    if (auto noise = ExtractValue(content, "noiseType")) {
        auto lower = ToLower(*noise);
        if (lower == "binary") {
            params.setParam(engine::ParamId::NoiseType, 1.0f);
        } else {
            params.setParam(engine::ParamId::NoiseType, 0.0f);
        }
    }
    if (auto gain = ExtractValue(content, "masterGain")) {
        const float value = ParseFloat(*gain, ok);
        if (!ok) {
            errorMessage = L"解析 masterGain 失败";
            return false;
        }
        params.setParam(engine::ParamId::MasterGain, value);
    }

    config = params.stringConfig();
    masterGain = params.getParam(engine::ParamId::MasterGain);
    return true;
}

std::string PresetManager::serialize(const synthesis::StringConfig& config,
                                     float masterGain) {
    std::ostringstream oss;
    oss << "{\n"
        << "  \"decay\": " << config.decay << ",\n"
        << "  \"brightness\": " << config.brightness << ",\n"
        << "  \"pickPosition\": " << config.pickPosition << ",\n"
        << "  \"enableLowpass\": " << (config.enableLowpass ? "true" : "false") << ",\n"
        << "  \"noiseType\": \"" << (config.noiseType == synthesis::NoiseType::Binary ? "binary" : "white")
        << "\",\n"
        << "  \"masterGain\": " << masterGain << "\n"
        << "}\n";
    return oss.str();
}

}  // namespace winapp
