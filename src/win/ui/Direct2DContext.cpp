#include "win/ui/Direct2DContext.h"

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <sstream>

#include <d2d1helper.h>

#include "win/ui/D2DHelpers.h"
#include "win/ui/NunitoFont.h"
#include "win/ui/RenderResources.h"
#include "win/ui/UIModel.h"
#include "dsp/RoomIrLibrary.h"
#include "win/ui/nodes/FlowDiagramNode.h"
#include "win/ui/nodes/ButtonBarNode.h"
#include "win/ui/nodes/HeaderBarNode.h"
#include "win/ui/nodes/KnobPanelNode.h"
#include "win/ui/nodes/KeyboardNode.h"
#include "win/ui/nodes/WaveformNode.h"
#include "win/ui/nodes/ModuleCardNode.h"
#include "win/ui/nodes/ModulePreviewNode.h"
#include "win/ui/nodes/DropdownSelectorNode.h"
#include "win/ui/nodes/RoomReverbPreviewNode.h"
#include "win/ui/layout/UIHorizontalStack.h"
#include "win/ui/layout/UIStackPanel.h"

namespace winui {

namespace {

UISkinConfig MakeDefaultSkinConfig() {
    UISkinConfig config;
    config.id = UISkinId::kDefault;
    config.name = L"Default";
    config.assetsBaseDir = L"assets/ui/default";
    config.primaryFontFamily = L"Nunito";
    config.baseFontSize = 18.0f;
    return config;
}

struct SkinBrushColors {
    D2D1_COLOR_F accent{};
    D2D1_COLOR_F text{};
    D2D1_COLOR_F track{};
    D2D1_COLOR_F fill{};
    D2D1_COLOR_F panel{};
    D2D1_COLOR_F card{};
    D2D1_COLOR_F grid{};
};

SkinBrushColors MakeBrushColors() {
    SkinBrushColors colors{};
    // Dark, layered UI with a warm accent. Background is cleared separately.
    // Cyber cyan accent.
    colors.accent = D2D1::ColorF(0.0f, 1.0f, 0.78f, 1.0f);   // #00FFC8
    colors.text = D2D1::ColorF(0.92f, 0.92f, 0.92f, 1.0f);
    colors.track = D2D1::ColorF(0.17f, 0.17f, 0.17f, 1.0f); // #2B2B2B
    colors.fill = D2D1::ColorF(0.22f, 0.22f, 0.22f, 1.0f);  // secondary fill
    // "Panel" is the darker content surface used behind knobs and visualizers.
    colors.panel = D2D1::ColorF(0.12f, 0.12f, 0.12f, 1.0f); // #1E1E1E
    // "Card" is the module container background.
    colors.card = D2D1::ColorF(0.172549f, 0.172549f, 0.172549f, 1.0f);  // #2C2C2C
    colors.grid = D2D1::ColorF(0.45f, 0.45f, 0.45f, 0.25f); // subtle helpers
    return colors;
}

bool IsRectValid(const D2D1_RECT_F& rect) {
    return rect.right > rect.left && rect.bottom > rect.top;
}

D2D1_RECT_F InsetRect(const D2D1_RECT_F& rect, float inset) {
    return D2D1::RectF(rect.left + inset, rect.top + inset,
                       rect.right - inset, rect.bottom - inset);
}

bool ContainsPoint(const D2D1_RECT_F& rect, float x, float y) {
    return x >= rect.left && x <= rect.right && y >= rect.top &&
           y <= rect.bottom;
}

DebugBoxModel MakeLayoutBox(const D2D1_RECT_F& rect) {
    DebugBoxModel model{};
    if (!IsRectValid(rect)) {
        return model;
    }
    model.segments.push_back({DebugBoxLayer::kBorder, rect});

    const float paddingInset = 6.0f;
    const auto padding = InsetRect(rect, paddingInset);
    if (IsRectValid(padding)) {
        model.segments.push_back({DebugBoxLayer::kPadding, padding});

        const float contentInset = 8.0f;
        const auto content = InsetRect(padding, contentInset);
        if (IsRectValid(content)) {
            model.segments.push_back({DebugBoxLayer::kContent, content});
        }
    }
    return model;
}

bool AreRectsClose(const D2D1_RECT_F& a, const D2D1_RECT_F& b) {
    const float kTolerance = 0.25f;
    return std::fabs(a.left - b.left) < kTolerance &&
           std::fabs(a.top - b.top) < kTolerance &&
           std::fabs(a.right - b.right) < kTolerance &&
           std::fabs(a.bottom - b.bottom) < kTolerance;
}

bool AreModelsEqual(const DebugBoxModel& lhs, const DebugBoxModel& rhs) {
    if (lhs.segments.size() != rhs.segments.size()) {
        return false;
    }
    for (std::size_t i = 0; i < lhs.segments.size(); ++i) {
        const auto& a = lhs.segments[i];
        const auto& b = rhs.segments[i];
        if (a.layer != b.layer || !AreRectsClose(a.rect, b.rect)) {
            return false;
        }
    }
    return true;
}

}  // namespace

Direct2DContext::Direct2DContext() {
    skinConfig_ = MakeDefaultSkinConfig();
    skinResources_.config = skinConfig_;
#if SATORI_UI_DEBUG_ENABLED
    debugOverlayPalette_ = MakeUnifiedDebugOverlayPalette();
#endif
}
Direct2DContext::~Direct2DContext() = default;

bool Direct2DContext::initialize(HWND hwnd) {
    hwnd_ = hwnd;
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    width_ = static_cast<UINT>(rc.right - rc.left);
    height_ = static_cast<UINT>(rc.bottom - rc.top);
    D2D1_FACTORY_OPTIONS options{};
#if defined(_DEBUG)
    options.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                   __uuidof(ID2D1Factory), &options,
                                   &d2dFactory_);
    if (FAILED(hr)) {
        return false;
    }
    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                             __uuidof(IDWriteFactory),
                             &dwriteFactory_);
    if (FAILED(hr)) {
        return false;
    }

    // 按当前皮肤配置创建文本格式，Nunito 为全局默认字体。
    const wchar_t* primaryFont =
        skinConfig_.primaryFontFamily.empty()
            ? NunitoFontFamily()
            : skinConfig_.primaryFontFamily.c_str();
    const float fontSize =
        skinConfig_.baseFontSize > 0.0f ? skinConfig_.baseFontSize : 18.0f;

    const bool nunitoAvailable = EnsureNunitoFontLoaded();
    nunitoFontCollection_ = CreateNunitoFontCollection(dwriteFactory_.Get());
    const bool useNunitoCollection =
        primaryFont &&
        std::wcscmp(primaryFont, NunitoFontFamily()) == 0;
    if (useNunitoCollection && !nunitoAvailable && !nunitoFontCollection_) {
        return false;
    }
    { Microsoft::WRL::ComPtr<IDWriteFontCollection> coll; dwriteFactory_->GetSystemFontCollection(&coll, TRUE); }
    hr = dwriteFactory_->CreateTextFormat(
        primaryFont,
        useNunitoCollection ? nunitoFontCollection_.Get() : nullptr,
        DWRITE_FONT_WEIGHT_REGULAR,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, fontSize,
        L"zh-CN", &textFormat_);
    if (FAILED(hr)) {
        return false;
    }
    ApplyChineseFontFallback(dwriteFactory_.Get(), textFormat_.Get());
    return createDeviceResources();
}

