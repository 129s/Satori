#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include "engine/StringParams.h"
#include "synthesis/KarplusStrongString.h"

namespace engine {

enum class EventType { NoteOn, NoteOff, ParamChange };

struct Event {
    EventType type = EventType::NoteOn;
    int noteId = -1;
    float velocity = 1.0f;
    ParamId param = ParamId::Decay;
    float paramValue = 0.0f;
    double frequency = 440.0;
    double durationSeconds = 1.0;
    // 绝对帧时间戳，基于当前采样率
    std::uint64_t frameOffset = 0;
};

struct ProcessBlock {
    float* output = nullptr;
    std::size_t frames = 0;
    uint16_t channels = 1;
};

class StringSynthEngine {
public:
    explicit StringSynthEngine(synthesis::StringConfig config = {});
    ~StringSynthEngine();

    void setConfig(const synthesis::StringConfig& config);
    synthesis::StringConfig stringConfig() const;

    void setSampleRate(double sampleRate);
    double sampleRate() const;

    void enqueueEvent(const Event& event);
    void enqueueEventAt(const Event& event, std::uint64_t frameOffset);
    void noteOn(int noteId, double frequency, float velocity = 1.0f,
                double durationSeconds = 0.0);
    void noteOff(int noteId);
    void noteOn(double frequency, double durationSeconds);

    void setParam(ParamId id, float value);
    float getParam(ParamId id) const;

    void process(const ProcessBlock& block);
    std::size_t activeVoiceCount() const;
    std::size_t queuedEventCount() const;
    std::uint64_t renderedFrames() const;
    std::vector<std::uint64_t> queuedEventFrames() const;

private:
    class VoiceManager;

    void handleEvent(const Event& event, synthesis::StringConfig& config,
                     float& masterGain, double& ampRelease);
    void applyParamUnlocked(ParamId id, float value,
                            synthesis::StringConfig& config,
                            float& masterGain);
    static constexpr std::size_t kMaxVoices = 8;
    static constexpr double kDefaultAttackSeconds = 0.004;

    synthesis::StringConfig config_;
    float masterGain_ = 1.0f;
    double ampReleaseSeconds_ = 0.35;
    std::vector<Event> eventQueue_;
    std::unique_ptr<VoiceManager> voiceManager_;
    std::atomic<std::uint64_t> frameCursor_{0};
    int nextNoteId_ = 1;
    mutable std::mutex mutex_;
};

}  // namespace engine
