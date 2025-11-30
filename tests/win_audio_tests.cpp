#ifdef _WIN32

#include <atomic>
#include <chrono>
#include <thread>
#include <windows.h>

#include <catch2/catch_amalgamated.hpp>

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
    engine.triggerNote(440.0, 0.5);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    engine.stop();
    engine.shutdown();
}

#endif  // _WIN32
