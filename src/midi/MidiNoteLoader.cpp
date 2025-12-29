#include "midi/MidiNoteLoader.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace midi {

namespace {

constexpr double kDefaultTempoUsPerQuarter = 500000.0;  // 120 BPM

struct RawNote {
    int midiNote = 60;
    std::int64_t startTick = 0;
    std::int64_t endTick = 0;
    float velocity = 1.0f;
    int channel = 0;
};

struct TempoEvent {
    std::int64_t tick = 0;
    double microsecondsPerQuarter = kDefaultTempoUsPerQuarter;
};

struct TempoPoint {
    std::int64_t tick = 0;
    double seconds = 0.0;
    double microsecondsPerQuarter = kDefaultTempoUsPerQuarter;
};

struct SustainEvent {
    std::int64_t tick = 0;
    int channel = 0;
    bool pedalDown = false;
};

struct SustainInterval {
    std::int64_t startTick = 0;
    std::int64_t endTick = 0;
};

using SustainMap = std::unordered_map<int, std::vector<SustainInterval>>;

struct ActiveNoteState {
    std::int64_t startTick = 0;
    float velocity = 1.0f;
    int channel = 0;
    int midiNote = 60;
};

double MidiNoteToFrequency(int midiNote) {
    return 440.0 * std::pow(2.0, (static_cast<double>(midiNote) - 69.0) / 12.0);
}

std::uint16_t ReadBigEndianU16(const unsigned char* data) {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(data[0]) << 8) |
        static_cast<std::uint16_t>(data[1]));
}

std::uint32_t ReadBigEndianU32(const unsigned char* data) {
    return (static_cast<std::uint32_t>(data[0]) << 24) |
           (static_cast<std::uint32_t>(data[1]) << 16) |
           (static_cast<std::uint32_t>(data[2]) << 8) |
           static_cast<std::uint32_t>(data[3]);
}

bool ReadChunkHeader(std::ifstream& stream,
                     char (&id)[4],
                     std::uint32_t& length,
                     std::string& error) {
    if (!stream.read(id, 4)) {
        error = "Unexpected end of file while reading chunk id.";
        return false;
    }
    std::array<unsigned char, 4> lenBytes{};
    if (!stream.read(reinterpret_cast<char*>(lenBytes.data()), 4)) {
        error = "Unexpected end of file while reading chunk length.";
        return false;
    }
    length = ReadBigEndianU32(lenBytes.data());
    return true;
}

bool ReadVarLen(const std::vector<std::uint8_t>& data,
                std::size_t& offset,
                std::uint32_t& value) {
    value = 0;
    int count = 0;
    while (offset < data.size()) {
        const std::uint8_t byte = data[offset++];
        value = (value << 7) | (byte & 0x7Fu);
        ++count;
        if ((byte & 0x80u) == 0) {
            return true;
        }
        if (count >= 4) {
            return false;
        }
    }
    return false;
}

struct TrackParseResult {
    std::int64_t endTick = 0;
    bool success = true;
    std::string error;
};

