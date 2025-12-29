#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace midi {

struct MidiNoteEvent {
    int midiNote = 60;
    double frequency = 440.0;
    double startTime = 0.0;
    double duration = 0.0;
    float velocity = 1.0f;
};

struct MidiSong {
    std::vector<MidiNoteEvent> notes;
    double lengthSeconds = 0.0;
    std::uint16_t ticksPerQuarter = 480;
};

bool LoadMidiFile(const std::filesystem::path& path,
                  MidiSong& outSong,
                  std::string& errorMessage);

}  // namespace midi