void Direct2DContext::resize(UINT width, UINT height) {
    width_ = width;
    height_ = height;
    if (renderTarget_) {
        renderTarget_->Resize(D2D1::SizeU(width, height));
    }
    layoutDirty_ = true;
}

void Direct2DContext::handleDeviceLost() {
    discardDeviceResources();
    createDeviceResources();
    layoutDirty_ = true;
}

void Direct2DContext::setModel(UIModel model) {
    model_ = std::move(model);
    rebuildLayout();
}

void Direct2DContext::updateWaveformSamples(
    const std::vector<float>& samples) {
    model_.waveformSamples = samples;
    // Room preview uses the selected IR waveform (updated via FlowDiagramState), not output waveform samples.
}

void Direct2DContext::updateDiagramState(const FlowDiagramState& state) {
    model_.diagram = state;
    if (excitationPreviewNode_) {
        excitationPreviewNode_->setDiagramState(state);
    }
    if (stringPreviewNode_) {
        stringPreviewNode_->setDiagramState(state);
    }
    if (bodyPreviewNode_) {
        bodyPreviewNode_->setDiagramState(state);
    }
    if (roomReverbPreviewNode_) {
        roomReverbPreviewNode_->setDiagramState(state);
    }
}

void Direct2DContext::syncSliders() {
    if (excitationKnobsNode_) excitationKnobsNode_->syncKnobs();
    if (stringKnobsNode_) stringKnobsNode_->syncKnobs();
    if (bodyKnobsNode_) bodyKnobsNode_->syncKnobs();
    if (roomKnobsNode_) roomKnobsNode_->syncKnobs();
}

