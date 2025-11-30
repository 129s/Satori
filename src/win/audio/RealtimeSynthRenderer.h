#pragma once

#include <mutex>
#include <vector>

#include "synthesis/KarplusStrongString.h"

namespace winaudio {

class RealtimeSynthRenderer {
public:
    explicit RealtimeSynthRenderer(synthesis::StringConfig config = {});

    void setConfig(const synthesis::StringConfig& config);
    void enqueueNote(double frequency, double durationSeconds);
    void render(float* output, std::size_t frames, uint16_t channels);

private:
    struct ActiveVoice {
        std::vector<float> buffer;
        std::size_t cursor = 0;
    };

    synthesis::StringConfig config_;
    std::vector<ActiveVoice> voices_;
    std::mutex mutex_;
};

}  // namespace winaudio
