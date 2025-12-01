#pragma once

#include <vector>

#include <d2d1.h>

namespace winui {

class WaveformView {
public:
    void setBounds(const D2D1_RECT_F& bounds) { bounds_ = bounds; }
    void setSamples(std::vector<float> samples);

    void draw(ID2D1HwndRenderTarget* target,
              ID2D1SolidColorBrush* background,
              ID2D1SolidColorBrush* grid,
              ID2D1SolidColorBrush* waveform) const;

private:
    D2D1_RECT_F bounds_{};
    std::vector<float> samples_;
};

}  // namespace winui