bool Direct2DContext::onPointerDown(float x, float y) {
#if SATORI_UI_DEBUG_ENABLED
    pointerInside_ = true;
    hasPointerPosition_ = true;
    lastPointerPosition_ = D2D1::Point2F(x, y);
#endif
    bool handled = false;
    // If a dropdown is open, route the click to its overlay first so it can
    // select/close without underlying knobs reacting.
    auto routeOverlayDown = [&](const std::shared_ptr<DropdownSelectorNode>& node) {
        return node && node->isOpen() && node->onOverlayPointerDown(x, y);
    };
    if ((headerBarNode_ &&
         (routeOverlayDown(headerBarNode_->deviceSelector()) ||
          routeOverlayDown(headerBarNode_->sampleRateSelector()) ||
          routeOverlayDown(headerBarNode_->bufferFramesSelector()))) ||
        routeOverlayDown(roomIrSelectorNode_)) {
#if SATORI_UI_DEBUG_ENABLED
        const bool selectionChanged = updateDebugSelection(x, y);
        pointerCaptured_ = true;
        (void)selectionChanged;
        return true;
#else
        pointerCaptured_ = true;
        return true;
#endif
    }
    if (rootLayout_) {
        handled = rootLayout_->onPointerDown(x, y);
    }
    pointerCaptured_ = handled;
#if SATORI_UI_DEBUG_ENABLED
    const bool selectionChanged = updateDebugSelection(x, y);
    return handled || selectionChanged;
#else
    return handled;
#endif
}

bool Direct2DContext::onPointerMove(float x, float y) {
#if SATORI_UI_DEBUG_ENABLED
    pointerInside_ = true;
    hasPointerPosition_ = true;
    lastPointerPosition_ = D2D1::Point2F(x, y);
#endif
    bool handled = false;
    auto routeOverlayMove = [&](const std::shared_ptr<DropdownSelectorNode>& node) {
        return node && node->isOpen() && node->onOverlayPointerMove(x, y);
    };
    if ((headerBarNode_ &&
         (routeOverlayMove(headerBarNode_->deviceSelector()) ||
          routeOverlayMove(headerBarNode_->sampleRateSelector()) ||
          routeOverlayMove(headerBarNode_->bufferFramesSelector()))) ||
        routeOverlayMove(roomIrSelectorNode_)) {
#if SATORI_UI_DEBUG_ENABLED
        const bool selectionChanged = updateDebugSelection(x, y);
        (void)selectionChanged;
        return true;
#else
        return true;
#endif
    }
    if (rootLayout_) {
        handled = rootLayout_->onPointerMove(x, y);
    }
#if SATORI_UI_DEBUG_ENABLED
    const bool selectionChanged = updateDebugSelection(x, y);
    return handled || selectionChanged;
#else
    return handled;
#endif
}

void Direct2DContext::onPointerUp() {
    if (rootLayout_) {
        rootLayout_->onPointerUp();
    }
#if SATORI_UI_DEBUG_ENABLED
    pointerCaptured_ = false;
    if (hasPointerPosition_) {
        updateDebugSelection(lastPointerPosition_.x, lastPointerPosition_.y);
    }
#else
    pointerCaptured_ = false;
#endif
}

bool Direct2DContext::onPointerLeave() {
#if SATORI_UI_DEBUG_ENABLED
    pointerInside_ = false;
    hasPointerPosition_ = false;
    if (pointerCaptured_) {
        return false;
    }
    return clearDebugSelection();
#else
    return false;
#endif
}

bool Direct2DContext::pressKeyboardKey(int midiNote) {
    if (!keyboardNode_) {
        return false;
    }
    const bool handled = keyboardNode_->pressKeyByMidi(midiNote);
    if (handled) {
        layoutDirty_ = true;
    }
    return handled;
}

void Direct2DContext::releaseKeyboardKey(int midiNote) {
    if (!keyboardNode_) {
        return;
    }
    keyboardNode_->releaseKeyByMidi(midiNote);
}

void Direct2DContext::releaseAllKeyboardKeys() {
    if (keyboardNode_) {
        keyboardNode_->releaseAllKeys();
    }
}

void Direct2DContext::setDebugOverlayMode(DebugOverlayMode mode) {
#if SATORI_UI_DEBUG_ENABLED
    if (debugOverlayMode_ == mode) {
        return;
    }
    debugOverlayMode_ = mode;
    applyDebugOverlayState();
#else
    (void)mode;
#endif
}

void Direct2DContext::toggleDebugOverlay() {
#if SATORI_UI_DEBUG_ENABLED
    const auto nextMode =
        debugOverlayMode_ == DebugOverlayMode::kOff
            ? DebugOverlayMode::kBoxModel
            : DebugOverlayMode::kOff;
    setDebugOverlayMode(nextMode);
#endif
}

