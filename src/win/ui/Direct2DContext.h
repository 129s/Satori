#pragma once

#include <windows.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <d2d1.h>
#include <dwrite.h>
#include <dwrite_3.h>
#include <wrl/client.h>

#include "win/ui/DebugOverlay.h"
#include "win/ui/RenderResources.h"
#include "win/ui/UIModel.h"
#include "win/ui/layout/UILayoutNode.h"
#include "win/ui/UISkin.h"

namespace winui {

class UILayoutNode;
class UIStackPanel;
class UIOverlay;
class TopBarNode;
class FlowDiagramNode;
class KnobPanelNode;
class KeyboardNode;

class Direct2DContext {
public:
    Direct2DContext();
    ~Direct2DContext();

    bool initialize(HWND hwnd);
    void resize(UINT width, UINT height);
    void render();
    void handleDeviceLost();

    void setModel(UIModel model);
    void updateWaveformSamples(const std::vector<float>& samples);
    void syncSliders();
    bool onPointerDown(float x, float y);
    bool onPointerMove(float x, float y);
    void onPointerUp();
    bool onPointerLeave();
    bool hasPointerCapture() const { return pointerCaptured_; }

    void setDebugOverlayMode(DebugOverlayMode mode);
    void toggleDebugOverlay();
    void dumpLayoutDebugInfo();

private:
    bool createDeviceResources();
    void discardDeviceResources();
    void rebuildLayout();
    void ensureLayout();
    RenderResources makeResources();
    void drawDebugOverlay();
    void applyDebugOverlayState();
    bool updateDebugSelection(float x, float y);
    bool clearDebugSelection();
    std::optional<DebugBoxModel> pickDebugSelection(float x, float y) const;

    HWND hwnd_ = nullptr;
    UINT width_ = 0;
    UINT height_ = 0;
    Microsoft::WRL::ComPtr<ID2D1Factory> d2dFactory_;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> renderTarget_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> accentBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> textBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> trackBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> fillBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> panelBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> gridBrush_;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwriteFactory_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> textFormat_;
    Microsoft::WRL::ComPtr<IDWriteFontCollection1> nunitoFontCollection_;

    UISkinConfig skinConfig_{};
    UISkinResources skinResources_{};

    UIModel model_;
    bool layoutDirty_ = true;
    DebugOverlayMode debugOverlayMode_ = DebugOverlayMode::kOff;
    DebugOverlayPalette debugOverlayPalette_{};
    DebugBoxRenderer debugBoxRenderer_;
#if SATORI_UI_DEBUG_ENABLED
    std::optional<DebugBoxModel> hoverDebugModel_;
    bool pointerCaptured_ = false;
    bool pointerInside_ = false;
    bool hasPointerPosition_ = false;
    D2D1_POINT_2F lastPointerPosition_{};
#else
    bool pointerCaptured_ = false;
#endif
    std::shared_ptr<UILayoutNode> rootLayout_;
    std::shared_ptr<TopBarNode> topBarNode_;
    std::shared_ptr<FlowDiagramNode> flowNode_;
    std::shared_ptr<KnobPanelNode> knobPanelNode_;
    std::shared_ptr<KeyboardNode> keyboardNode_;
};

}  // namespace winui
