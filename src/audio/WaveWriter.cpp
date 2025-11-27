#include "audio/WaveWriter.h"

#include <algorithm>
#include <fstream>
#include <limits>

namespace audio {

namespace {
struct WaveHeader {
    char chunkId[4] = {'R', 'I', 'F', 'F'};
    uint32_t chunkSize = 0;
    char format[4] = {'W', 'A', 'V', 'E'};

    char subchunk1Id[4] = {'f', 'm', 't', ' '};
    uint32_t subchunk1Size = 16;
    uint16_t audioFormat = 1;  // PCM
    uint16_t numChannels = 1;
    uint32_t sampleRate = 44100;
    uint32_t byteRate = 0;
    uint16_t blockAlign = 0;
    uint16_t bitsPerSample = 16;

    char subchunk2Id[4] = {'d', 'a', 't', 'a'};
    uint32_t subchunk2Size = 0;
};
}  // namespace

bool WaveWriter::write(const std::filesystem::path& path,
                       const std::vector<float>& samples,
                       const WaveFormat& format,
                       std::string& errorMessage) const {
    errorMessage.clear();

    auto pcm = quantize(samples);
    WaveHeader header{};
    header.numChannels = format.channels;
    header.sampleRate = format.sampleRate;
    header.bitsPerSample = format.bitsPerSample;
    const uint16_t bytesPerSample = header.bitsPerSample / 8;
    header.blockAlign = header.numChannels * bytesPerSample;
    header.byteRate = header.sampleRate * header.blockAlign;
    header.subchunk2Size = static_cast<uint32_t>(pcm.size() * bytesPerSample);
    header.chunkSize = 36 + header.subchunk2Size;

    std::ofstream stream(path, std::ios::binary);
    if (!stream.is_open()) {
        errorMessage = "无法打开输出文件: " + path.string();
        return false;
    }

    stream.write(reinterpret_cast<const char*>(&header), sizeof(header));
    stream.write(reinterpret_cast<const char*>(pcm.data()),
                 static_cast<std::streamsize>(pcm.size() * sizeof(int16_t)));

    if (!stream.good()) {
        errorMessage = "写入 WAV 文件失败: " + path.string();
        return false;
    }

    return true;
}

std::vector<int16_t> WaveWriter::quantize(
    const std::vector<float>& samples) {
    std::vector<int16_t> pcm(samples.size());
    constexpr float kMax = static_cast<float>(std::numeric_limits<int16_t>::max());

    std::transform(samples.begin(), samples.end(), pcm.begin(),
                   [](float sample) {
                       sample = std::clamp(sample, -1.0f, 1.0f);
                       return static_cast<int16_t>(sample * kMax);
                   });
    return pcm;
}

}  // namespace audio
