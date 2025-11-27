#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace audio {

struct WaveFormat {
    uint32_t sampleRate = 44100;
    uint16_t bitsPerSample = 16;
    uint16_t channels = 1;
};

class WaveWriter {
public:
    bool write(const std::filesystem::path& path,
               const std::vector<float>& samples,
               const WaveFormat& format,
               std::string& errorMessage) const;

private:
    static std::vector<int16_t> quantize(const std::vector<float>& samples);
};

}  // namespace audio
