#include "win/audio/SatoriRealtimeEngine.h"

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
        synthEngine_.setConfig(synthConfig_);
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

void SatoriRealtimeEngine::setSynthConfig(const synthesis::StringConfig& config) {
    synthConfig_ = config;
    synthConfig_.sampleRate = static_cast<double>(audioConfig_.sampleRate);
    synthEngine_.setConfig(synthConfig_);
}

void SatoriRealtimeEngine::handleRender(float* output, std::size_t frames) {
    engine::ProcessBlock block;
    block.output = output;
    block.frames = frames;
    block.channels = audioConfig_.channels;
    synthEngine_.process(block);
}

}  // namespace winaudio
