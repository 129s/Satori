#include <algorithm>
#include <cmath>
#include <vector>

#include <catch2/catch_amalgamated.hpp>

#include "engine/StringSynthEngine.h"
#include "synthesis/KarplusStrongString.h"
#include "synthesis/KarplusStrongSynth.h"

namespace {

std::vector<float> renderEngineSequence(engine::StringSynthEngine& engine,
                                        const std::vector<engine::Event>& events,
                                        std::size_t totalFrames,
                                        uint16_t channels = 1,
                                        std::size_t blockFrames = 256) {
    for (const auto& e : events) {
        engine.enqueueEventAt(e, e.frameOffset);
    }
    std::vector<float> buffer(totalFrames * channels, 0.0f);
    std::size_t cursor = 0;
    while (cursor < totalFrames) {
        const std::size_t framesThisBlock =
            std::min(blockFrames, totalFrames - cursor);
        engine::ProcessBlock block{
            buffer.data() + cursor * channels, framesThisBlock, channels};
        engine.process(block);
        cursor += framesThisBlock;
    }
    return buffer;
}

float maxAbs(const std::vector<float>& buffer) {
    float peak = 0.0f;
    for (float sample : buffer) {
        peak = std::max(peak, std::abs(sample));
    }
    return peak;
}

float rms(const std::vector<float>& buffer,
          std::size_t start,
          std::size_t end) {
    if (buffer.empty() || start >= buffer.size()) {
        return 0.0f;
    }
    const std::size_t clampedEnd = std::min(end, buffer.size());
    double sum = 0.0;
    std::size_t count = 0;
    for (std::size_t i = start; i < clampedEnd; ++i) {
        sum += static_cast<double>(buffer[i]) * static_cast<double>(buffer[i]);
        ++count;
    }
    if (count == 0) {
        return 0.0f;
    }
    return static_cast<float>(std::sqrt(sum / static_cast<double>(count)));
}

}  // namespace

TEST_CASE("KarplusStrongString 生成预期长度样本", "[ks-string]") {
    synthesis::StringConfig config;
    config.sampleRate = 44100.0;
    config.decay = 0.99f;
    synthesis::KarplusStrongString string(config);

    const double freq = 440.0;
    const double duration = 1.0;
    auto samples = string.pluck(freq, duration);

    REQUIRE(samples.size() ==
            Catch::Approx(config.sampleRate * duration).margin(2.0));
    const auto peak = *std::max_element(samples.begin(), samples.end(),
                                        [](float a, float b) { return std::abs(a) < std::abs(b); });
    REQUIRE(std::abs(peak) > 0.05f);
}

TEST_CASE("KarplusStrongSynth 可混合多音并归一化", "[ks-synth]") {
    synthesis::StringConfig config;
    config.sampleRate = 44100.0;
    config.decay = 0.99f;
    synthesis::KarplusStrongSynth synth(config);

    std::vector<synthesis::NoteEvent> notes = {
        {261.63, 1.0, 0.0},
        {329.63, 1.0, 0.1},
        {392.00, 1.0, 0.2},
    };

    auto buffer = synth.renderNotes(notes);
    REQUIRE_FALSE(buffer.empty());

    float maxSample = 0.0f;
    for (float value : buffer) {
        maxSample = std::max(maxSample, std::abs(value));
    }
    REQUIRE(maxSample <= Catch::Approx(1.0f).epsilon(0.001f));
}

TEST_CASE("String Loop 频散模块在极端参数下保持稳定", "[ks-string][dispersion]") {
    auto renderWithConfig = [](const synthesis::StringConfig& cfg, double freq,
                               std::size_t frames) {
        synthesis::KarplusStrongString string(cfg);
        string.start(freq);
        std::vector<float> buffer;
        buffer.reserve(frames);
        for (std::size_t i = 0; i < frames; ++i) {
            buffer.push_back(string.processSample());
        }
        REQUIRE(std::all_of(buffer.begin(), buffer.end(),
                            [](float v) { return std::isfinite(v); }));
        return buffer;
    };

    synthesis::StringConfig hotConfig;
    hotConfig.sampleRate = 48000.0;
    hotConfig.decay = 0.999f;
    hotConfig.brightness = 1.0f;
    hotConfig.dispersionAmount = 1.0f;
    hotConfig.pickPosition = 0.35f;
    hotConfig.seed = 123u;

    auto highBuffer = renderWithConfig(hotConfig, 1975.0, 4096);
    const float highPeak = maxAbs(highBuffer);
    INFO("highPeak=" << highPeak);
    REQUIRE(highPeak > 0.001f);
    REQUIRE(highPeak < 3.0f);

    synthesis::StringConfig dampedConfig = hotConfig;
    dampedConfig.decay = 0.92f;
    dampedConfig.brightness = 0.2f;
    dampedConfig.dispersionAmount = 0.85f;
    auto lowBuffer = renderWithConfig(dampedConfig, 82.41, 4096);
    const float lowPeak = maxAbs(lowBuffer);
    INFO("lowPeak=" << lowPeak);
    REQUIRE(lowPeak > 0.0005f);
    REQUIRE(lowPeak < 2.5f);
}