void Direct2DContext::dumpLayoutDebugInfo() {
    if (!rootLayout_) {
        return;
    }

    auto formatRect = [](const D2D1_RECT_F& r) {
        std::wstringstream ss;
        ss << L"(" << static_cast<int>(r.left) << L"," << static_cast<int>(r.top)
           << L")-(" << static_cast<int>(r.right) << L","
           << static_cast<int>(r.bottom) << L")";
        return ss.str();
    };

    std::wstringstream output;
    output << L"[SatoriWin][Layout] client=" << width_ << L"x" << height_
           << L"\n";
    output << L"  root      " << formatRect(rootLayout_->bounds()) << L"\n";
    if (headerBarNode_) {
        output << L"  header    " << formatRect(headerBarNode_->bounds()) << L"\n";
    }
    if (buttonBarNode_) {
        output << L"  buttons   " << formatRect(buttonBarNode_->bounds()) << L"\n";
    }
    if (excitationPreviewNode_) {
        output << L"  excitation " << formatRect(excitationPreviewNode_->bounds()) << L"\n";
    }
    if (stringPreviewNode_) {
        output << L"  string    " << formatRect(stringPreviewNode_->bounds()) << L"\n";
    }
    if (bodyPreviewNode_) {
        output << L"  body      " << formatRect(bodyPreviewNode_->bounds()) << L"\n";
    }
    if (roomPreviewNode_) {
        output << L"  room      " << formatRect(roomPreviewNode_->bounds()) << L"\n";
    }
    if (excitationKnobsNode_) {
        output << L"  ex-knobs  " << formatRect(excitationKnobsNode_->bounds()) << L"\n";
    }
    if (stringKnobsNode_) {
        output << L"  st-knobs  " << formatRect(stringKnobsNode_->bounds()) << L"\n";
    }
    if (bodyKnobsNode_) {
        output << L"  bd-knobs  " << formatRect(bodyKnobsNode_->bounds()) << L"\n";
    }
    if (roomKnobsNode_) {
        output << L"  rm-knobs  " << formatRect(roomKnobsNode_->bounds()) << L"\n";
    }
    if (keyboardNode_) {
        output << L"  keyboard  " << formatRect(keyboardNode_->bounds())
               << L"\n";
    }

    const auto text = output.str();
    OutputDebugStringW(text.c_str());
}

