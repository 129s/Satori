#include "win/ui/WaveformView.h"

#include <algorithm>

#include <d2d1helper.h>

namespace winui {

void WaveformView::setSamples(std::vector<float> samples) {
    samples_ = std::move(samples);
}

void WaveformView::draw(ID2D1HwndRenderTarget* target,
                        ID2D1SolidColorBrush* background,
                        ID2D1SolidColorBrush* grid,
                        ID2D1SolidColorBrush* waveform) const {
    if (!target || !background || !grid || !waveform) {
        return;
    }
    target->FillRectangle(bounds_, background);

    const float width = bounds_.right - bounds_.left;
    const float height = bounds_.bottom - bounds_.top;
    if (width <= 0.0f || height <= 0.0f) {
        return;
    }

    const float midY = bounds_.top + height * 0.5f;
    target->DrawLine(D2D1::Point2F(bounds_.left, midY),
                     D2D1::Point2F(bounds_.right, midY), grid, 1.0f);

    if (samples_.size() < 2) {
        return;
    }

    const float scaleY = height * 0.45f;
    float prevX = bounds_.left;
    float prevY = midY - std::clamp(samples_[0], -1.0f, 1.0f) * scaleY;

    const float step = width / static_cast<float>(samples_.size() - 1);
    for (std::size_t i = 1; i < samples_.size(); ++i) {
        const float x = bounds_.left + step * static_cast<float>(i);
        const float y =
            midY - std::clamp(samples_[i], -1.0f, 1.0f) * scaleY;
        target->DrawLine(D2D1::Point2F(prevX, prevY), D2D1::Point2F(x, y),
                         waveform, 2.0f);
        prevX = x;
        prevY = y;
    }
}

}  // namespace winui