TEST_CASE("String Loop 在关闭低通时仍保持稳定", "[ks-string][dispersion]") {
    synthesis::StringConfig config;
    config.sampleRate = 96000.0;
    config.decay = 0.998f;
    config.brightness = 1.0f;
    config.dispersionAmount = 1.0f;
    config.enableLowpass = false;
    config.pickPosition = 0.18f;
    config.seed = 321u;

    synthesis::KarplusStrongString string(config);
    string.start(1318.51, 0.95f);

    std::vector<float> buffer;
    buffer.reserve(4096);
    for (int i = 0; i < 4096; ++i) {
        buffer.push_back(string.processSample());
    }

    REQUIRE(std::all_of(buffer.begin(), buffer.end(),
                        [](float v) { return std::isfinite(v); }));
    const float peak = maxAbs(buffer);
    INFO("noLowpassPeak=" << peak);
    REQUIRE(peak > 0.0005f);
    REQUIRE(peak < 2.5f);
}

TEST_CASE("Body 模块在极端参数下保持有限增益", "[engine-body]") {
    const double sampleRate = 44100.0;

    auto toFrames = [sampleRate](double seconds) {
        return static_cast<std::uint64_t>(
            std::max(0.0, std::round(seconds * sampleRate)));
    };

    auto renderWithTone = [&](float tone, float size) {
        engine::StringSynthEngine engine;
        engine.setSampleRate(sampleRate);
        engine.setParam(engine::ParamId::AmpRelease, 0.08f);
        engine.setParam(engine::ParamId::BodyTone, tone);
        engine.setParam(engine::ParamId::BodySize, size);
        engine::Event on{};
        on.type = engine::EventType::NoteOn;
        on.noteId = 1;
        on.frequency = 196.0;
        on.velocity = 0.9f;
        on.frameOffset = 0;

        engine::Event off{};
        off.type = engine::EventType::NoteOff;
        off.noteId = 1;
        off.frameOffset = toFrames(0.12);

        const std::size_t totalFrames = static_cast<std::size_t>(sampleRate * 0.4);
        return renderEngineSequence(engine, {on, off}, totalFrames);
    };

    auto neutral = renderWithTone(0.5f, 0.5f);
    auto bright = renderWithTone(1.0f, 1.0f);
    auto warm = renderWithTone(0.0f, 0.2f);

    REQUIRE(std::all_of(neutral.begin(), neutral.end(),
                        [](float v) { return std::isfinite(v); }));
    REQUIRE(std::all_of(bright.begin(), bright.end(),
                        [](float v) { return std::isfinite(v); }));
    REQUIRE(std::all_of(warm.begin(), warm.end(),
                        [](float v) { return std::isfinite(v); }));

    REQUIRE(maxAbs(neutral) < 2.0f);
    REQUIRE(maxAbs(bright) < 2.0f);
    REQUIRE(maxAbs(warm) < 2.0f);
    const auto energyStart = toFrames(0.05);
    const auto energyEnd = toFrames(0.2);
    const float neutralEnergy = rms(neutral, energyStart, energyEnd);
    const float brightEnergy = rms(bright, energyStart, energyEnd);
    const float warmEnergy = rms(warm, energyStart, energyEnd);

    REQUIRE(neutralEnergy > 0.0f);
    REQUIRE(brightEnergy < 2.5f);
    REQUIRE(warmEnergy < 2.5f);
    REQUIRE(brightEnergy / neutralEnergy < 2.5f);
    REQUIRE(warmEnergy / neutralEnergy < 2.5f);
    REQUIRE(brightEnergy / neutralEnergy > 0.35f);
    REQUIRE(warmEnergy / neutralEnergy > 0.35f);
    REQUIRE(brightEnergy != Catch::Approx(warmEnergy));
}