bool Direct2DContext::createDeviceResources() {
    if (renderTarget_) {
        return true;
    }
    RECT rc{};
    if (!GetClientRect(hwnd_, &rc)) {
        return false;
    }
    const auto size =
        D2D1::SizeU(static_cast<UINT>(rc.right - rc.left),
                    static_cast<UINT>(rc.bottom - rc.top));

    HRESULT hr = d2dFactory_->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(),
        D2D1::HwndRenderTargetProperties(hwnd_, size),
        &renderTarget_);
    if (FAILED(hr)) {
        return false;
    }

    const SkinBrushColors colors = MakeBrushColors();

    hr = renderTarget_->CreateSolidColorBrush(colors.accent, &accentBrush_);
    if (FAILED(hr)) {
        return false;
    }
    // Excitation Scope uses the same accent by default.
    hr = renderTarget_->CreateSolidColorBrush(colors.accent, &excitationBrush_);
    if (FAILED(hr)) {
        return false;
    }
    hr = renderTarget_->CreateSolidColorBrush(colors.text, &textBrush_);
    if (FAILED(hr)) {
        return false;
    }
    hr = renderTarget_->CreateSolidColorBrush(colors.track, &trackBrush_);
    if (FAILED(hr)) {
        return false;
    }
    hr = renderTarget_->CreateSolidColorBrush(colors.fill, &fillBrush_);
    if (FAILED(hr)) {
        return false;
    }
    hr = renderTarget_->CreateSolidColorBrush(colors.panel, &panelBrush_);
    if (FAILED(hr)) {
        return false;
    }
    hr = renderTarget_->CreateSolidColorBrush(colors.card, &cardBrush_);
    if (FAILED(hr)) {
        return false;
    }
    // Subtle shadow used by card containers (cheap, no blur).
    hr = renderTarget_->CreateSolidColorBrush(
        D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.30f), &shadowBrush_);
    if (FAILED(hr)) {
        return false;
    }
    hr = renderTarget_->CreateSolidColorBrush(colors.grid, &gridBrush_);
    if (FAILED(hr)) {
        return false;
    }

    // Accent gradient fill for visualizers (top->bottom fade to transparent).
    D2D1_GRADIENT_STOP stops[2]{};
    stops[0].position = 0.0f;
    stops[0].color = D2D1::ColorF(colors.accent.r, colors.accent.g, colors.accent.b, 0.22f);
    stops[1].position = 1.0f;
    stops[1].color = D2D1::ColorF(colors.accent.r, colors.accent.g, colors.accent.b, 0.0f);
    hr = renderTarget_->CreateGradientStopCollection(
        stops, 2,
        D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &accentFillStops_);
    if (FAILED(hr)) {
        return false;
    }
    hr = renderTarget_->CreateLinearGradientBrush(
        D2D1::LinearGradientBrushProperties(D2D1::Point2F(0.0f, 0.0f),
                                            D2D1::Point2F(0.0f, 100.0f)),
        accentFillStops_.Get(),
        &accentFillBrush_);
    if (FAILED(hr)) {
        return false;
    }
    // 虚拟键盘配色复用 KeyboardSandbox 方案。
    hr = renderTarget_->CreateSolidColorBrush(
        D2D1::ColorF(0.94f, 0.94f, 0.95f, 1.0f), &keyboardWhiteFillBrush_);
    if (FAILED(hr)) {
        return false;
    }
    hr = renderTarget_->CreateSolidColorBrush(
        D2D1::ColorF(0.88f, 0.88f, 0.90f, 1.0f), &keyboardWhitePressedBrush_);
    if (FAILED(hr)) {
        return false;
    }
    hr = renderTarget_->CreateSolidColorBrush(
        D2D1::ColorF(0.10f, 0.10f, 0.10f, 1.0f), &keyboardWhiteTextBrush_);
    if (FAILED(hr)) {
        return false;
    }
    hr = renderTarget_->CreateSolidColorBrush(
        D2D1::ColorF(0.08f, 0.08f, 0.10f, 1.0f), &keyboardBlackFillBrush_);
    if (FAILED(hr)) {
        return false;
    }
    hr = renderTarget_->CreateSolidColorBrush(
        D2D1::ColorF(0.25f, 0.25f, 0.25f, 1.0f), &keyboardBlackPressedBrush_);
    if (FAILED(hr)) {
        return false;
    }
    hr = renderTarget_->CreateSolidColorBrush(
        D2D1::ColorF(0.98f, 0.98f, 0.99f, 1.0f), &keyboardBlackTextBrush_);
    if (FAILED(hr)) {
        return false;
    }
    hr = renderTarget_->CreateSolidColorBrush(
        D2D1::ColorF(0.22f, 0.22f, 0.22f, 1.0f), &keyboardBorderBrush_);
    if (FAILED(hr)) {
        return false;
    }
    hr = renderTarget_->CreateSolidColorBrush(
        colors.accent, &keyboardHoverBrush_);
    if (FAILED(hr)) {
        return false;
    }

    keyboardColors_.whiteFill = keyboardWhiteFillBrush_.Get();
    keyboardColors_.whitePressed = keyboardWhitePressedBrush_.Get();
    keyboardColors_.whiteBorder = keyboardBorderBrush_.Get();
    keyboardColors_.whiteText = keyboardWhiteTextBrush_.Get();
    keyboardColors_.blackFill = keyboardBlackFillBrush_.Get();
    keyboardColors_.blackPressed = keyboardBlackPressedBrush_.Get();
    keyboardColors_.blackBorder = keyboardBorderBrush_.Get();
    keyboardColors_.blackText = keyboardBlackTextBrush_.Get();
    keyboardColors_.hoverOutline = keyboardHoverBrush_.Get();
    if (keyboardNode_) {
        keyboardNode_->setColors(keyboardColors_);
    }

    debugOverlayPalette_ = MakeUnifiedDebugOverlayPalette();
    debugBoxRenderer_.setPalette(debugOverlayPalette_);
    return true;
}

void Direct2DContext::discardDeviceResources() {
    accentBrush_.Reset();
    excitationBrush_.Reset();
    accentFillBrush_.Reset();
    accentFillStops_.Reset();
    textBrush_.Reset();
    trackBrush_.Reset();
    fillBrush_.Reset();
    panelBrush_.Reset();
    cardBrush_.Reset();
    shadowBrush_.Reset();
    gridBrush_.Reset();
    keyboardWhiteFillBrush_.Reset();
    keyboardWhitePressedBrush_.Reset();
    keyboardWhiteTextBrush_.Reset();
    keyboardBlackFillBrush_.Reset();
    keyboardBlackPressedBrush_.Reset();
    keyboardBlackTextBrush_.Reset();
    keyboardBorderBrush_.Reset();
    keyboardHoverBrush_.Reset();
    keyboardColors_ = {};
    renderTarget_.Reset();
    debugBoxRenderer_.setPalette(debugOverlayPalette_);
}