TrackParseResult ParseTrack(const std::vector<std::uint8_t>& data,
                            std::vector<RawNote>& rawNotes,
                            std::vector<TempoEvent>& tempoEvents,
                            std::vector<SustainEvent>& sustainEvents) {
    std::unordered_map<int, std::vector<ActiveNoteState>> active;
    std::size_t offset = 0;
    std::uint8_t runningStatus = 0;
    std::int64_t currentTick = 0;
    while (offset < data.size()) {
        std::uint32_t delta = 0;
        if (!ReadVarLen(data, offset, delta)) {
            return {currentTick, false,
                    "Failed to read variable-length delta time."};
        }
        currentTick += static_cast<std::int64_t>(delta);
        if (offset >= data.size()) {
            break;
        }

        std::uint8_t status = data[offset];
        if (status < 0x80) {
            if (runningStatus == 0) {
                return {currentTick, false, "Running status used before set."};
            }
            status = runningStatus;
        } else {
            ++offset;
            runningStatus = status;
        }

        if (status == 0xFF) {
            if (offset >= data.size()) {
                return {currentTick, false,
                        "Malformed meta event missing type byte."};
            }
            const std::uint8_t type = data[offset++];
            std::uint32_t length = 0;
            if (!ReadVarLen(data, offset, length)) {
                return {currentTick, false,
                        "Failed to read meta event length."};
            }
            if (offset + length > data.size()) {
                return {currentTick, false, "Meta event length exceeds chunk."};
            }
            if (type == 0x2F) {
                // End-of-track; ignore remaining bytes if any.
                offset = data.size();
                currentTick += 0;
            } else if (type == 0x51 && length == 3) {
                const std::uint32_t usPerQuarter =
                    (static_cast<std::uint32_t>(data[offset]) << 16) |
                    (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
                    static_cast<std::uint32_t>(data[offset + 2]);
                tempoEvents.push_back(
                    {currentTick, static_cast<double>(usPerQuarter)});
            }
            offset += length;
            continue;
        }

        if (status == 0xF0 || status == 0xF7) {
            std::uint32_t length = 0;
            if (!ReadVarLen(data, offset, length)) {
                return {currentTick, false,
                        "Failed to read SysEx event length."};
            }
            if (offset + length > data.size()) {
                return {currentTick, false, "SysEx length exceeds chunk."};
            }
            offset += length;
            continue;
        }

        const std::uint8_t type = status & 0xF0u;
        const std::uint8_t channel = status & 0x0Fu;
        auto readDataByte = [&](std::uint8_t& out) -> bool {
            if (offset >= data.size()) {
                return false;
            }
            out = data[offset++];
            return true;
        };

        std::uint8_t data0 = 0;
        std::uint8_t data1 = 0;
        switch (type) {
            case 0x80:
            case 0x90: {
                if (!readDataByte(data0) || !readDataByte(data1)) {
                    return {currentTick, false, "Malformed note event."};
                }
                const int key = channel * 128 + data0;
                if (type == 0x90 && data1 > 0) {
                    ActiveNoteState pending;
                    pending.startTick = currentTick;
                    pending.velocity = static_cast<float>(data1) / 127.0f;
                    pending.channel = channel;
                    pending.midiNote = data0;
                    active[key].push_back(pending);
                } else {
                    auto it = active.find(key);
                    if (it != active.end() && !it->second.empty()) {
                        auto start = it->second.back();
                        it->second.pop_back();
                        RawNote note;
                        note.midiNote = start.midiNote;
                        note.startTick = start.startTick;
                        note.endTick = currentTick;
                        note.velocity = start.velocity;
                        note.channel = start.channel;
                        if (note.endTick > note.startTick) {
                            rawNotes.push_back(note);
                        }
                    }
                }
                break;
            }
            case 0xA0:
            case 0xE0:
                if (!readDataByte(data0) || !readDataByte(data1)) {
                    return {currentTick, false,
                            "Malformed channel event (2 bytes)."};
                }
                break;
            case 0xB0:
                if (!readDataByte(data0) || !readDataByte(data1)) {
                    return {currentTick, false,
                            "Malformed channel event (2 bytes)."};
                }
                if (data0 == 64) {
                    SustainEvent evt;
                    evt.tick = currentTick;
                    evt.channel = channel;
                    evt.pedalDown = data1 >= 64;
                    sustainEvents.push_back(evt);
                }
                break;
            case 0xC0:
            case 0xD0:
                if (!readDataByte(data0)) {
                    return {currentTick, false,
                            "Malformed channel event (1 byte)."};
                }
                break;
            default:
                return {currentTick, false, "Unknown MIDI event type."};
        }
    }

    for (auto& entry : active) {
        for (const auto& pending : entry.second) {
            RawNote note;
            note.midiNote = pending.midiNote;
            note.startTick = pending.startTick;
            note.endTick = currentTick;
            note.velocity = pending.velocity;
            note.channel = pending.channel;
            if (note.endTick > note.startTick) {
                rawNotes.push_back(note);
            }
        }
    }

    return {currentTick, true, {}};
}

std::vector<TempoPoint> BuildTempoTimeline(const std::vector<TempoEvent>& events,
                                           std::uint16_t ticksPerQuarter) {
    std::vector<TempoEvent> sorted = events;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const TempoEvent& a, const TempoEvent& b) {
                         return a.tick < b.tick;
                     });

    std::vector<TempoPoint> timeline;
    timeline.push_back({0, 0.0, kDefaultTempoUsPerQuarter});
    double currentSeconds = 0.0;
    std::int64_t lastTick = 0;
    double currentTempo = kDefaultTempoUsPerQuarter;
    for (const auto& evt : sorted) {
        if (evt.tick < lastTick) {
            continue;
        }
        const std::int64_t deltaTick = evt.tick - lastTick;
        currentSeconds += static_cast<double>(deltaTick) *
                          (currentTempo / 1'000'000.0) /
                          static_cast<double>(ticksPerQuarter);
        lastTick = evt.tick;
        currentTempo = evt.microsecondsPerQuarter;
        timeline.push_back({evt.tick, currentSeconds, currentTempo});
    }
    return timeline;
}

