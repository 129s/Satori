#pragma once

#include <vector>

#include <d2d1.h>
#include <wrl/client.h>

#if defined(SATORI_ENABLE_UI_DEBUG)
#define SATORI_UI_DEBUG_ENABLED 1
#else
#define SATORI_UI_DEBUG_ENABLED 0
#endif

namespace winui {

enum class DebugOverlayMode {
    kOff,
    kBoxModel,
};

enum class DebugBoxLayer {
    kBorder,
    kPadding,
    kContent,
};

struct DebugBoxSegment {
    DebugBoxLayer layer = DebugBoxLayer::kBorder;
    D2D1_RECT_F rect{};
};

struct DebugBoxModel {
    std::vector<DebugBoxSegment> segments;
};

struct DebugOverlayPalette {
    D2D1_COLOR_F stroke{};
    float strokeWidth = 1.0f;
};

DebugOverlayPalette MakeUnifiedDebugOverlayPalette();

#if SATORI_UI_DEBUG_ENABLED
class DebugBoxRenderer {
public:
    DebugBoxRenderer();

    void setPalette(const DebugOverlayPalette& palette);
    void render(ID2D1HwndRenderTarget* target,
                const DebugBoxModel& model,
                bool alignToPixel = false);

private:
    void ensureBrushes(ID2D1HwndRenderTarget* target);

    DebugOverlayPalette palette_{};
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> strokeBrush_;
};
#else
class DebugBoxRenderer {
public:
    void setPalette(const DebugOverlayPalette&) {}
    void render(ID2D1HwndRenderTarget*, const DebugBoxModel&, bool = false) {}
};
#endif  // SATORI_UI_DEBUG_ENABLED

}  // namespace winui