void Direct2DContext::render() {
    if (!createDeviceResources()) {
        return;
    }
    ensureLayout();

    renderTarget_->BeginDraw();
    renderTarget_->SetTransform(D2D1::Matrix3x2F::Identity());

    // Global background (not pure black) to preserve hierarchy against module cards.
    D2D1_COLOR_F clearColor =
        D2D1::ColorF(0.117647f, 0.117647f, 0.117647f);  // #1E1E1E
    renderTarget_->Clear(clearColor);

    if (rootLayout_) {
        FlowModule highlight = FlowModule::kNone;
        auto pickHighlight = [&](const std::shared_ptr<KnobPanelNode>& panel) {
            if (!panel) return false;
            if (auto module = panel->activeModule()) {
                highlight = *module;
                return true;
            }
            return false;
        };
        (void)(pickHighlight(excitationKnobsNode_) ||
               pickHighlight(stringKnobsNode_) ||
               pickHighlight(bodyKnobsNode_) ||
               pickHighlight(roomKnobsNode_));
        if (highlight == FlowModule::kNone) {
            if (excitationPreviewNode_ && excitationPreviewNode_->isInteracting()) {
                highlight = FlowModule::kExcitation;
            }
        }

        model_.diagram.highlightedModule = highlight;
        if (excitationPreviewNode_) excitationPreviewNode_->setHighlighted(highlight == FlowModule::kExcitation);
        if (stringPreviewNode_) stringPreviewNode_->setHighlighted(highlight == FlowModule::kString);
        if (bodyPreviewNode_) bodyPreviewNode_->setHighlighted(highlight == FlowModule::kBody);
        if (excitationCardNode_) excitationCardNode_->setHighlighted(highlight == FlowModule::kExcitation);
        if (stringCardNode_) stringCardNode_->setHighlighted(highlight == FlowModule::kString);
        if (bodyCardNode_) bodyCardNode_->setHighlighted(highlight == FlowModule::kBody);
        if (roomCardNode_) roomCardNode_->setHighlighted(highlight == FlowModule::kRoom);

        auto resources = makeResources();
        rootLayout_->draw(resources);
        auto drawActiveTooltip = [&](const std::shared_ptr<KnobPanelNode>& panel) {
            if (!panel) return false;
            if (auto knob = panel->activeKnob()) {
                knob->drawTooltip(resources.target, resources.trackBrush,
                                  resources.fillBrush, resources.accentBrush,
                                  resources.textBrush, resources.textFormat);
                return true;
            }
            return false;
        };
        (void)(drawActiveTooltip(excitationKnobsNode_) ||
               drawActiveTooltip(stringKnobsNode_) ||
               drawActiveTooltip(bodyKnobsNode_) ||
               drawActiveTooltip(roomKnobsNode_));
        // Draw dropdown overlay last so it appears above knobs/cards.
        auto drawDropdownOverlay = [&](const std::shared_ptr<DropdownSelectorNode>& node) {
            if (node && node->isOpen()) {
                node->drawOverlay(resources);
            }
        };
        if (headerBarNode_) {
            drawDropdownOverlay(headerBarNode_->deviceSelector());
            drawDropdownOverlay(headerBarNode_->sampleRateSelector());
            drawDropdownOverlay(headerBarNode_->bufferFramesSelector());
        }
        drawDropdownOverlay(roomIrSelectorNode_);
#if SATORI_UI_DEBUG_ENABLED
        drawDebugOverlay();
#endif
    }

    HRESULT hr = renderTarget_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        handleDeviceLost();
    }
}