TEST_CASE("Room 模块提供可控的立体扩展", "[engine-room]") {
    const double sampleRate = 48000.0;

    auto toFrames = [sampleRate](double seconds) {
        return static_cast<std::uint64_t>(
            std::max(0.0, std::round(seconds * sampleRate)));
    };

    engine::Event on{};
    on.type = engine::EventType::NoteOn;
    on.noteId = 1;
    on.frequency = 330.0;
    on.velocity = 0.7f;
    on.frameOffset = 0;

    engine::Event off{};
    off.type = engine::EventType::NoteOff;
    off.noteId = 1;
    off.frameOffset = toFrames(0.2);

    const std::size_t totalFrames = static_cast<std::size_t>(sampleRate * 0.5);

    auto stereoRms = [](const std::vector<float>& buffer, std::size_t startFrame,
                        std::size_t endFrame) {
        if (buffer.size() < 2) {
            return 0.0f;
        }
        const std::size_t totalFrames = buffer.size() / 2;
        const std::size_t clampedEnd = std::min(endFrame, totalFrames);
        double sum = 0.0;
        std::size_t count = 0;
        for (std::size_t frame = startFrame; frame < clampedEnd; ++frame) {
            const std::size_t idx = frame * 2;
            if (idx + 1 >= buffer.size()) {
                break;
            }
            const float mono = 0.5f * (buffer[idx] + buffer[idx + 1]);
            sum += static_cast<double>(mono) * static_cast<double>(mono);
            ++count;
        }
        if (count == 0) {
            return 0.0f;
        }
        return static_cast<float>(std::sqrt(sum / static_cast<double>(count)));
    };

    auto renderWithRoom = [&](float amount) {
        engine::StringSynthEngine engine;
        engine.setSampleRate(sampleRate);
        // Make excitation deterministic to avoid flaky energy/peak assertions.
        auto cfg = engine.stringConfig();
        cfg.seed = 1234;
        cfg.excitationMode = synthesis::ExcitationMode::FixedNoisePick;
        engine.setConfig(cfg);
        engine.setParam(engine::ParamId::RoomAmount, amount);
        return renderEngineSequence(engine, {on, off}, totalFrames, 2);
    };

    auto dry = renderWithRoom(0.0f);
    auto wet = renderWithRoom(1.0f);

    REQUIRE(std::all_of(dry.begin(), dry.end(),
                        [](float v) { return std::isfinite(v); }));
    REQUIRE(std::all_of(wet.begin(), wet.end(),
                        [](float v) { return std::isfinite(v); }));

    const auto energyStart = toFrames(0.05);
    const auto energyEnd = toFrames(0.25);
    const float dryEnergy = stereoRms(dry, energyStart, energyEnd);
    const float wetEnergy = stereoRms(wet, energyStart, energyEnd);

    REQUIRE(dryEnergy > 0.0f);
    REQUIRE(wetEnergy > 0.0f);
    // Wet energy should stay in the same order of magnitude as dry.
    REQUIRE(wetEnergy < dryEnergy * 6.0f);
    REQUIRE(wetEnergy > dryEnergy * 0.1f);

    const float leftPeak = maxAbs(wet);
    float rightPeak = 0.0f;
    for (std::size_t i = 1; i < wet.size(); i += 2) {
        rightPeak = std::max(rightPeak, std::abs(wet[i]));
    }
    REQUIRE(leftPeak > 0.001f);
    REQUIRE(rightPeak > 0.001f);
    REQUIRE(std::abs(leftPeak - rightPeak) > 1e-4f);
}

