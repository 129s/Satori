#ifdef _WIN32

#include <algorithm>
#include <vector>

#include <catch2/catch_amalgamated.hpp>

#include "engine/StringSynthEngine.h"

TEST_CASE("StringSynthEngine NoteOn/NoteOff 进入释放阶段", "[engine-voice]") {
    engine::StringSynthEngine synth;
    synth.setSampleRate(48000.0);
    synth.setParam(engine::ParamId::AmpRelease, 0.05f);

    const int midiNote = 60;
    const double freq = 261.6256;
    synth.noteOn(midiNote, freq);

    const uint16_t channels = 1;
    const std::size_t frames = 128;

    bool produced = false;
    for (int i = 0; i < 4; ++i) {
        std::vector<float> buffer(frames * channels);
        engine::ProcessBlock block{buffer.data(), frames, channels};
        synth.process(block);
        produced = produced || std::any_of(buffer.begin(), buffer.end(),
                                           [](float s) { return s != 0.0f; });
    }
    REQUIRE(produced);

    synth.noteOff(midiNote);

    bool drained = false;
    for (int i = 0; i < 80; ++i) {
        std::vector<float> buffer(frames * channels);
        engine::ProcessBlock block{buffer.data(), frames, channels};
        synth.process(block);
        if (std::all_of(buffer.begin(), buffer.end(), [](float s) { return s == 0.0f; })) {
            drained = true;
            break;
        }
    }
    REQUIRE(drained);
}

#endif  // _WIN32
