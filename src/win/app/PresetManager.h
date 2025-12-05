#pragma once

#include <filesystem>
#include <string>

#include "synthesis/KarplusStrongString.h"

namespace winapp {

class PresetManager {
public:
    explicit PresetManager(std::filesystem::path presetDir);

    std::filesystem::path defaultPresetPath() const;
    std::filesystem::path userPresetPath() const;

    bool load(const std::filesystem::path& path,
              synthesis::StringConfig& config,
              float& masterGain,
              float& ampRelease,
              std::wstring& errorMessage) const;

    bool save(const std::filesystem::path& path,
              const synthesis::StringConfig& config,
              float masterGain,
              float ampRelease,
              std::wstring& errorMessage) const;

    const std::filesystem::path& root() const { return presetDir_; }

private:
    bool parse(const std::string& content,
               synthesis::StringConfig& config,
               float& masterGain,
               float& ampRelease,
               std::wstring& errorMessage) const;
    static std::string serialize(const synthesis::StringConfig& config,
                                 float masterGain,
                                 float ampRelease);

    std::filesystem::path presetDir_;
};

}  // namespace winapp
