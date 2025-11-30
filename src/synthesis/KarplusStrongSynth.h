#pragma once

#include <vector>

#include "synthesis/KarplusStrongString.h"

namespace synthesis {

struct NoteEvent {
    double frequency = 440.0;
    double duration = 1.0;
    double startTime = 0.0;
};

class KarplusStrongSynth {
public:
    explicit KarplusStrongSynth(StringConfig config = {});

    std::vector<float> renderNotes(const std::vector<NoteEvent>& notes) const;
    std::vector<float> renderChord(const std::vector<double>& frequencies,
                                   double durationSeconds) const;

private:
    std::vector<float> mixSamples(
        const std::vector<std::vector<float>>& noteSamples,
        const std::vector<std::size_t>& offsets) const;

    float findPeak(const std::vector<float>& buffer) const;
    void normalize(std::vector<float>& buffer, float peak) const;

    StringConfig baseConfig_;
};

}  // namespace synthesis