TEST_CASE("StringSynthEngine NoteOn/Off 控制尾音长度", "[engine-core]") {
    const double sampleRate = 48000.0;
    engine::StringSynthEngine engine;
    engine.setSampleRate(sampleRate);
    engine.setParam(engine::ParamId::AmpRelease, 0.05f);
    engine.setParam(engine::ParamId::Decay, 0.992f);
    INFO("engine sampleRate=" << engine.sampleRate());

    auto toFrames = [sampleRate](double seconds) {
        return static_cast<std::uint64_t>(
            std::max(0.0, std::round(seconds * sampleRate)));
    };

    engine::Event on{};
    on.type = engine::EventType::NoteOn;
    on.noteId = 1;
    on.frequency = 440.0;
    on.frameOffset = 0;

    engine::Event off{};
    off.type = engine::EventType::NoteOff;
    off.noteId = 1;
    off.frameOffset = toFrames(0.05);

    const std::size_t totalFrames = static_cast<std::size_t>(sampleRate * 0.3);
    auto buffer = renderEngineSequence(engine, {on, off}, totalFrames);

    const float earlyEnergy = rms(buffer, 0, toFrames(0.1));
    const float tailEnergy = rms(buffer, toFrames(0.2), buffer.size());
    INFO("peak=" << maxAbs(buffer) << " early=" << earlyEnergy << " tail=" << tailEnergy);

    REQUIRE(earlyEnergy > 0.001f);
    REQUIRE(tailEnergy < earlyEnergy * 0.25f);
    REQUIRE(tailEnergy < 0.0005f);
}

TEST_CASE("StringSynthEngine 支持多复音叠加", "[engine-core]") {
    const double sampleRate = 44100.0;
    engine::StringSynthEngine engine;
    engine.setSampleRate(sampleRate);
    engine.setParam(engine::ParamId::AmpRelease, 0.08f);
    engine.setParam(engine::ParamId::Decay, 0.995f);

    auto toFrames = [sampleRate](double seconds) {
        return static_cast<std::uint64_t>(
            std::max(0.0, std::round(seconds * sampleRate)));
    };

    engine::Event a{};
    a.type = engine::EventType::NoteOn;
    a.noteId = 1;
    a.frequency = 220.0;
    a.frameOffset = 0;

    engine::Event b = a;
    b.noteId = 2;
    b.frequency = 330.0;
    b.frameOffset = toFrames(0.1);

    engine::Event c = b;
    c.noteId = 3;
    c.frequency = 440.0;
    c.frameOffset = toFrames(0.18);

    engine::Event offA{};
    offA.type = engine::EventType::NoteOff;
    offA.noteId = 1;
    offA.frameOffset = toFrames(0.35);

    engine::Event offB = offA;
    offB.noteId = 2;
    offB.frameOffset = toFrames(0.4);

    engine::Event offC = offA;
    offC.noteId = 3;
    offC.frameOffset = toFrames(0.45);

    const std::size_t totalFrames = static_cast<std::size_t>(sampleRate * 0.6);
    auto buffer =
        renderEngineSequence(engine, {a, b, c, offA, offB, offC}, totalFrames);

    const float peak = maxAbs(buffer);
    INFO("peak=" << peak);
    REQUIRE(peak > 0.01f);
    REQUIRE(peak < 1.5f);
}

TEST_CASE("StringSynthEngine 在 voice stealing 下保持稳定输出", "[engine-core]") {
    const double sampleRate = 48000.0;
    engine::StringSynthEngine engine;
    engine.setSampleRate(sampleRate);
    engine.setParam(engine::ParamId::AmpRelease, 0.02f);
    engine.setParam(engine::ParamId::Decay, 0.991f);

    auto toFrames = [sampleRate](double seconds) {
        return static_cast<std::uint64_t>(
            std::max(0.0, std::round(seconds * sampleRate)));
    };

    std::vector<engine::Event> events;
    const std::vector<double> freqs = {110.0, 140.0, 176.0, 220.0, 261.63,
                                       330.0, 392.0, 466.0, 523.25, 659.25};
    int noteId = 1;
    for (double freq : freqs) {
        engine::Event on{};
        on.type = engine::EventType::NoteOn;
        on.noteId = noteId;
        on.frequency = freq;
        on.frameOffset = 0;
        events.push_back(on);

        engine::Event off{};
        off.type = engine::EventType::NoteOff;
        off.noteId = noteId;
        off.frameOffset = toFrames(0.12 + 0.01 * noteId);
        events.push_back(off);
        ++noteId;
    }

    const std::size_t totalFrames = static_cast<std::size_t>(sampleRate * 0.4);
    auto buffer = renderEngineSequence(engine, events, totalFrames);

    REQUIRE(std::all_of(buffer.begin(), buffer.end(),
                        [](float s) { return std::isfinite(s); }));
    const float lateEnergy = rms(buffer, toFrames(0.32), buffer.size());
    REQUIRE(lateEnergy < 0.0015f);
}
