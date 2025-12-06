#ifdef _WIN32

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <windows.h>

#include <catch2/catch_amalgamated.hpp>

#include "win/audio/SatoriRealtimeEngine.h"
#include "win/audio/WASAPIAudioEngine.h"

using Catch::Approx;

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
    auto callback = [&callbackCount](float* output, std::size_t frames) {
        const std::size_t total = frames;
        for (std::size_t i = 0; i < total; ++i) {
            output[i] = 0.0f;
        }
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

TEST_CASE("SatoriRealtimeEngine 初始化后同步设备采样率", "[realtime-engine]") {
    ScopedCOM com;
    winaudio::SatoriRealtimeEngine engine;
    REQUIRE(engine.initialize());

    const double deviceSampleRate = engine.synthConfig().sampleRate;
    REQUIRE(deviceSampleRate > 0.0);

    synthesis::StringConfig alteredConfig = engine.synthConfig();
    alteredConfig.sampleRate = deviceSampleRate + 1000.0;
    alteredConfig.decay = 0.992f;
    engine.setSynthConfig(alteredConfig);

    const auto& syncedConfig = engine.synthConfig();
    REQUIRE(syncedConfig.sampleRate == Approx(deviceSampleRate));
    REQUIRE(syncedConfig.decay == Approx(alteredConfig.decay));
}

TEST_CASE("RealtimeSynthRenderer 混合多 voice 并适配不同 buffer", "[realtime-engine]") {
    synthesis::StringConfig config;
    config.sampleRate = 48000.0;
    winaudio::RealtimeSynthRenderer renderer(config);

    renderer.enqueueNote(440.0, 0.02);
    renderer.enqueueNote(660.0, 0.03);

    const uint16_t channels = 2;
    {
        const std::size_t frames = 128;
        std::vector<float> buffer(frames * channels);
        renderer.render(buffer.data(), frames, channels);
        REQUIRE(std::any_of(buffer.begin(), buffer.end(),
                            [](float sample) { return sample != 0.0f; }));
    }

    {
        const std::size_t frames = 64;
        std::vector<float> buffer(frames * channels);
        renderer.render(buffer.data(), frames, channels);
        REQUIRE(std::any_of(buffer.begin(), buffer.end(),
                            [](float sample) { return sample != 0.0f; }));
    }

    bool drained = false;
    for (int i = 0; i < 64; ++i) {
        const std::size_t frames = 64;
        std::vector<float> buffer(frames * channels);
        renderer.render(buffer.data(), frames, channels);
        if (std::all_of(buffer.begin(), buffer.end(),
                        [](float sample) { return sample == 0.0f; })) {
            drained = true;
            break;
        }
    }
    REQUIRE(drained);
}

#endif  // _WIN32
