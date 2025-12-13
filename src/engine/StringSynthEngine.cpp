#include "engine/StringSynthEngine.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>

#include "dsp/Filter.h"
#include "dsp/ConvolutionReverb.h"
#include "dsp/PartitionedConvolver.h"
#include "dsp/RoomIrLibrary.h"

namespace engine {

class BodyFilter {
public:
    void setSampleRate(double sampleRate) {
        if (sampleRate <= 0.0) {
            return;
        }
        sampleRate_ = sampleRate;
        updateCoefficients();
    }

    void setParams(float tone, float size) {
        tone_ = std::clamp(tone, 0.0f, 1.0f);
        size_ = std::clamp(size, 0.0f, 1.0f);
        updateCoefficients();
    }

    void reset() {
        lowFilter_.reset();
    }

    float process(float input) {
        const float low = lowFilter_.process(input);
        const float high = input - low;
        return low * lowGain_ + high * highGain_;
    }

private:
    void updateCoefficients() {
        const float fc = 180.0f + 800.0f * size_;
        const float alpha = std::clamp(
            static_cast<float>((2.0 * 3.141592653589793 * fc) / sampleRate_), 0.001f, 0.99f);
        lowFilter_.setAlpha(alpha);
        const float tilt = (tone_ - 0.5f) * 0.6f;
        lowGain_ = std::clamp(1.0f + (-tilt), 0.6f, 1.6f);
        highGain_ = std::clamp(1.0f + tilt, 0.6f, 1.6f);
    }

    dsp::OnePoleLowPass lowFilter_{0.1f};
    double sampleRate_ = 44100.0;
    float tone_ = 0.5f;
    float size_ = 0.5f;
    float lowGain_ = 1.0f;
    float highGain_ = 1.0f;
};

class ExpressiveMapping {
public:
    static float Apply(float velocity, double frequency,
                       const synthesis::StringConfig& base,
                       synthesis::StringConfig& out) {
        out = base;
        const float v = std::clamp(velocity, 0.0f, 1.0f);
        const double refFreq = 440.0;
        const double ratio = (frequency > 0.0) ? (frequency / refFreq) : 1.0;
        const float keyTrack =
            static_cast<float>(std::clamp(std::log2(ratio), -3.0, 3.0));

        const float amp = 0.45f + 0.65f * v;

        auto clampParam = [](ParamId id, float value) {
            if (const auto* info = GetParamInfo(id)) {
                return ClampToRange(*info, value);
            }
            return value;
        };

        const float brightnessDelta = 0.28f * (v - 0.5f) + 0.12f * keyTrack;
        out.brightness =
            clampParam(ParamId::Brightness, out.brightness + brightnessDelta);

        const float decayDelta = -0.022f * (v - 0.5f) - 0.012f * keyTrack;
        out.decay = clampParam(ParamId::Decay, out.decay + decayDelta);

        return amp;
    }
};

class RoomProcessor {
public:
    void setSampleRate(double sampleRate) {
        if (sampleRate <= 0.0) {
            return;
        }
        sampleRate_ = sampleRate;
        rebuildKernels();
        reverb_.setSampleRate(sampleRate_);
        reverb_.setIrKernels(kernels_);
    }

    void setMix(float mix) {
        mix_ = std::clamp(mix, 0.0f, 1.0f);
        reverb_.setMix(mix_);
    }

    void setIrIndex(int index) {
        irIndex_ = std::max(0, index);
        reverb_.setIrIndex(irIndex_);
    }

    void reset() { reverb_.reset(); }

