#include "engine/StringSynthEngine.h"

#include <algorithm>
#include <iterator>

namespace engine {

StringSynthEngine::StringSynthEngine(synthesis::StringConfig config)
    : config_(config) {}

void StringSynthEngine::setConfig(const synthesis::StringConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_.sampleRate = config.sampleRate;
    applyParamUnlocked(ParamId::Decay, static_cast<float>(config.decay), config_,
                       masterGain_);
    applyParamUnlocked(ParamId::Brightness, config.brightness, config_, masterGain_);
    applyParamUnlocked(ParamId::PickPosition, config.pickPosition, config_, masterGain_);
    applyParamUnlocked(ParamId::EnableLowpass, config.enableLowpass ? 1.0f : 0.0f,
                       config_, masterGain_);
    applyParamUnlocked(ParamId::NoiseType,
                       config.noiseType == synthesis::NoiseType::Binary ? 1.0f : 0.0f,
                       config_, masterGain_);
}

synthesis::StringConfig StringSynthEngine::stringConfig() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

void StringSynthEngine::setSampleRate(double sampleRate) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_.sampleRate = sampleRate;
}

double StringSynthEngine::sampleRate() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_.sampleRate;
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

void StringSynthEngine::setParam(ParamId id, float value) {
    std::lock_guard<std::mutex> lock(mutex_);
    applyParamUnlocked(id, value, config_, masterGain_);
}

float StringSynthEngine::getParam(ParamId id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    switch (id) {
        case ParamId::Decay:
            return static_cast<float>(config_.decay);
        case ParamId::Brightness:
            return config_.brightness;
        case ParamId::PickPosition:
            return config_.pickPosition;
        case ParamId::EnableLowpass:
            return config_.enableLowpass ? 1.0f : 0.0f;
        case ParamId::NoiseType:
            return config_.noiseType == synthesis::NoiseType::Binary ? 1.0f : 0.0f;
        case ParamId::MasterGain:
            return masterGain_;
        default:
            return 0.0f;
    }
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
    float currentMasterGain = 1.0f;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        currentConfig = config_;
        currentMasterGain = masterGain_;
        events.swap(eventQueue_);
        localVoices.swap(voices_);
    }

    for (const auto& event : events) {
        handleEvent(event, currentConfig, localVoices, currentMasterGain);
    }

    mixVoices(block, localVoices, currentMasterGain);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        config_ = currentConfig;
        masterGain_ = currentMasterGain;
        if (!localVoices.empty()) {
            voices_.insert(voices_.end(),
                           std::make_move_iterator(localVoices.begin()),
                           std::make_move_iterator(localVoices.end()));
        }
    }
}

void StringSynthEngine::handleEvent(const Event& event,
                                    synthesis::StringConfig& config,
                                    std::vector<Voice>& voices,
                                    float& masterGain) {
    switch (event.type) {
        case EventType::NoteOn:
            handleNoteOn(event.frequency, event.durationSeconds, config, voices);
            break;
        case EventType::ParamChange:
            applyParamUnlocked(event.param, event.paramValue, config, masterGain);
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
                                  std::vector<Voice>& voices,
                                  float masterGain) const {
    for (auto& voice : voices) {
        const auto bufferSize = voice.buffer.size();
        for (std::size_t frame = 0; frame < block.frames && voice.cursor < bufferSize;
             ++frame) {
            const float sample = voice.buffer[voice.cursor++] * masterGain;
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

void StringSynthEngine::applyParamUnlocked(ParamId id, float value,
                                           synthesis::StringConfig& config,
                                           float& masterGain) {
    const auto* info = GetParamInfo(id);
    if (!info) {
        return;
    }
    const float clamped = ClampToRange(*info, value);
    switch (id) {
        case ParamId::Decay:
            config.decay = clamped;
            break;
        case ParamId::Brightness:
            config.brightness = clamped;
            break;
        case ParamId::PickPosition:
            config.pickPosition = clamped;
            break;
        case ParamId::EnableLowpass:
            config.enableLowpass = clamped >= 0.5f;
            break;
        case ParamId::NoiseType:
            config.noiseType =
                (clamped >= 0.5f) ? synthesis::NoiseType::Binary : synthesis::NoiseType::White;
            break;
        case ParamId::MasterGain:
            masterGain = clamped;
            break;
        default:
            break;
    }
}

}  // namespace engine