double TicksToSeconds(const std::vector<TempoPoint>& timeline,
                      std::uint16_t ticksPerQuarter,
                      std::int64_t tick) {
    if (timeline.empty()) {
        return static_cast<double>(tick) *
               (kDefaultTempoUsPerQuarter / 1'000'000.0) /
               static_cast<double>(ticksPerQuarter);
    }
    double seconds = 0.0;
    std::int64_t lastTick = 0;
    double tempo = kDefaultTempoUsPerQuarter;
    for (std::size_t i = 0; i < timeline.size(); ++i) {
        const TempoPoint& point = timeline[i];
        if (i == 0) {
            seconds = point.seconds;
            lastTick = point.tick;
            tempo = point.microsecondsPerQuarter;
            continue;
        }
        const TempoPoint& prev = timeline[i - 1];
        if (tick < point.tick) {
            const std::int64_t deltaTick = tick - prev.tick;
            return prev.seconds +
                   static_cast<double>(deltaTick) *
                       (prev.microsecondsPerQuarter / 1'000'000.0) /
                       static_cast<double>(ticksPerQuarter);
        }
        seconds = point.seconds;
        lastTick = point.tick;
        tempo = point.microsecondsPerQuarter;
    }
    const TempoPoint& last = timeline.back();
    const std::int64_t deltaTick = tick - last.tick;
    return last.seconds +
           static_cast<double>(deltaTick) *
               (last.microsecondsPerQuarter / 1'000'000.0) /
               static_cast<double>(ticksPerQuarter);
}

SustainMap BuildSustainMap(const std::vector<SustainEvent>& events,
                           std::int64_t maxTick) {
    SustainMap map;
    if (events.empty()) {
        return map;
    }
    std::unordered_map<int, std::vector<SustainEvent>> byChannel;
    for (const auto& evt : events) {
        byChannel[evt.channel].push_back(evt);
    }
    for (auto& entry : byChannel) {
        auto& list = entry.second;
        std::stable_sort(
            list.begin(), list.end(),
            [](const SustainEvent& a, const SustainEvent& b) {
                if (a.tick == b.tick) {
                    return a.pedalDown && !b.pedalDown;
                }
                return a.tick < b.tick;
            });
        bool pedalDown = false;
        std::int64_t startTick = 0;
        for (const auto& evt : list) {
            if (evt.pedalDown) {
                if (!pedalDown) {
                    pedalDown = true;
                    startTick = evt.tick;
                } else {
                    startTick = std::min(startTick, evt.tick);
                }
            } else if (pedalDown) {
                if (evt.tick > startTick) {
                    map[entry.first].push_back({startTick, evt.tick});
                }
                pedalDown = false;
            }
        }
        if (pedalDown) {
            const std::int64_t endTick = std::max(startTick, maxTick);
            if (endTick > startTick) {
                map[entry.first].push_back({startTick, endTick});
            }
        }
    }
    for (auto& entry : map) {
        std::sort(entry.second.begin(), entry.second.end(),
                  [](const SustainInterval& a, const SustainInterval& b) {
                      return a.startTick < b.startTick;
                  });
    }
    return map;
}

}  // namespace

