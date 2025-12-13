#include "win/audio/SatoriRealtimeEngine.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <windows.h>
#include <utility>

namespace winaudio {

SatoriRealtimeEngine::SatoriRealtimeEngine()
    : audioConfig_({44100, 1, 512}),
      synthConfig_(),
      audioEngine_(audioConfig_),
      synthEngine_(synthConfig_) {}

SatoriRealtimeEngine::~SatoriRealtimeEngine() {
    shutdown();
}

bool SatoriRealtimeEngine::initialize() {
    using namespace std::placeholders;
    const bool ok = audioEngine_.initialize(
        [this](float* buffer, std::size_t frames) { handleRender(buffer, frames); });
    if (ok) {
        audioConfig_ = audioEngine_.config();
        synthConfig_.sampleRate = static_cast<double>(audioConfig_.sampleRate);
        synthEngine_.setSampleRate(synthConfig_.sampleRate);
        synthEngine_.setConfig(synthConfig_);
        masterGain_ = synthEngine_.getParam(engine::ParamId::MasterGain);
        ampReleaseSeconds_ = synthEngine_.getParam(engine::ParamId::AmpRelease);
        for (std::size_t i = 0; i < kParamCount; ++i) {
            pendingParamValues_[i].store(0.0f, std::memory_order_relaxed);
        }
        pendingParamMask_.store(0, std::memory_order_relaxed);
    }
    return ok;
}

void SatoriRealtimeEngine::shutdown() {
    stop();
    audioEngine_.shutdown();
}

bool SatoriRealtimeEngine::start() {
    return audioEngine_.start();
}

void SatoriRealtimeEngine::stop() {
    audioEngine_.stop();
}

void SatoriRealtimeEngine::triggerNote(double frequency, double durationSeconds) {
    synthEngine_.noteOn(frequency, durationSeconds);
}

void SatoriRealtimeEngine::noteOn(int midiNote, double frequency, float velocity) {
    synthEngine_.noteOn(midiNote, frequency, velocity);
}

void SatoriRealtimeEngine::noteOff(int midiNote) {
    synthEngine_.noteOff(midiNote);
}

void SatoriRealtimeEngine::setSynthConfig(const synthesis::StringConfig& config) {
    synthesis::StringConfig clamped = config;
    clamped.sampleRate = static_cast<double>(audioConfig_.sampleRate);
    synthConfig_ = clamped;
    const bool wasRunning = audioEngine_.isRunning();
    if (wasRunning) {
        audioEngine_.stop();
    }
    synthEngine_.setConfig(clamped);
    masterGain_ = synthEngine_.getParam(engine::ParamId::MasterGain);
    ampReleaseSeconds_ = synthEngine_.getParam(engine::ParamId::AmpRelease);
    if (wasRunning) {
        (void)audioEngine_.start();
    }
}

void SatoriRealtimeEngine::setParam(engine::ParamId id, float value) {
    const auto* info = engine::GetParamInfo(id);
    if (info) {
        value = engine::ClampToRange(*info, value);
    }
    const std::size_t index = static_cast<std::size_t>(id);
    if (index < kParamCount) {
        pendingParamValues_[index].store(value, std::memory_order_relaxed);
        pendingParamMask_.fetch_or(static_cast<std::uint32_t>(1u << index),
                                   std::memory_order_release);
    }
    switch (id) {
        case engine::ParamId::Decay:
            synthConfig_.decay = value;
            break;
        case engine::ParamId::Brightness:
            synthConfig_.brightness = value;
            break;
        case engine::ParamId::DispersionAmount:
            synthConfig_.dispersionAmount = value;
            break;
        case engine::ParamId::ExcitationBrightness:
            synthConfig_.excitationBrightness = value;
            break;
        case engine::ParamId::ExcitationVelocity:
            synthConfig_.excitationVelocity = value;
            break;
        case engine::ParamId::ExcitationMix:
            synthConfig_.excitationMix = value;
            break;
        case engine::ParamId::BodyTone:
            synthConfig_.bodyTone = value;
            break;
        case engine::ParamId::BodySize:
            synthConfig_.bodySize = value;
            break;
        case engine::ParamId::RoomAmount:
            synthConfig_.roomAmount = value;
            break;
        case engine::ParamId::RoomIR:
            synthConfig_.roomIrIndex = static_cast<int>(std::lround(value));
            break;
        case engine::ParamId::PickPosition:
            synthConfig_.pickPosition = value;
            break;
        case engine::ParamId::EnableLowpass:
            synthConfig_.enableLowpass = value >= 0.5f;
            break;
        case engine::ParamId::NoiseType:
            synthConfig_.noiseType =
                (value >= 0.5f) ? synthesis::NoiseType::Binary : synthesis::NoiseType::White;
            break;
        case engine::ParamId::MasterGain:
            masterGain_ = value;
            break;
        case engine::ParamId::AmpRelease:
            ampReleaseSeconds_ = value;
            break;
        default:
            break;
    }
    if (!audioEngine_.isRunning()) {
        applyPendingParams();
    }
}

float SatoriRealtimeEngine::getParam(engine::ParamId id) const {
    switch (id) {
        case engine::ParamId::Decay:
            return static_cast<float>(synthConfig_.decay);
        case engine::ParamId::Brightness:
            return synthConfig_.brightness;
        case engine::ParamId::DispersionAmount:
            return synthConfig_.dispersionAmount;
        case engine::ParamId::ExcitationBrightness:
            return synthConfig_.excitationBrightness;
        case engine::ParamId::ExcitationVelocity:
            return synthConfig_.excitationVelocity;
        case engine::ParamId::ExcitationMix:
            return synthConfig_.excitationMix;
        case engine::ParamId::BodyTone:
            return synthConfig_.bodyTone;
        case engine::ParamId::BodySize:
            return synthConfig_.bodySize;
        case engine::ParamId::RoomAmount:
            return synthConfig_.roomAmount;
        case engine::ParamId::RoomIR:
            return static_cast<float>(synthConfig_.roomIrIndex);
        case engine::ParamId::PickPosition:
            return synthConfig_.pickPosition;
        case engine::ParamId::EnableLowpass:
            return synthConfig_.enableLowpass ? 1.0f : 0.0f;
        case engine::ParamId::NoiseType:
            return synthConfig_.noiseType == synthesis::NoiseType::Binary ? 1.0f : 0.0f;
        case engine::ParamId::MasterGain:
            return masterGain_;
        case engine::ParamId::AmpRelease:
            return ampReleaseSeconds_;
        default:
            break;
    }
    return 0.0f;
}

void SatoriRealtimeEngine::setMasterGain(float value) {
    setParam(engine::ParamId::MasterGain, value);
}

float SatoriRealtimeEngine::masterGain() const {
    return masterGain_;
}

SatoriRealtimeEngine::RealtimeMetrics SatoriRealtimeEngine::metrics() const {
    RealtimeMetrics m;
    m.callbackCount = callbackCount_.load(std::memory_order_relaxed);
    m.callbackMsAvg = callbackMsAvg_.load(std::memory_order_relaxed);
    m.callbackMsMax = callbackMsMax_.load(std::memory_order_relaxed);
    m.pendingParamMask = pendingParamMask_.load(std::memory_order_relaxed);
    return m;
}

void SatoriRealtimeEngine::applyPendingParams() {
    std::uint32_t mask = pendingParamMask_.exchange(0, std::memory_order_acq_rel);
    while (mask) {
        const unsigned bit = static_cast<unsigned>(std::countr_zero(mask));
        mask &= (mask - 1);
        const std::size_t index = static_cast<std::size_t>(bit);
        if (index >= kParamCount) {
            continue;
        }
        const auto id = static_cast<engine::ParamId>(index);
        const float value = pendingParamValues_[index].load(std::memory_order_relaxed);
        synthEngine_.setParam(id, value);
    }
}

void SatoriRealtimeEngine::handleRender(float* output, std::size_t frames) {
    LARGE_INTEGER start{};
    LARGE_INTEGER end{};
    static const LONGLONG qpcFreq = [] {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return f.QuadPart;
    }();
    QueryPerformanceCounter(&start);

    applyPendingParams();

    engine::ProcessBlock block;
    block.output = output;
    block.frames = frames;
    block.channels = audioConfig_.channels;
    synthEngine_.process(block);

    QueryPerformanceCounter(&end);
    const double elapsedMs =
        qpcFreq > 0
            ? (static_cast<double>(end.QuadPart - start.QuadPart) * 1000.0 /
               static_cast<double>(qpcFreq))
            : 0.0;

    const std::uint64_t count =
        callbackCount_.fetch_add(1, std::memory_order_relaxed) + 1;
    const double prevAvg = callbackMsAvg_.load(std::memory_order_relaxed);
    const double avg = (count <= 1 || prevAvg <= 0.0) ? elapsedMs : (prevAvg * 0.99 + elapsedMs * 0.01);
    callbackMsAvg_.store(avg, std::memory_order_relaxed);

    const double prevMax = callbackMsMax_.load(std::memory_order_relaxed);
    if (elapsedMs > prevMax) {
        callbackMsMax_.store(elapsedMs, std::memory_order_relaxed);
    }
}

}  // namespace winaudio
