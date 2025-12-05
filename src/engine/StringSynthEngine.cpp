#include "engine/StringSynthEngine.h"

#include <algorithm>
#include <iterator>
#include <limits>

namespace engine {

StringSynthEngine::StringSynthEngine(synthesis::StringConfig config)
    : config_(config) {
    if (const auto* info = GetParamInfo(ParamId::AmpRelease)) {
        ampReleaseSeconds_ = info->defaultValue;
    }
}

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

void StringSynthEngine::noteOn(int noteId, double frequency, float velocity) {
    Event event;
    event.type = EventType::NoteOn;
    event.noteId = noteId;
    event.velocity = velocity;
    event.frequency = frequency;
    event.durationSeconds = 6.0;
    enqueueEvent(event);
}

void StringSynthEngine::noteOff(int noteId) {
    if (noteId < 0) {
        return;
    }
    Event event;
    event.type = EventType::NoteOff;
    event.noteId = noteId;
    event.frequency = 0.0;
    event.durationSeconds = 0.0;
    enqueueEvent(event);
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
        case ParamId::AmpRelease:
            return static_cast<float>(ampReleaseSeconds_);
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
    double currentAmpRelease = 0.0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        currentConfig = config_;
        currentMasterGain = masterGain_;
        currentAmpRelease = ampReleaseSeconds_;
        events.swap(eventQueue_);
        localVoices.swap(voices_);
    }

    for (const auto& event : events) {
        if (event.type == EventType::NoteOff) {
            handleNoteOff(event.noteId, localVoices, currentAmpRelease,
                          currentConfig.sampleRate);
            continue;
        }
        handleEvent(event, currentConfig, localVoices, currentMasterGain,
                    currentAmpRelease);
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
                                    float& masterGain,
                                    double ampRelease) {
    switch (event.type) {
        case EventType::NoteOn:
            handleNoteOn(event.frequency, event.durationSeconds, config, event.noteId,
                         event.velocity, voices, ampRelease);
            break;
        case EventType::ParamChange:
            applyParamUnlocked(event.param, event.paramValue, config, masterGain);
            break;
        default:
            break;
    }
}

void StringSynthEngine::handleNoteOn(double frequency, double durationSeconds,
                                     const synthesis::StringConfig& config, int noteId,
                                     float velocity, std::vector<Voice>& voices,
                                     double ampRelease) {
    if (frequency <= 0.0 || durationSeconds <= 0.0 ||
        config.sampleRate <= 0.0) {
        return;
    }
    if (voices.size() >= kMaxVoices) {
        auto it = std::max_element(
            voices.begin(), voices.end(),
            [](const Voice& a, const Voice& b) { return a.cursor < b.cursor; });
        if (it != voices.end()) {
            voices.erase(it);
        }
    }
    synthesis::KarplusStrongString string(config);
    const double sustainSeconds = std::max(durationSeconds, 6.0);
    auto samples = string.pluck(frequency, sustainSeconds);
    if (samples.empty()) {
        return;
    }
    Voice v;
    v.buffer = std::move(samples);
    v.cursor = 0;
    v.releaseStart = std::numeric_limits<std::size_t>::max();
    v.releaseSamples =
        static_cast<std::size_t>(std::max(0.0, ampRelease * config.sampleRate));
    v.noteId = noteId;
    v.velocity = velocity;
    v.state = Voice::State::Active;
    voices.push_back(std::move(v));
}

void StringSynthEngine::handleNoteOff(int noteId, std::vector<Voice>& voices,
                                      double ampRelease, double sampleRateValue) {
    if (noteId < 0) {
        return;
    }
    for (auto& voice : voices) {
        if (voice.noteId != noteId || voice.state == Voice::State::Releasing) {
            continue;
        }
        voice.state = Voice::State::Releasing;
        voice.releaseStart = voice.cursor;
        voice.releaseSamples =
            static_cast<std::size_t>(std::max(0.0, ampRelease * sampleRateValue));
        break;
    }
}

void StringSynthEngine::mixVoices(const ProcessBlock& block,
                                  std::vector<Voice>& voices,
                                  float masterGain) const {
    for (auto& voice : voices) {
        const auto bufferSize = voice.buffer.size();
        for (std::size_t frame = 0; frame < block.frames && voice.cursor < bufferSize;
             ++frame) {
            const float envelope = ComputeReleaseEnvelope(voice);
            const float sample = voice.buffer[voice.cursor++] * masterGain * envelope;
            for (uint16_t ch = 0; ch < block.channels; ++ch) {
                block.output[frame * block.channels + ch] += sample;
            }
        }
    }

    voices.erase(
        std::remove_if(voices.begin(), voices.end(),
                       [](const Voice& voice) {
                           const bool finishedBuffer = voice.cursor >= voice.buffer.size();
                           const bool finishedRelease =
                               voice.state == Voice::State::Releasing &&
                               voice.releaseSamples > 0 &&
                               voice.cursor >= voice.releaseStart + voice.releaseSamples;
                           return finishedBuffer || finishedRelease;
                       }),
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
        case ParamId::AmpRelease:
            ampReleaseSeconds_ = clamped;
            break;
        default:
            break;
    }
}

float StringSynthEngine::ComputeReleaseEnvelope(const Voice& voice) {
    if (voice.state != Voice::State::Releasing || voice.releaseSamples == 0 ||
        voice.releaseStart == std::numeric_limits<std::size_t>::max()) {
        return voice.velocity;
    }
    const std::size_t elapsed = voice.cursor > voice.releaseStart
                                    ? (voice.cursor - voice.releaseStart)
                                    : 0;
    if (elapsed >= voice.releaseSamples) {
        return 0.0f;
    }
    const float factor =
        1.0f - static_cast<float>(elapsed) / static_cast<float>(voice.releaseSamples);
    return voice.velocity * factor;
}

}  // namespace engine