void Direct2DContext::rebuildLayout() {
    headerBarNode_ = std::make_shared<HeaderBarNode>();
    headerBarNode_->setModel(model_.headerBar);
    buttonBarNode_.reset();

    // Unified modules: create one preview + one knob panel per FlowModule.
    auto makePreview = [&](FlowModule module) {
        auto node = std::make_shared<ModulePreviewNode>(module);
        node->setDiagramState(model_.diagram);
        node->setWaveformSamples(model_.waveformSamples);
        node->setOnSelected([this](FlowModule selected) {
            // External highlight by click; knob hover will override in render().
            model_.diagram.highlightedModule = selected;
            layoutDirty_ = true;
        });
        return node;
    };
    excitationPreviewNode_ = makePreview(FlowModule::kExcitation);
    stringPreviewNode_ = makePreview(FlowModule::kString);
    bodyPreviewNode_ = makePreview(FlowModule::kBody);
    roomReverbPreviewNode_ = std::make_shared<RoomReverbPreviewNode>();
    roomReverbPreviewNode_->setDiagramState(model_.diagram);
    roomPreviewNode_ = roomReverbPreviewNode_;
    roomIrSelectorNode_ = roomReverbPreviewNode_->selector();
    if (roomIrSelectorNode_) {
        std::vector<std::wstring> names;
        for (const auto& ir : dsp::RoomIrLibrary::list()) {
            // Names are ASCII in the built-in library; widen for DWrite.
            names.emplace_back(ir.displayName.begin(), ir.displayName.end());
        }
        roomIrSelectorNode_->setItems(std::move(names));
        roomIrSelectorNode_->setPageSize(6);
    }

    // Bind Excitation "Position" to an interactive slider inside the preview card.
    auto findParam = [&](FlowModule module,
                         const std::wstring& label) -> const ModuleParamDescriptor* {
        for (const auto& m : model_.modules) {
            if (m.module != module) continue;
            for (const auto& p : m.params) {
                if (p.label == label) return &p;
            }
        }
        return nullptr;
    };
    if (excitationPreviewNode_) {
        if (const auto* pos = findParam(FlowModule::kExcitation, L"Position")) {
            excitationPreviewNode_->setPickPositionRange(pos->min, pos->max);
            excitationPreviewNode_->setOnPickPositionChanged(
                [setter = pos->setter](float value) {
                    if (setter) setter(value);
                });
        }
    }
    // Bind Room "IR" param to the dropdown selector (if available).
    if (roomIrSelectorNode_) {
        if (const auto* irParam = findParam(FlowModule::kRoom, L"IR")) {
            if (irParam->getter) {
                roomIrSelectorNode_->setSelectedIndex(
                    static_cast<int>(std::lround(irParam->getter())));
            }
            roomIrSelectorNode_->setOnChanged(
                [setter = irParam->setter](int index) {
                    if (setter) {
                        setter(static_cast<float>(index));
                    }
                });
        }
    }

    auto makeKnobs = [&](FlowModule module) {
        auto node = std::make_shared<KnobPanelNode>();
        // Filter modules list down to this module.
        std::vector<ModuleUI> filtered;
        for (const auto& m : model_.modules) {
            if (m.module == module) {
                // The module header is rendered by the preview region; avoid duplicating it here.
                ModuleUI copy = m;
                copy.title.clear();
                filtered.push_back(std::move(copy));
            }
        }
        node->setModules(filtered, /*surfaceOnly=*/true, /*compactLayout=*/false);
        return node;
    };
    excitationKnobsNode_ = makeKnobs(FlowModule::kExcitation);
    stringKnobsNode_ = makeKnobs(FlowModule::kString);
    bodyKnobsNode_ = makeKnobs(FlowModule::kBody);
    roomKnobsNode_ = makeKnobs(FlowModule::kRoom);
#if SATORI_UI_DEBUG_ENABLED
    applyDebugOverlayState();
#endif

    keyboardNode_ = std::make_shared<KeyboardNode>();
    keyboardNode_->setColors(keyboardColors_);
    keyboardNode_->setConfig(model_.keyboardConfig, model_.keyCallback);

    // Four module cards aligned to the signal flow.
    excitationCardNode_ = std::make_shared<ModuleCardNode>(
        FlowModule::kExcitation, excitationPreviewNode_, excitationKnobsNode_);
    stringCardNode_ = std::make_shared<ModuleCardNode>(
        FlowModule::kString, stringPreviewNode_, stringKnobsNode_);
    bodyCardNode_ = std::make_shared<ModuleCardNode>(
        FlowModule::kBody, bodyPreviewNode_, bodyKnobsNode_);
    roomCardNode_ = std::make_shared<ModuleCardNode>(
        FlowModule::kRoom, roomPreviewNode_, roomKnobsNode_);

    auto mainRow = std::make_shared<UIHorizontalStack>(12.0f);
    mainRow->setItems({
        {excitationCardNode_, {UISizeMode::kPercent, 0.25f, 220.0f}},
        {stringCardNode_, {UISizeMode::kPercent, 0.25f, 220.0f}},
        {bodyCardNode_, {UISizeMode::kPercent, 0.25f, 220.0f}},
        {roomCardNode_, {UISizeMode::kPercent, 0.25f, 220.0f}},
    });
    mainRow_ = mainRow;

    auto rootStack = std::make_shared<UIStackPanel>(8.0f);
    std::vector<UIStackPanel::Item> items;
    items.push_back({headerBarNode_, {UISizeMode::kFixed, 56.0f, 56.0f}});
    items.push_back({mainRow, {UISizeMode::kAuto, 0.0f, 480.0f}});
    items.push_back({keyboardNode_, {UISizeMode::kFixed, 140.0f, 110.0f}});
    rootStack->setItems(std::move(items));

    rootLayout_ = rootStack;
    layoutDirty_ = true;
}

void Direct2DContext::ensureLayout() {
    if (!rootLayout_) {
        rebuildLayout();
    }
    if (!rootLayout_) {
        return;
    }
    if (!layoutDirty_) {
        return;
    }
    const auto bounds =
        D2D1::RectF(0.0f, 0.0f, static_cast<float>(width_),
                    static_cast<float>(height_));
    rootLayout_->arrange(bounds);
    layoutDirty_ = false;
}

