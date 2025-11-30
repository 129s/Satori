#include <algorithm>
#include <cmath>
#include <vector>

#include <catch2/catch_amalgamated.hpp>

#include "synthesis/KarplusStrongString.h"
#include "synthesis/KarplusStrongSynth.h"

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
