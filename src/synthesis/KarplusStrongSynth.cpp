#include "synthesis/KarplusStrongSynth.h"

#include <algorithm>
#include <cmath>

namespace synthesis {

KarplusStrongSynth::KarplusStrongSynth(StringConfig config)
    : baseConfig_(config) {}

std::vector<float> KarplusStrongSynth::renderNotes(
    const std::vector<NoteEvent>& notes) const {
    if (notes.empty()) {
        return {};
    }

    double maxTime = 0.0;
    for (const auto& note : notes) {
        maxTime = std::max(maxTime, note.startTime + note.duration);
    }
    const std::size_t totalSamples = static_cast<std::size_t>(
        std::max(0.0, std::ceil(maxTime * baseConfig_.sampleRate)));
    if (totalSamples == 0) {
        return {};
    }

    std::vector<std::vector<float>> noteBuffers;
    std::vector<std::size_t> offsets;
    noteBuffers.reserve(notes.size());
        offsets.reserve(notes.size());

    for (const auto& note : notes) {
        KarplusStrongString string(baseConfig_);
        auto samples = string.pluck(note.frequency, note.duration, 1.0f);
        if (samples.empty()) {
            continue;
        }
        noteBuffers.emplace_back(std::move(samples));
        const std::size_t offsetSamples =
            static_cast<std::size_t>(std::max(0.0, std::floor(note.startTime * baseConfig_.sampleRate)));
        offsets.emplace_back(offsetSamples);
    }

    auto mixed = mixSamples(noteBuffers, offsets);
    const float peak = findPeak(mixed);
    normalize(mixed, peak);
    return mixed;
}

std::vector<float> KarplusStrongSynth::renderChord(
    const std::vector<double>& frequencies,
    double durationSeconds) const {
    std::vector<NoteEvent> notes;
    notes.reserve(frequencies.size());
    for (double freq : frequencies) {
        notes.push_back({freq, durationSeconds, 0.0});
    }
    return renderNotes(notes);
}

std::vector<float> KarplusStrongSynth::mixSamples(
    const std::vector<std::vector<float>>& noteSamples,
    const std::vector<std::size_t>& offsets) const {
    if (noteSamples.empty()) {
        return {};
    }

    std::size_t maxSamples = 0;
    for (std::size_t i = 0; i < noteSamples.size(); ++i) {
        maxSamples = std::max(
            maxSamples, offsets[i] + noteSamples[i].size());
    }

    std::vector<float> buffer(maxSamples, 0.0f);
    for (std::size_t i = 0; i < noteSamples.size(); ++i) {
        const auto& samples = noteSamples[i];
        const auto offset = offsets[i];
        for (std::size_t j = 0; j < samples.size(); ++j) {
            buffer[offset + j] += samples[j];
        }
    }

    return buffer;
}

float KarplusStrongSynth::findPeak(const std::vector<float>& buffer) const {
    float peak = 0.0f;
    for (float sample : buffer) {
        peak = std::max(peak, std::abs(sample));
    }
    return peak;
}

void KarplusStrongSynth::normalize(std::vector<float>& buffer,
                                   float peak) const {
    if (peak <= 1.0f || peak == 0.0f) {
        return;
    }
    const float invPeak = 1.0f / peak;
    for (auto& sample : buffer) {
        sample *= invPeak;
    }
}

}  // namespace synthesis
