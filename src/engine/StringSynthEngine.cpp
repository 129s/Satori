#include "engine/StringSynthEngine.h"

#include <algorithm>
#include <iterator>

namespace engine {

StringSynthEngine::StringSynthEngine(synthesis::StringConfig config)
    : config_(config) {}

void StringSynthEngine::setConfig(const synthesis::StringConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
}

synthesis::StringConfig StringSynthEngine::config() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

void StringSynthEngine::enqueueEvent(const Event& event) {
    std::lock_guard<std::mutex> lock(mutex_);
    eventQueue_.push_back(event);
}

void StringSynthEngine::noteOn(double frequency, double durationSeconds) {
    Event event;
    event.type = EventType::NoteOn;
    event.frequency = frequency;
    event.durationSeconds = durationSeconds;
    enqueueEvent(event);
}

void StringSynthEngine::process(const ProcessBlock& block) {
    if (!block.output || block.frames == 0 || block.channels == 0) {
        return;
    }
    const std::size_t totalSamples = block.frames * block.channels;
    std::fill(block.output, block.output + totalSamples, 0.0f);

    std::vector<Event> events;
    std::vector<Voice> localVoices;
    synthesis::StringConfig currentConfig{};
    {
        std::lock_guard<std::mutex> lock(mutex_);
        currentConfig = config_;
        events.swap(eventQueue_);
        localVoices.swap(voices_);
    }

    for (const auto& event : events) {
        handleEvent(event, currentConfig, localVoices);
    }

    mixVoices(block, localVoices);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        config_ = currentConfig;
        if (!localVoices.empty()) {
            voices_.insert(voices_.end(),
                           std::make_move_iterator(localVoices.begin()),
                           std::make_move_iterator(localVoices.end()));
        }
    }
}

void StringSynthEngine::handleEvent(const Event& event,
                                    synthesis::StringConfig& config,
                                    std::vector<Voice>& voices) {
    switch (event.type) {
        case EventType::NoteOn:
            handleNoteOn(event.frequency, event.durationSeconds, config, voices);
            break;
        case EventType::ParamChange:
            config = event.params;
            break;
        default:
            break;
    }
}

void StringSynthEngine::handleNoteOn(double frequency, double durationSeconds,
                                     const synthesis::StringConfig& config,
                                     std::vector<Voice>& voices) {
    if (frequency <= 0.0 || durationSeconds <= 0.0 ||
        config.sampleRate <= 0.0) {
        return;
    }
    synthesis::KarplusStrongString string(config);
    auto samples = string.pluck(frequency, durationSeconds);
    if (samples.empty()) {
        return;
    }
    voices.push_back(Voice{std::move(samples), 0});
}

void StringSynthEngine::mixVoices(const ProcessBlock& block,
                                  std::vector<Voice>& voices) const {
    for (auto& voice : voices) {
        const auto bufferSize = voice.buffer.size();
        for (std::size_t frame = 0; frame < block.frames && voice.cursor < bufferSize;
             ++frame) {
            const float sample = voice.buffer[voice.cursor++];
            for (uint16_t ch = 0; ch < block.channels; ++ch) {
                block.output[frame * block.channels + ch] += sample;
            }
        }
    }

    voices.erase(
        std::remove_if(voices.begin(), voices.end(),
                       [](const Voice& voice) { return voice.cursor >= voice.buffer.size(); }),
        voices.end());
}

}  // namespace engine

