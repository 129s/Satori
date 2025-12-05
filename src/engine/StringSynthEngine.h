#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

#include "synthesis/KarplusStrongString.h"

namespace engine {

enum class EventType { NoteOn, ParamChange };

struct Event {
    EventType type = EventType::NoteOn;
    double frequency = 440.0;
    double durationSeconds = 1.0;
    synthesis::StringConfig params{};
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
    synthesis::StringConfig config() const;

    void enqueueEvent(const Event& event);
    void noteOn(double frequency, double durationSeconds);

    void process(const ProcessBlock& block);

private:
    struct Voice {
        std::vector<float> buffer;
        std::size_t cursor = 0;
    };

    void handleEvent(const Event& event, synthesis::StringConfig& config,
                     std::vector<Voice>& voices);
    void handleNoteOn(double frequency, double durationSeconds,
                      const synthesis::StringConfig& config,
                      std::vector<Voice>& voices);
    void mixVoices(const ProcessBlock& block, std::vector<Voice>& voices) const;

    synthesis::StringConfig config_;
    std::vector<Voice> voices_;
    std::vector<Event> eventQueue_;
    mutable std::mutex mutex_;
};

}  // namespace engine

