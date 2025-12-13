#pragma once

#include <d2d1.h>
#include <dwrite.h>

#include "win/ui/UISkin.h"

namespace winui {

// 在一次绘制遍历中需要用到的渲染资源与皮肤信息。
struct RenderResources {
    ID2D1HwndRenderTarget* target = nullptr;
    ID2D1SolidColorBrush* accentBrush = nullptr;
    ID2D1SolidColorBrush* excitationBrush = nullptr;
    ID2D1LinearGradientBrush* accentFillBrush = nullptr;  // Optional (module visualizers)
    ID2D1SolidColorBrush* textBrush = nullptr;
    ID2D1SolidColorBrush* trackBrush = nullptr;
    ID2D1SolidColorBrush* fillBrush = nullptr;
    ID2D1SolidColorBrush* panelBrush = nullptr;
    ID2D1SolidColorBrush* cardBrush = nullptr;   // Module card background
    ID2D1SolidColorBrush* shadowBrush = nullptr; // Subtle drop shadows for cards
    ID2D1SolidColorBrush* gridBrush = nullptr;
    IDWriteTextFormat* textFormat = nullptr;

    // 当前使用的 UI 皮肤（只读视图，由 Direct2DContext 填充）。
    UISkinId skinId = UISkinId::kDefault;
    const UISkinResources* skin = nullptr;
};

}  // namespace winui