    void process(float input, float& outL, float& outR) {
        reverb_.processSample(input, outL, outR);
    }

private:
    static std::vector<float> ResampleLinear(const float* src,
                                             std::size_t srcCount,
                                             int srcRate,
                                             int dstRate) {
        if (!src || srcCount == 0 || srcRate <= 0 || dstRate <= 0) {
            return {};
        }
        if (srcRate == dstRate) {
            return std::vector<float>(src, src + srcCount);
        }
        const double ratio = static_cast<double>(dstRate) / static_cast<double>(srcRate);
        const std::size_t dstCount =
            std::max<std::size_t>(1, static_cast<std::size_t>(std::llround(srcCount * ratio)));
        std::vector<float> dst(dstCount, 0.0f);
        for (std::size_t i = 0; i < dstCount; ++i) {
            const double srcPos = static_cast<double>(i) / ratio;
            const std::size_t idx = static_cast<std::size_t>(std::floor(srcPos));
            const std::size_t idx1 = std::min(srcCount - 1, idx + 1);
            const float t = static_cast<float>(srcPos - static_cast<double>(idx));
            const float a = src[std::min(idx, srcCount - 1)];
            const float b = src[idx1];
            dst[i] = a + (b - a) * t;
        }
        return dst;
    }

    void rebuildKernels() {
        kernels_.clear();
        const auto& list = dsp::RoomIrLibrary::list();
        kernels_.reserve(list.size());

        for (std::size_t i = 0; i < list.size(); ++i) {
            std::size_t count = 0;
            int irSr = 0;
            const float* ir = dsp::RoomIrLibrary::samplesMono(static_cast<int>(i), &count, &irSr);
            std::vector<float> resampled = ResampleLinear(ir, count, irSr,
                                                         static_cast<int>(std::lround(sampleRate_)));
            // Safety: keep IR length bounded for CPU predictability (truncate tail).
            const std::size_t maxLen = static_cast<std::size_t>(std::lround(sampleRate_ * 2.0)); // 2s
            if (resampled.size() > maxLen) {
                resampled.resize(maxLen);
            }
            kernels_.push_back(dsp::PartitionedConvolver::buildKernelFromIr(
                resampled, reverbBlockSize_, reverbFftSize_));
        }
    }

    double sampleRate_ = 44100.0;
    float mix_ = 0.0f;
    int irIndex_ = 0;

    // Keep consistent with dsp::ConvolutionReverb defaults.
    std::size_t reverbBlockSize_ = 256;
    std::size_t reverbFftSize_ = 512;

    std::vector<dsp::ConvolutionKernel> kernels_;
    dsp::ConvolutionReverb reverb_;
};

namespace {

constexpr float kVoiceSilenceThreshold = 1e-5f;
constexpr float kEnergyDecay = 0.995f;
constexpr float kEnvelopeFloor = 1e-5f;
constexpr double kDefaultAttackSecondsValue = 0.004;

class AmpEnvelope {
public:
    AmpEnvelope() = default;

    void setSampleRate(double sampleRate) {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : sampleRate_;
        updateAttackSamples();
        updateReleaseSamples();
    }

    void setAttackSeconds(double seconds) {
        attackSeconds_ = std::max(0.0, seconds);
        updateAttackSamples();
    }

    void setReleaseSeconds(double seconds) {
        releaseSeconds_ = std::max(0.0, seconds);
        updateReleaseSamples();
    }

    void noteOn(float targetLevel) {
        targetLevel_ = std::max(0.0f, targetLevel);
        stageCursor_ = 0;
        if (attackSamples_ == 0) {
            level_ = targetLevel_;
            stage_ = Stage::Sustain;
        } else {
            level_ = 0.0f;
            stage_ = Stage::Attack;
        }
    }

    void noteOff() {
        if (stage_ == Stage::Idle) {
            return;
        }
        stage_ = Stage::Release;
        stageCursor_ = 0;
        updateReleaseSamples();
        releaseStartLevel_ = level_;
        if (releaseSamples_ == 0) {
            level_ = 0.0f;
            stage_ = Stage::Idle;
        }
    }

