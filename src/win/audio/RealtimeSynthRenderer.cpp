#include "win/audio/RealtimeSynthRenderer.h"

#include <algorithm>
#include <iterator>

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

    std::vector<ActiveVoice> localVoices;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        localVoices.swap(voices_);
    }

    std::vector<ActiveVoice> activeVoices;
    activeVoices.reserve(localVoices.size());

    for (auto& voice : localVoices) {
        const auto bufferSize = voice.buffer.size();
        for (std::size_t frame = 0; frame < frames && voice.cursor < bufferSize; ++frame) {
            const float sample = voice.buffer[voice.cursor++];
            for (uint16_t ch = 0; ch < channels; ++ch) {
                output[frame * channels + ch] += sample;
            }
        }
        if (voice.cursor < bufferSize) {
            activeVoices.push_back(std::move(voice));
        }
    }

    if (!activeVoices.empty()) {
        std::lock_guard<std::mutex> lock(mutex_);
        voices_.insert(voices_.end(),
                       std::make_move_iterator(activeVoices.begin()),
                       std::make_move_iterator(activeVoices.end()));
    }
}

}  // namespace winaudio
