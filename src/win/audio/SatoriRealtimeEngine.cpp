#include "win/audio/SatoriRealtimeEngine.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <windows.h>
#include <utility>
#include <vector>

namespace winaudio {

SatoriRealtimeEngine::SatoriRealtimeEngine()
    : audioConfig_({AudioBackendType::WasapiShared, L"", 0, 0, 1, 512}),
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

bool SatoriRealtimeEngine::reconfigureAudio(const AudioEngineConfig& config) {
    const bool wasRunning = audioEngine_.isRunning();
    if (wasRunning) {
        audioEngine_.stop();
    }
    const bool ok = audioEngine_.reinitialize(
        config, [this](float* buffer, std::size_t frames) { handleRender(buffer, frames); });
    if (!ok) {
        return false;
    }
    audioConfig_ = audioEngine_.config();
    if (audioConfig_.backend == AudioBackendType::Asio && audioConfig_.sampleRate > 0) {
        synthConfig_.sampleRate = static_cast<double>(audioConfig_.sampleRate);
        synthEngine_.setSampleRate(synthConfig_.sampleRate);
    } else if (synthConfig_.sampleRate <= 0.0) {
        synthConfig_.sampleRate = static_cast<double>(audioConfig_.sampleRate);
        synthEngine_.setSampleRate(synthConfig_.sampleRate);
    }
    synthEngine_.setConfig(synthConfig_);
    resetResampler();
    if (wasRunning) {
        (void)audioEngine_.start();
    }
    return true;
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
    if (clamped.sampleRate <= 0.0) {
        clamped.sampleRate = synthConfig_.sampleRate > 0.0
                                 ? synthConfig_.sampleRate
                                 : static_cast<double>(audioConfig_.sampleRate);
    }
    synthConfig_ = clamped;
    const bool wasRunning = audioEngine_.isRunning();
    if (wasRunning) {
        audioEngine_.stop();
    }
    synthEngine_.setSampleRate(synthConfig_.sampleRate);
    synthEngine_.setConfig(synthConfig_);
    resetResampler();
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

    const std::size_t channels = static_cast<std::size_t>(audioConfig_.channels);
    const double outRate = static_cast<double>(audioConfig_.sampleRate);
    const double inRate = synthConfig_.sampleRate;

    if (channels == 0 || outRate <= 0.0 || inRate <= 0.0 ||
        std::abs(inRate - outRate) < 1e-6) {
        engine::ProcessBlock block;
        block.output = output;
        block.frames = frames;
        block.channels = audioConfig_.channels;
        synthEngine_.process(block);
    } else {
        const bool needReset = (resampleChannels_ != channels) ||
                               (std::abs(resampleInRate_ - inRate) > 1e-6) ||
                               (std::abs(resampleOutRate_ - outRate) > 1e-6);
        if (needReset) {
            resampleChannels_ = channels;
            resampleInRate_ = inRate;
            resampleOutRate_ = outRate;
            resampleStep_ = resampleInRate_ / resampleOutRate_;
            resetResampler();
        }

        if (!resampleReady_) {
            resampleFrame0_.assign(channels, 0.0f);
            resampleFrame1_.assign(channels, 0.0f);
            ensureResampleInputFrames(2);
            popResampleInputFrame(resampleFrame0_);
            popResampleInputFrame(resampleFrame1_);
            resampleReady_ = true;
        }

        const float phaseEps = 1e-7f;
        for (std::size_t f = 0; f < frames; ++f) {
            const float t = static_cast<float>(std::clamp(resamplePhase_, 0.0, 1.0));
            const float a = 1.0f - t;
            const std::size_t base = f * channels;
            for (std::size_t ch = 0; ch < channels; ++ch) {
                output[base + ch] = resampleFrame0_[ch] * a + resampleFrame1_[ch] * t;
            }

            resamplePhase_ += resampleStep_;
            while (resamplePhase_ >= 1.0 - phaseEps) {
                resamplePhase_ -= 1.0;
                resampleFrame0_ = resampleFrame1_;
                ensureResampleInputFrames(1);
                popResampleInputFrame(resampleFrame1_);
            }
        }
    }

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

void SatoriRealtimeEngine::resetResampler() {
    resamplePhase_ = 0.0;
    resampleFifo_.clear();
    resampleReadSample_ = 0;
    resampleFrame0_.clear();
    resampleFrame1_.clear();
    resampleReady_ = false;
}

void SatoriRealtimeEngine::ensureResampleInputFrames(std::size_t frames) {
    const std::size_t channels = static_cast<std::size_t>(audioConfig_.channels);
    if (channels == 0) {
        return;
    }
    if (resampleReadSample_ > 0 && resampleReadSample_ >= 8192 * channels) {
        resampleFifo_.erase(resampleFifo_.begin(),
                            resampleFifo_.begin() + static_cast<std::ptrdiff_t>(resampleReadSample_));
        resampleReadSample_ = 0;
    }

    const std::size_t availableSamples =
        resampleFifo_.size() >= resampleReadSample_ ? (resampleFifo_.size() - resampleReadSample_) : 0;
    const std::size_t availableFrames = availableSamples / channels;
    if (availableFrames >= frames) {
        return;
    }

    const std::size_t need = frames - availableFrames;
    const std::size_t renderFrames = std::max<std::size_t>(need, 256);

    std::vector<float> temp(renderFrames * channels, 0.0f);
    engine::ProcessBlock block{temp.data(), renderFrames, static_cast<uint16_t>(channels)};
    synthEngine_.process(block);
    resampleFifo_.insert(resampleFifo_.end(), temp.begin(), temp.end());
}

void SatoriRealtimeEngine::popResampleInputFrame(std::vector<float>& frame) {
    const std::size_t channels = static_cast<std::size_t>(audioConfig_.channels);
    if (channels == 0) {
        frame.clear();
        return;
    }
    frame.resize(channels);
    ensureResampleInputFrames(1);

    const std::size_t availableSamples =
        resampleFifo_.size() >= resampleReadSample_ ? (resampleFifo_.size() - resampleReadSample_) : 0;
    if (availableSamples < channels) {
        std::fill(frame.begin(), frame.end(), 0.0f);
        return;
    }

    for (std::size_t ch = 0; ch < channels; ++ch) {
        frame[ch] = resampleFifo_[resampleReadSample_ + ch];
    }
    resampleReadSample_ += channels;
}

}  // namespace winaudio
