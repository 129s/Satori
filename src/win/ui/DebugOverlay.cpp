#include "win/ui/DebugOverlay.h"

#include <d2d1helper.h>

#include "win/ui/D2DHelpers.h"

namespace winui {
namespace {
constexpr float kDefaultStrokeWidth = 1.0f;
}  // namespace

DebugOverlayPalette MakeUnifiedDebugOverlayPalette() {
    DebugOverlayPalette palette{};
    palette.stroke = D2D1::ColorF(1.0f, 1.0f, 0.0f, 1.0f);
    palette.strokeWidth = kDefaultStrokeWidth;
    return palette;
}

#if SATORI_UI_DEBUG_ENABLED
DebugBoxRenderer::DebugBoxRenderer() {
    palette_ = MakeUnifiedDebugOverlayPalette();
}

void DebugBoxRenderer::setPalette(const DebugOverlayPalette& palette) {
    palette_ = palette;
    strokeBrush_.Reset();
}

void DebugBoxRenderer::render(ID2D1HwndRenderTarget* target,
                              const DebugBoxModel& model,
                              bool alignToPixel) {
    if (!target || model.segments.empty()) {
        return;
    }

    ensureBrushes(target);
    if (!strokeBrush_) {
        return;
    }
    for (const auto& segment : model.segments) {
        const auto rect = alignToPixel
                              ? AlignRectToPixel(segment.rect)
                              : segment.rect;
        target->DrawRectangle(rect, strokeBrush_.Get(),
                              palette_.strokeWidth);
    }
}

void DebugBoxRenderer::ensureBrushes(ID2D1HwndRenderTarget* target) {
    if (!target) {
        return;
    }
    if (!strokeBrush_) {
        target->CreateSolidColorBrush(palette_.stroke, &strokeBrush_);
    }
}
#endif

}  // namespace winui