    float next() {
        switch (stage_) {
            case Stage::Idle:
                level_ = 0.0f;
                return level_;
            case Stage::Attack: {
                if (attackSamples_ == 0) {
                    level_ = targetLevel_;
                    stage_ = Stage::Sustain;
                    return level_;
                }
                const float t = static_cast<float>(stageCursor_ + 1) /
                                static_cast<float>(attackSamples_);
                level_ = targetLevel_ * std::min(1.0f, t);
                if (++stageCursor_ >= attackSamples_) {
                    stage_ = Stage::Sustain;
                    stageCursor_ = 0;
                }
                return level_;
            }
            case Stage::Sustain:
                level_ = targetLevel_;
                return level_;
            case Stage::Release: {
                if (releaseSamples_ == 0) {
                    level_ = 0.0f;
                    stage_ = Stage::Idle;
                    return level_;
                }
                const float t = static_cast<float>(stageCursor_) /
                                static_cast<float>(releaseSamples_);
                const float factor = std::max(0.0f, 1.0f - t);
                level_ = releaseStartLevel_ * factor;
                if (++stageCursor_ >= releaseSamples_ || level_ < kEnvelopeFloor) {
                    level_ = 0.0f;
                    stage_ = Stage::Idle;
                }
                return level_;
            }
        }
        return 0.0f;
    }

    bool isIdle() const { return stage_ == Stage::Idle; }
    bool isReleasing() const { return stage_ == Stage::Release; }
    float level() const { return level_; }

private:
    enum class Stage { Idle, Attack, Sustain, Release };

    void updateAttackSamples() {
        attackSamples_ = static_cast<std::size_t>(
            std::max(0.0, std::round(attackSeconds_ * sampleRate_)));
    }

    void updateReleaseSamples() {
        releaseSamples_ = static_cast<std::size_t>(
            std::max(0.0, std::round(releaseSeconds_ * sampleRate_)));
    }

    Stage stage_ = Stage::Idle;
    double sampleRate_ = 44100.0;
    double attackSeconds_ = kDefaultAttackSecondsValue;
    double releaseSeconds_ = 0.35;
    float level_ = 0.0f;
    float targetLevel_ = 1.0f;
    float releaseStartLevel_ = 0.0f;
    std::size_t stageCursor_ = 0;
    std::size_t attackSamples_ = 0;
    std::size_t releaseSamples_ = 0;
};

struct Voice {
    synthesis::KarplusStrongString string;
    AmpEnvelope envelope;
    int noteId = -1;
    double frequency = 0.0;
    float velocity = 1.0f;
    std::uint64_t age = 0;
    float energy = 0.0f;
};

}  // namespace

class StringSynthEngine::VoiceManager {
public:
    VoiceManager(std::size_t maxVoices, double sampleRate, double attackSeconds,
                 double releaseSeconds)
        : maxVoices_(maxVoices),
          sampleRate_(sampleRate > 0.0 ? sampleRate : 44100.0),
          attackSeconds_(attackSeconds),
          releaseSeconds_(releaseSeconds) {}

    void setSampleRate(double sampleRate) {
        if (sampleRate <= 0.0) {
            return;
        }
        sampleRate_ = sampleRate;
        for (auto& voice : voices_) {
            voice.envelope.setSampleRate(sampleRate_);
        }
    }

    void setAttackSeconds(double seconds) {
        attackSeconds_ = std::max(0.0, seconds);
        for (auto& voice : voices_) {
            voice.envelope.setAttackSeconds(attackSeconds_);
        }
    }

    void setReleaseSeconds(double seconds) {
        releaseSeconds_ = std::max(0.0, seconds);
        for (auto& voice : voices_) {
            voice.envelope.setReleaseSeconds(releaseSeconds_);
        }
    }