bool LoadMidiFile(const std::filesystem::path& path,
                  MidiSong& outSong,
                  std::string& errorMessage) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open()) {
        errorMessage = "Failed to open MIDI file: " + path.string();
        return false;
    }

    char chunkId[4];
    std::uint32_t chunkLength = 0;
    if (!ReadChunkHeader(stream, chunkId, chunkLength, errorMessage)) {
        return false;
    }
    if (std::string(chunkId, 4) != "MThd") {
        errorMessage = "Invalid MIDI file: missing MThd header.";
        return false;
    }
    if (chunkLength < 6) {
        errorMessage = "Invalid MIDI header length.";
        return false;
    }
    std::array<unsigned char, 6> headerData{};
    if (!stream.read(reinterpret_cast<char*>(headerData.data()), 6)) {
        errorMessage = "Failed to read MIDI header data.";
        return false;
    }
    const std::uint16_t format = ReadBigEndianU16(headerData.data());
    const std::uint16_t trackCount =
        ReadBigEndianU16(headerData.data() + 2);
    const std::uint16_t division =
        ReadBigEndianU16(headerData.data() + 4);
    if (division & 0x8000u) {
        errorMessage = "SMPTE timecode divisions are not supported.";
        return false;
    }
    if (division == 0) {
        errorMessage = "Invalid ticks-per-quarter value.";
        return false;
    }
    if (format > 1) {
        errorMessage = "Only MIDI format 0 or 1 files are supported.";
        return false;
    }

    // Skip any remaining header bytes beyond the standard 6.
    if (chunkLength > 6) {
        stream.seekg(static_cast<std::streamoff>(chunkLength - 6), std::ios::cur);
    }

    std::vector<RawNote> rawNotes;
    std::vector<TempoEvent> tempoEvents;
    tempoEvents.reserve(16);
    std::vector<SustainEvent> sustainEvents;
    sustainEvents.reserve(32);
    std::int64_t maxTick = 0;

    for (std::uint16_t track = 0; track < trackCount; ++track) {
        if (!ReadChunkHeader(stream, chunkId, chunkLength, errorMessage)) {
            return false;
        }
        if (std::string(chunkId, 4) != "MTrk") {
            errorMessage = "Expected MTrk chunk.";
            return false;
        }
        std::vector<std::uint8_t> data(chunkLength);
        if (!stream.read(reinterpret_cast<char*>(data.data()), chunkLength)) {
            errorMessage = "Unexpected EOF while reading track data.";
            return false;
        }
        auto result = ParseTrack(data, rawNotes, tempoEvents, sustainEvents);
        if (!result.success) {
            errorMessage = result.error;
            return false;
        }
        maxTick = std::max(maxTick, result.endTick);
    }

    if (rawNotes.empty()) {
        outSong = MidiSong{};
        return true;
    }

    std::vector<TempoPoint> timeline =
        BuildTempoTimeline(tempoEvents, division);
    SustainMap sustainMap = BuildSustainMap(sustainEvents, maxTick);
    MidiSong song;
    song.ticksPerQuarter = division;
    song.notes.reserve(rawNotes.size());
    for (const auto& raw : rawNotes) {
        std::int64_t endTick = raw.endTick;
        if (endTick > raw.startTick) {
            auto sustainIt = sustainMap.find(raw.channel);
            if (sustainIt != sustainMap.end()) {
                const auto& intervals = sustainIt->second;
                for (const auto& interval : intervals) {
                    if (endTick < interval.startTick) {
                        break;
                    }
                    if (endTick >= interval.startTick &&
                        endTick < interval.endTick) {
                        endTick = interval.endTick;
                        break;
                    }
                }
            }
        }
        const double startSeconds =
            TicksToSeconds(timeline, division, raw.startTick);
        const double endSeconds =
            TicksToSeconds(timeline, division, endTick);
        double duration = endSeconds - startSeconds;
        if (duration <= 0.0) {
            duration = 0.0;
        }
        MidiNoteEvent note;
        note.midiNote = raw.midiNote;
        note.frequency = MidiNoteToFrequency(raw.midiNote);
        note.startTime = startSeconds;
        note.duration = duration;
        note.velocity = std::clamp(raw.velocity, 0.0f, 1.0f);
        song.lengthSeconds = std::max(song.lengthSeconds, endSeconds);
        song.notes.push_back(note);
    }
    std::sort(song.notes.begin(), song.notes.end(),
              [](const MidiNoteEvent& a, const MidiNoteEvent& b) {
                  if (a.startTime == b.startTime) {
                      return a.midiNote < b.midiNote;
                  }
                  return a.startTime < b.startTime;
              });

    outSong = std::move(song);
    return true;
}

}  // namespace midi
