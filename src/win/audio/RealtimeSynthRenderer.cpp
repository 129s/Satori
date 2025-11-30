#include "win/audio/RealtimeSynthRenderer.h"

#include <algorithm>

namespace winaudio {

RealtimeSynthRenderer::RealtimeSynthRenderer(synthesis::StringConfig config)
    : config_(config) {}

void RealtimeSynthRenderer::setConfig(const synthesis::StringConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
}

void RealtimeSynthRenderer::enqueueNote(double frequency, double durationSeconds) {
    synthesis::KarplusStrongString string(config_);
    auto samples = string.pluck(frequency, durationSeconds);
    if (samples.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    voices_.push_back(ActiveVoice{std::move(samples), 0});
}

void RealtimeSynthRenderer::render(float* output, std::size_t frames, uint16_t channels) {
    const std::size_t totalSamples = frames * channels;
    std::fill(output, output + totalSamples, 0.0f);

    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& voice : voices_) {
        for (std::size_t frame = 0; frame < frames; ++frame) {
            if (voice.cursor >= voice.buffer.size()) {
                break;
            }
            const float sample = voice.buffer[voice.cursor++];
            for (uint16_t ch = 0; ch < channels; ++ch) {
                output[frame * channels + ch] += sample;
            }
        }
    }

    voices_.erase(
        std::remove_if(voices_.begin(), voices_.end(),
                       [](const ActiveVoice& voice) { return voice.cursor >= voice.buffer.size(); }),
        voices_.end());
}

}  // namespace winaudio