    void noteOn(int noteId, double frequency, float velocity,
                const synthesis::StringConfig& config) {
        if (frequency <= 0.0) {
            return;
        }
        Voice* voice = findVoiceByNote(noteId);
        if (!voice) {
            voice = allocateVoice();
        }
        if (!voice) {
            return;
        }
        voice->noteId = noteId;
        voice->frequency = frequency;
        voice->velocity = velocity;
        voice->age = ++ageCounter_;
        voice->energy = 0.0f;

        synthesis::StringConfig voiceConfig = config;
        const float amp = ExpressiveMapping::Apply(velocity, frequency, config, voiceConfig);
        voiceConfig.sampleRate = sampleRate_;
        voice->string.updateConfig(voiceConfig);
        voice->string.start(frequency, velocity);

        voice->envelope.setSampleRate(sampleRate_);
        voice->envelope.setAttackSeconds(attackSeconds_);
        voice->envelope.setReleaseSeconds(releaseSeconds_);
        voice->envelope.noteOn(amp);
    }

    void noteOff(int noteId) {
        if (noteId < 0) {
            return;
        }
        for (auto& voice : voices_) {
            if (voice.noteId == noteId) {
                voice.envelope.setReleaseSeconds(releaseSeconds_);
                voice.envelope.noteOff();
            }
        }
    }

    float renderFrame(float masterGain) {
        float mixed = 0.0f;
        for (auto& voice : voices_) {
            if (voice.envelope.isIdle()) {
                continue;
            }
            const float env = voice.envelope.next();
            const float sample = voice.string.processSample() * env * voice.velocity;
            voice.energy = kEnergyDecay * voice.energy +
                           (1.0f - kEnergyDecay) * std::abs(sample);
            mixed += sample;
        }

        cleanupSilentVoices();
        return mixed * masterGain;
    }

    std::size_t activeVoices() const { return voices_.size(); }

private:
    Voice* findVoiceByNote(int noteId) {
        auto it = std::find_if(
            voices_.begin(), voices_.end(),
            [noteId](const Voice& voice) { return voice.noteId == noteId; });
        if (it == voices_.end()) {
            return nullptr;
        }
        return &(*it);
    }

    Voice* allocateVoice() {
        if (voices_.size() < maxVoices_) {
            voices_.emplace_back();
            voices_.back().envelope.setSampleRate(sampleRate_);
            voices_.back().envelope.setAttackSeconds(attackSeconds_);
            voices_.back().envelope.setReleaseSeconds(releaseSeconds_);
            return &voices_.back();
        }
        // Voice stealing: prefer releasing voices, otherwise lowest energy, then oldest.
        auto candidate = std::min_element(
            voices_.begin(), voices_.end(),
            [](const Voice& a, const Voice& b) {
                if (a.envelope.isReleasing() != b.envelope.isReleasing()) {
                    return a.envelope.isReleasing();
                }
                if (std::abs(a.energy - b.energy) > std::numeric_limits<float>::epsilon()) {
                    return a.energy < b.energy;
                }
                return a.age < b.age;
            });
        if (candidate == voices_.end()) {
            return nullptr;
        }
        return &(*candidate);
    }

    void cleanupSilentVoices() {
        voices_.erase(
            std::remove_if(voices_.begin(), voices_.end(),
                           [](const Voice& voice) {
                               return voice.envelope.isIdle() ||
                                      (voice.envelope.isReleasing() &&
                                       voice.energy < kVoiceSilenceThreshold);
                           }),
            voices_.end());
    }

