#include <filesystem>
#include <fstream>
#include <system_error>
#include <vector>

#include <catch2/catch_amalgamated.hpp>

#include "midi/MidiNoteLoader.h"

namespace {

void WriteVarLen(std::vector<std::uint8_t>& buffer, std::uint32_t value) {
    std::uint8_t bytes[4];
    int count = 0;
    bytes[count++] = static_cast<std::uint8_t>(value & 0x7Fu);
    while ((value >>= 7u) != 0u) {
        bytes[count++] =
            static_cast<std::uint8_t>(0x80u | (value & 0x7Fu));
    }
    for (int i = count - 1; i >= 0; --i) {
        buffer.push_back(bytes[i]);
    }
}

struct TempFile {
    explicit TempFile(std::filesystem::path p) : path(std::move(p)) {}
    ~TempFile() {
        if (!path.empty()) {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
    }
    std::filesystem::path path;
};

std::vector<std::uint8_t> BuildSustainTrack() {
    std::vector<std::uint8_t> track;
    auto writeEvent = [&](std::uint32_t delta, std::uint8_t status,
                          std::uint8_t data0, std::uint8_t data1) {
        WriteVarLen(track, delta);
        track.push_back(status);
        track.push_back(data0);
        track.push_back(data1);
    };

    // Note on immediately.
    writeEvent(0, 0x90, 60, 100);
    // Pedal down after half a beat.
    writeEvent(240, 0xB0, 64, 127);
    // Release key while pedal is held.
    writeEvent(240, 0x80, 60, 0);
    // Pedal up one beat later.
    writeEvent(480, 0xB0, 64, 0);
    // End of track.
    WriteVarLen(track, 0);
    track.push_back(0xFF);
    track.push_back(0x2F);
    track.push_back(0x00);
    return track;
}

std::vector<std::uint8_t> BuildMidiFileBytes() {
    const std::uint16_t division = 480;
    std::vector<std::uint8_t> track = BuildSustainTrack();
    std::vector<std::uint8_t> bytes;
    auto append = [&](const char* data) {
        bytes.insert(bytes.end(), data, data + 4);
    };
    auto appendU32 = [&](std::uint32_t value) {
        bytes.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFFu));
        bytes.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFu));
        bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
        bytes.push_back(static_cast<std::uint8_t>(value & 0xFFu));
    };
    auto appendU16 = [&](std::uint16_t value) {
        bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
        bytes.push_back(static_cast<std::uint8_t>(value & 0xFFu));
    };

    append("MThd");
    appendU32(6);
    appendU16(0);   // format 0
    appendU16(1);   // tracks
    appendU16(division);

    append("MTrk");
    appendU32(static_cast<std::uint32_t>(track.size()));
    bytes.insert(bytes.end(), track.begin(), track.end());
    return bytes;
}

}  // namespace

TEST_CASE("Midi loader applies velocity and sustain pedal", "[midi][sustain]") {
    const auto bytes = BuildMidiFileBytes();
    const auto tempPath = std::filesystem::temp_directory_path() /
                          std::filesystem::unique_path("satori-midi-%%%%-%%%%.mid");
    {
        std::ofstream out(tempPath, std::ios::binary);
        REQUIRE(out.is_open());
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        REQUIRE(out.good());
    }
    TempFile cleanup(tempPath);

    midi::MidiSong song;
    std::string error;
    REQUIRE(midi::LoadMidiFile(tempPath, song, error));
    REQUIRE(error.empty());
    REQUIRE(song.notes.size() == 1);
    const auto& note = song.notes.front();
    REQUIRE(note.startTime == Catch::Approx(0.0).margin(1e-6));
    // Pedal holds the note to roughly one second (960 ticks at 120BPM).
    REQUIRE(note.duration == Catch::Approx(1.0).margin(0.01));
    const float expectedVelocity = 100.0f / 127.0f;
    REQUIRE(note.velocity == Catch::Approx(expectedVelocity).margin(1e-4));
    REQUIRE(song.lengthSeconds == Catch::Approx(note.duration).margin(0.01));
}
