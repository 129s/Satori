#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

#include "engine/StringParams.h"
#include "synthesis/KarplusStrongString.h"

namespace engine {

enum class EventType { NoteOn, ParamChange };

struct Event {
    EventType type = EventType::NoteOn;
    ParamId param = ParamId::Decay;
    float paramValue = 0.0f;
    double frequency = 440.0;
    double durationSeconds = 1.0;
};

struct ProcessBlock {
    float* output = nullptr;
    std::size_t frames = 0;
    uint16_t channels = 1;
};

class StringSynthEngine {
public:
    explicit StringSynthEngine(synthesis::StringConfig config = {});

    void setConfig(const synthesis::StringConfig& config);
    synthesis::StringConfig stringConfig() const;

    void setSampleRate(double sampleRate);
    double sampleRate() const;

    void enqueueEvent(const Event& event);
    void noteOn(double frequency, double durationSeconds);

    void setParam(ParamId id, float value);
    float getParam(ParamId id) const;

    void process(const ProcessBlock& block);

private:
    struct Voice {
        std::vector<float> buffer;
        std::size_t cursor = 0;
    };

    void handleEvent(const Event& event, synthesis::StringConfig& config,
                     std::vector<Voice>& voices, float& masterGain);
    void handleNoteOn(double frequency, double durationSeconds,
                      const synthesis::StringConfig& config,
                      std::vector<Voice>& voices);
    void mixVoices(const ProcessBlock& block, std::vector<Voice>& voices,
                   float masterGain) const;
    void applyParamUnlocked(ParamId id, float value,
                            synthesis::StringConfig& config,
                            float& masterGain);

    synthesis::StringConfig config_;
    float masterGain_ = 1.0f;
    std::vector<Voice> voices_;
    std::vector<Event> eventQueue_;
    mutable std::mutex mutex_;
};

}  // namespace engine