    std::vector<Voice> voices_;
    const std::size_t maxVoices_;
    double sampleRate_ = 44100.0;
    double attackSeconds_ = kDefaultAttackSecondsValue;
    double releaseSeconds_ = 0.35;
    std::uint64_t ageCounter_ = 0;
};

StringSynthEngine::StringSynthEngine(synthesis::StringConfig config)
    : config_(config) {
    if (const auto* info = GetParamInfo(ParamId::AmpRelease)) {
        ampReleaseSeconds_ = info->defaultValue;
    }
    voiceManager_ = std::make_unique<VoiceManager>(
        kMaxVoices, config_.sampleRate, kDefaultAttackSeconds, ampReleaseSeconds_);
    voiceManager_->setReleaseSeconds(ampReleaseSeconds_);
    bodyFilter_ = std::make_unique<BodyFilter>();
    bodyFilter_->setSampleRate(config_.sampleRate);
    bodyFilter_->setParams(config_.bodyTone, config_.bodySize);
    roomProcessor_ = std::make_unique<RoomProcessor>();
    roomProcessor_->setSampleRate(config_.sampleRate);
    roomProcessor_->setMix(config_.roomAmount);
    roomProcessor_->setIrIndex(config_.roomIrIndex);
}

StringSynthEngine::~StringSynthEngine() = default;

void StringSynthEngine::setConfig(const synthesis::StringConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_.sampleRate = config.sampleRate;
    config_.seed = config.seed;
    config_.excitationMode = config.excitationMode;
    applyParamUnlocked(ParamId::Decay, static_cast<float>(config.decay), config_,
                       masterGain_);
    applyParamUnlocked(ParamId::Brightness, config.brightness, config_, masterGain_);
    applyParamUnlocked(ParamId::DispersionAmount, config.dispersionAmount, config_,
                       masterGain_);
    applyParamUnlocked(ParamId::ExcitationBrightness, config.excitationBrightness, config_,
                       masterGain_);
    applyParamUnlocked(ParamId::ExcitationVelocity, config.excitationVelocity, config_,
                       masterGain_);
    applyParamUnlocked(ParamId::ExcitationMix, config.excitationMix, config_,
                       masterGain_);
    applyParamUnlocked(ParamId::BodyTone, config.bodyTone, config_, masterGain_);
    applyParamUnlocked(ParamId::BodySize, config.bodySize, config_, masterGain_);
    applyParamUnlocked(ParamId::RoomAmount, config.roomAmount, config_, masterGain_);
    applyParamUnlocked(ParamId::RoomIR, static_cast<float>(config.roomIrIndex), config_,
                       masterGain_);
    applyParamUnlocked(ParamId::PickPosition, config.pickPosition, config_, masterGain_);
    applyParamUnlocked(ParamId::EnableLowpass, config.enableLowpass ? 1.0f : 0.0f,
                       config_, masterGain_);
    applyParamUnlocked(ParamId::NoiseType,
                       config.noiseType == synthesis::NoiseType::Binary ? 1.0f : 0.0f,
                       config_, masterGain_);
    voiceManager_->setSampleRate(config_.sampleRate);
    if (bodyFilter_) {
        bodyFilter_->setSampleRate(config_.sampleRate);
    }
    if (roomProcessor_) {
        roomProcessor_->setSampleRate(config_.sampleRate);
        roomProcessor_->setIrIndex(config_.roomIrIndex);
        roomProcessor_->setMix(config_.roomAmount);
    }
}

synthesis::StringConfig StringSynthEngine::stringConfig() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

void StringSynthEngine::setSampleRate(double sampleRate) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_.sampleRate = sampleRate;
    voiceManager_->setSampleRate(config_.sampleRate);
    if (bodyFilter_) {
        bodyFilter_->setSampleRate(config_.sampleRate);
    }
    if (roomProcessor_) {
        roomProcessor_->setSampleRate(config_.sampleRate);
    }
}

double StringSynthEngine::sampleRate() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_.sampleRate;
}

void StringSynthEngine::enqueueEvent(const Event& event) {
    enqueueEventAt(event, frameCursor_.load(std::memory_order_relaxed));
}

void StringSynthEngine::enqueueEventAt(const Event& event,
                                       std::uint64_t frameOffset) {
    Event stamped = event;
    stamped.frameOffset = frameOffset;
    std::lock_guard<std::mutex> lock(mutex_);
    eventQueue_.push_back(stamped);
}

