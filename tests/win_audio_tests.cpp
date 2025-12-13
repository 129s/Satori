#ifdef _WIN32

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <windows.h>

#include <catch2/catch_amalgamated.hpp>

#include "engine/StringSynthEngine.h"
#include "win/audio/SatoriRealtimeEngine.h"
#include "win/audio/WASAPIAudioEngine.h"

namespace {
class ScopedCOM {
public:
    ScopedCOM() { CoInitializeEx(nullptr, COINIT_MULTITHREADED); }
    ~ScopedCOM() { CoUninitialize(); }
};
}  // namespace

TEST_CASE("WASAPIAudioEngine 能完成初始化", "[wasapi]") {
    ScopedCOM com;
    winaudio::AudioEngineConfig config;
    config.bufferFrames = 256;
    winaudio::WASAPIAudioEngine engine(config);
    std::atomic<int> callbackCount{0};
    auto callback = [&engine, &callbackCount](float* output, std::size_t frames) {
        const std::size_t channels = engine.config().channels;
        std::fill_n(output, frames * channels, 0.0f);
        ++callbackCount;
    };
    REQUIRE(engine.initialize(callback));
    REQUIRE(engine.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    engine.stop();
    engine.shutdown();
    REQUIRE(callbackCount.load() >= 1);
}

TEST_CASE("SatoriRealtimeEngine 支持触发音符", "[realtime-engine]") {
    ScopedCOM com;
    winaudio::SatoriRealtimeEngine engine;
    REQUIRE(engine.initialize());
    REQUIRE(engine.start());
    engine.triggerNote(440.0);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    engine.stop();
    engine.shutdown();
}

TEST_CASE("SatoriRealtimeEngine 支持更改工作采样率", "[realtime-engine]") {
    ScopedCOM com;
    winaudio::SatoriRealtimeEngine engine;
    REQUIRE(engine.initialize());

    const double deviceSampleRate = static_cast<double>(engine.audioConfig().sampleRate);
    REQUIRE(deviceSampleRate > 0.0);

    synthesis::StringConfig alteredConfig = engine.synthConfig();
    alteredConfig.sampleRate = (std::abs(deviceSampleRate - 48000.0) < 1e-6) ? 44100.0 : 48000.0;
    alteredConfig.decay = 0.992f;
    engine.setSynthConfig(alteredConfig);

    const auto& syncedConfig = engine.synthConfig();
    REQUIRE(syncedConfig.sampleRate == Catch::Approx(alteredConfig.sampleRate));
    REQUIRE(syncedConfig.decay == Catch::Approx(alteredConfig.decay));
}

TEST_CASE("StringSynthEngine 处理 NoteOn 并耗尽 voice", "[engine]") {
    synthesis::StringConfig config;
    config.sampleRate = 48000.0;
    engine::StringSynthEngine synth(config);
    synth.setParam(engine::ParamId::AmpRelease, 0.02f);

    const int midi = 60;
    synth.noteOn(midi, 440.0, 1.0f);

    const uint16_t channels = 2;
    const std::size_t frames = 128;

    bool produced = false;
    bool drained = false;
    for (int i = 0; i < 48; ++i) {
        std::vector<float> buffer(frames * channels);
        engine::ProcessBlock block{buffer.data(), frames, channels};
        synth.process(block);
        const bool hasSignal = std::any_of(
            buffer.begin(), buffer.end(), [](float sample) { return sample != 0.0f; });
        produced = produced || hasSignal;
        if (i == 4) {
            synth.noteOff(midi);
        }
    }

    for (int i = 0; i < 80; ++i) {
        std::vector<float> buffer(frames * channels);
        engine::ProcessBlock block{buffer.data(), frames, channels};
        synth.process(block);
        if (std::all_of(buffer.begin(), buffer.end(),
                        [](float sample) { return sample == 0.0f; })) {
            drained = true;
            break;
        }
    }

    REQUIRE(produced);
    REQUIRE(drained);
}

#endif  // _WIN32