void Direct2DContext::drawDebugOverlay() {
#if SATORI_UI_DEBUG_ENABLED
    if (debugOverlayMode_ != DebugOverlayMode::kBoxModel || !renderTarget_ ||
        !rootLayout_) {
        return;
    }
    if (!hoverDebugModel_) {
        return;
    }
    ScopedAntialiasMode scoped(renderTarget_.Get(),
                               D2D1_ANTIALIAS_MODE_ALIASED);
    debugBoxRenderer_.render(renderTarget_.Get(), *hoverDebugModel_, true);
#endif
}

void Direct2DContext::applyDebugOverlayState() {
#if SATORI_UI_DEBUG_ENABLED
    if (debugOverlayMode_ != DebugOverlayMode::kBoxModel) {
        clearDebugSelection();
        return;
    }
    if (hasPointerPosition_) {
        updateDebugSelection(lastPointerPosition_.x, lastPointerPosition_.y);
    }
#endif
}

bool Direct2DContext::updateDebugSelection(float x, float y) {
#if SATORI_UI_DEBUG_ENABLED
    if (debugOverlayMode_ != DebugOverlayMode::kBoxModel) {
        return clearDebugSelection();
    }
    if (!pointerCaptured_ && !pointerInside_) {
        return clearDebugSelection();
    }
    auto selection = pickDebugSelection(x, y);
    if (!selection.has_value()) {
        return clearDebugSelection();
    }
    if (hoverDebugModel_ && AreModelsEqual(*hoverDebugModel_, *selection)) {
        return false;
    }
    hoverDebugModel_ = std::move(selection);
    return true;
#else
    (void)x;
    (void)y;
    return false;
#endif
}

bool Direct2DContext::clearDebugSelection() {
#if SATORI_UI_DEBUG_ENABLED
    if (!hoverDebugModel_) {
        return false;
    }
    hoverDebugModel_.reset();
    return true;
#else
    return false;
#endif
}

std::optional<DebugBoxModel> Direct2DContext::pickDebugSelection(float x,
                                                                 float y) const {
#if SATORI_UI_DEBUG_ENABLED
    if (!rootLayout_) {
        return std::nullopt;
    }

    if (excitationKnobsNode_) {
        if (auto panel = excitationKnobsNode_->debugBoxForPoint(x, y)) {
            return panel;
        }
    }
    if (stringKnobsNode_) {
        if (auto panel = stringKnobsNode_->debugBoxForPoint(x, y)) {
            return panel;
        }
    }
    if (bodyKnobsNode_) {
        if (auto panel = bodyKnobsNode_->debugBoxForPoint(x, y)) {
            return panel;
        }
    }
    if (roomKnobsNode_) {
        if (auto panel = roomKnobsNode_->debugBoxForPoint(x, y)) {
            return panel;
        }
    }

    auto checkNode =
        [&](const std::shared_ptr<UILayoutNode>& node) -> std::optional<DebugBoxModel> {
        if (node && ContainsPoint(node->bounds(), x, y)) {
            return MakeLayoutBox(node->bounds());
        }
        return std::nullopt;
    };

    if (keyboardNode_) {
        if (auto keyboardSelection = checkNode(keyboardNode_)) {
            return keyboardSelection;
        }
    }
    if (excitationPreviewNode_) {
        if (auto sel = checkNode(excitationPreviewNode_)) {
            return sel;
        }
    }
    if (stringPreviewNode_) {
        if (auto sel = checkNode(stringPreviewNode_)) {
            return sel;
        }
    }
    if (bodyPreviewNode_) {
        if (auto sel = checkNode(bodyPreviewNode_)) {
            return sel;
        }
    }
    if (roomPreviewNode_) {
        if (auto sel = checkNode(roomPreviewNode_)) {
            return sel;
        }
    }
    if (auto headerSelection = checkNode(headerBarNode_)) {
        return headerSelection;
    }
    if (ContainsPoint(rootLayout_->bounds(), x, y)) {
        return MakeLayoutBox(rootLayout_->bounds());
    }
    return std::nullopt;
#else
    (void)x;
    (void)y;
    return std::nullopt;
#endif
}

RenderResources Direct2DContext::makeResources() {
    RenderResources resources;
    resources.target = renderTarget_.Get();
    resources.accentBrush = accentBrush_.Get();
    resources.excitationBrush = excitationBrush_.Get();
    resources.accentFillBrush = accentFillBrush_.Get();
    resources.textBrush = textBrush_.Get();
    resources.trackBrush = trackBrush_.Get();
    resources.fillBrush = fillBrush_.Get();
    resources.panelBrush = panelBrush_.Get();
    resources.cardBrush = cardBrush_.Get();
    resources.shadowBrush = shadowBrush_.Get();
    resources.gridBrush = gridBrush_.Get();
    resources.textFormat = textFormat_.Get();
    resources.skinId = skinConfig_.id;
    resources.skin = &skinResources_;
    return resources;
}

}  // namespace winui