void StringSynthEngine::noteOn(int noteId, double frequency, float velocity,
                               double durationSeconds) {
    if (frequency <= 0.0) {
        return;
    }
    std::uint64_t startFrame = frameCursor_.load(std::memory_order_relaxed);
    double currentSampleRate = 0.0;
    int resolvedNoteId = noteId;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        currentSampleRate = config_.sampleRate;
        if (resolvedNoteId < 0) {
            resolvedNoteId = nextNoteId_++;
        }
    }

    Event event;
    event.type = EventType::NoteOn;
    event.noteId = resolvedNoteId;
    event.velocity = velocity;
    event.frequency = frequency;
    enqueueEventAt(event, startFrame);

    if (durationSeconds > 0.0 && currentSampleRate > 0.0) {
        const auto deltaFrames = static_cast<std::uint64_t>(
            std::max(0.0, std::round(durationSeconds * currentSampleRate)));
        Event off;
        off.type = EventType::NoteOff;
        off.noteId = resolvedNoteId;
        enqueueEventAt(off, startFrame + deltaFrames);
    }
}

void StringSynthEngine::noteOff(int noteId) {
    if (noteId < 0) {
        return;
    }
    Event event;
    event.type = EventType::NoteOff;
    event.noteId = noteId;
    enqueueEvent(event);
}

void StringSynthEngine::noteOn(double frequency, double durationSeconds) {
    noteOn(-1, frequency, 1.0f, durationSeconds);
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
        case ParamId::DispersionAmount:
            return config_.dispersionAmount;
        case ParamId::ExcitationBrightness:
            return config_.excitationBrightness;
        case ParamId::ExcitationVelocity:
            return config_.excitationVelocity;
        case ParamId::ExcitationMix:
            return config_.excitationMix;
        case ParamId::BodyTone:
            return config_.bodyTone;
        case ParamId::BodySize:
            return config_.bodySize;
        case ParamId::RoomAmount:
            return config_.roomAmount;
        case ParamId::RoomIR:
            return static_cast<float>(config_.roomIrIndex);
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

    const std::uint64_t blockStartFrame =
        frameCursor_.load(std::memory_order_relaxed);
    const std::uint64_t blockEndFrame = blockStartFrame + block.frames;

    std::vector<Event> pendingEvents;
    synthesis::StringConfig currentConfig{};
    float currentMasterGain = 1.0f;
    double currentAmpRelease = 0.0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        currentConfig = config_;
        currentMasterGain = masterGain_;
        currentAmpRelease = ampReleaseSeconds_;
        pendingEvents.swap(eventQueue_);
    }

    voiceManager_->setSampleRate(currentConfig.sampleRate);
    voiceManager_->setReleaseSeconds(currentAmpRelease);

    std::vector<Event> readyEvents;
    std::vector<Event> futureEvents;
    readyEvents.reserve(pendingEvents.size());
    futureEvents.reserve(pendingEvents.size());

    for (auto& event : pendingEvents) {
        if (event.frameOffset <= blockStartFrame) {
            event.frameOffset = blockStartFrame;
            readyEvents.push_back(event);
        } else if (event.frameOffset < blockEndFrame) {
            readyEvents.push_back(event);
        } else {
            futureEvents.push_back(event);
        }
    }

    std::stable_sort(
        readyEvents.begin(), readyEvents.end(),
        [](const Event& a, const Event& b) { return a.frameOffset < b.frameOffset; });

    std::size_t eventIndex = 0;
    for (std::size_t frame = 0; frame < block.frames; ++frame) {
        const std::uint64_t absoluteFrame = blockStartFrame + frame;
        while (eventIndex < readyEvents.size() &&
               readyEvents[eventIndex].frameOffset <= absoluteFrame) {
            handleEvent(readyEvents[eventIndex], currentConfig, currentMasterGain,
                        currentAmpRelease);
            ++eventIndex;
        }

        float sample = voiceManager_->renderFrame(currentMasterGain);
        if (bodyFilter_) {
            sample = bodyFilter_->process(sample);
        }
        float left = sample;
        float right = sample;
        if (roomProcessor_) {
            roomProcessor_->process(sample, left, right);
        }
        if (block.channels >= 2) {
            block.output[frame * block.channels] += left;
            block.output[frame * block.channels + 1] += right;
            for (uint16_t ch = 2; ch < block.channels; ++ch) {
                block.output[frame * block.channels + ch] += sample;
            }
        } else if (block.channels == 1) {
            block.output[frame] += 0.5f * (left + right);
        }
    }

    frameCursor_.fetch_add(block.frames, std::memory_order_relaxed);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        config_ = currentConfig;
        masterGain_ = currentMasterGain;
        ampReleaseSeconds_ = currentAmpRelease;
        if (!futureEvents.empty()) {
            eventQueue_.insert(eventQueue_.end(),
                               std::make_move_iterator(futureEvents.begin()),
                               std::make_move_iterator(futureEvents.end()));
            std::stable_sort(
                eventQueue_.begin(), eventQueue_.end(),
                [](const Event& a, const Event& b) {
                    return a.frameOffset < b.frameOffset;
                });
        }
    }
}

