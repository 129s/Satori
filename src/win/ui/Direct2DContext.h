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
#include "win/ui/VirtualKeyboard.h"

namespace winui {

class UILayoutNode;
class UIStackPanel;
class UIHorizontalStack;
class UIOverlay;
class TopBarNode;
class FlowDiagramNode;
class KnobPanelNode;
class ButtonBarNode;
class KeyboardNode;
class WaveformNode;
class ModulePreviewNode;
class ModuleCardNode;
class DropdownSelectorNode;
class RoomReverbPreviewNode;

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
    void updateDiagramState(const FlowDiagramState& state);
    void syncSliders();
    bool onPointerDown(float x, float y);
    bool onPointerMove(float x, float y);
    void onPointerUp();
    bool onPointerLeave();
    bool hasPointerCapture() const { return pointerCaptured_; }
    bool pressKeyboardKey(int midiNote);
    void releaseKeyboardKey(int midiNote);
    void releaseAllKeyboardKeys();

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
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> excitationBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> textBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> trackBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> fillBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> panelBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> cardBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> shadowBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> gridBrush_;
    Microsoft::WRL::ComPtr<ID2D1GradientStopCollection> accentFillStops_;
    Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> accentFillBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> keyboardWhiteFillBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> keyboardWhitePressedBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> keyboardWhiteTextBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> keyboardBlackFillBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> keyboardBlackPressedBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> keyboardBlackTextBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> keyboardBorderBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> keyboardHoverBrush_;
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
    std::shared_ptr<ButtonBarNode> buttonBarNode_;
    std::shared_ptr<UIHorizontalStack> mainRow_;
    std::shared_ptr<UILayoutNode> leftColumn_;
    std::shared_ptr<UILayoutNode> rightColumn_;
    // Unified main UI: 4 module preview cards + per-module knob panels.
    std::shared_ptr<ModulePreviewNode> excitationPreviewNode_;
    std::shared_ptr<ModulePreviewNode> stringPreviewNode_;
    std::shared_ptr<ModulePreviewNode> bodyPreviewNode_;
    std::shared_ptr<UILayoutNode> roomPreviewNode_;
    std::shared_ptr<RoomReverbPreviewNode> roomReverbPreviewNode_;
    std::shared_ptr<DropdownSelectorNode> roomIrSelectorNode_;
    std::shared_ptr<ModuleCardNode> excitationCardNode_;
    std::shared_ptr<ModuleCardNode> stringCardNode_;
    std::shared_ptr<ModuleCardNode> bodyCardNode_;
    std::shared_ptr<ModuleCardNode> roomCardNode_;

    std::shared_ptr<KnobPanelNode> excitationKnobsNode_;
    std::shared_ptr<KnobPanelNode> stringKnobsNode_;
    std::shared_ptr<KnobPanelNode> bodyKnobsNode_;
    std::shared_ptr<KnobPanelNode> roomKnobsNode_;
    std::shared_ptr<KeyboardNode> keyboardNode_;
    KeyboardColors keyboardColors_{};
};

}  // namespace winui