std::size_t StringSynthEngine::activeVoiceCount() const {
    return voiceManager_ ? voiceManager_->activeVoices() : 0;
}

std::size_t StringSynthEngine::queuedEventCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return eventQueue_.size();
}

std::uint64_t StringSynthEngine::renderedFrames() const {
    return frameCursor_.load(std::memory_order_relaxed);
}

std::vector<std::uint64_t> StringSynthEngine::queuedEventFrames() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::uint64_t> frames;
    frames.reserve(eventQueue_.size());
    for (const auto& event : eventQueue_) {
        frames.push_back(event.frameOffset);
    }
    return frames;
}

void StringSynthEngine::handleEvent(const Event& event,
                                    synthesis::StringConfig& config,
                                    float& masterGain,
                                    double& ampRelease) {
    switch (event.type) {
        case EventType::NoteOn:
            voiceManager_->noteOn(event.noteId, event.frequency, event.velocity, config);
            break;
        case EventType::NoteOff:
            voiceManager_->noteOff(event.noteId);
            break;
        case EventType::ParamChange:
            applyParamUnlocked(event.param, event.paramValue, config, masterGain);
            ampRelease = ampReleaseSeconds_;
            voiceManager_->setReleaseSeconds(ampRelease);
            break;
        default:
            break;
    }
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
        case ParamId::DispersionAmount:
            config.dispersionAmount = clamped;
            break;
        case ParamId::ExcitationBrightness:
            config.excitationBrightness = clamped;
            break;
        case ParamId::ExcitationVelocity:
            config.excitationVelocity = clamped;
            break;
        case ParamId::ExcitationMix:
            config.excitationMix = clamped;
            break;
        case ParamId::BodyTone:
            config.bodyTone = clamped;
            if (bodyFilter_) {
                bodyFilter_->setParams(config.bodyTone, config.bodySize);
            }
            break;
        case ParamId::BodySize:
            config.bodySize = clamped;
            if (bodyFilter_) {
                bodyFilter_->setParams(config.bodyTone, config.bodySize);
            }
            break;
        case ParamId::RoomAmount:
            config.roomAmount = clamped;
            if (roomProcessor_) {
                roomProcessor_->setMix(config.roomAmount);
            }
            break;
        case ParamId::RoomIR:
            config.roomIrIndex = static_cast<int>(std::lround(clamped));
            if (roomProcessor_) {
                roomProcessor_->setIrIndex(config.roomIrIndex);
            }
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
            if (voiceManager_) {
                voiceManager_->setReleaseSeconds(ampReleaseSeconds_);
            }
            break;
        default:
            break;
    }
}

}  // namespace engine
