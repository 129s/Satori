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
#include "win/ui/nodes/FlowDiagramNode.h"
#include "win/ui/nodes/KnobPanelNode.h"
#include "win/ui/nodes/KeyboardNode.h"
#include "win/ui/nodes/TopBarNode.h"
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
    D2D1_COLOR_F grid{};
};

SkinBrushColors MakeBrushColors() {
    SkinBrushColors colors{};
    // 颜色方案借鉴 Serum UI 氛围，但固定为当前默认主题，不再提供独立皮肤切换。
    colors.accent = D2D1::ColorF(0.4f, 0.7f, 0.9f, 1.0f);
    colors.text = D2D1::ColorF(0.9f, 0.9f, 0.95f, 1.0f);
    colors.track = D2D1::ColorF(0.15f, 0.15f, 0.2f, 1.0f);
    colors.fill = D2D1::ColorF(0.25f, 0.55f, 0.75f, 1.0f);
    colors.panel = D2D1::ColorF(0.12f, 0.15f, 0.2f, 1.0f);
    colors.grid = D2D1::ColorF(0.35f, 0.4f, 0.45f, 1.0f);
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
    if (flowNode_) {
        flowNode_->setWaveformSamples(samples);
    }
}

void Direct2DContext::syncSliders() {
    if (knobPanelNode_) {
        knobPanelNode_->syncKnobs();
    }
}

bool Direct2DContext::onPointerDown(float x, float y) {
#if SATORI_UI_DEBUG_ENABLED
    pointerInside_ = true;
    hasPointerPosition_ = true;
    lastPointerPosition_ = D2D1::Point2F(x, y);
#endif
    bool handled = false;
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
    if (topBarNode_) {
        output << L"  topbar    " << formatRect(topBarNode_->bounds()) << L"\n";
    }
    if (flowNode_) {
        output << L"  flow      " << formatRect(flowNode_->bounds()) << L"\n";
    }
    if (knobPanelNode_) {
        output << L"  knobs     " << formatRect(knobPanelNode_->bounds())
               << L"\n";
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
    hr = renderTarget_->CreateSolidColorBrush(colors.grid, &gridBrush_);
    if (FAILED(hr)) {
        return false;
    }
    // 虚拟键盘配色复用 KeyboardSandbox 方案。
    hr = renderTarget_->CreateSolidColorBrush(
        D2D1::ColorF(0.94f, 0.95f, 0.98f, 1.0f), &keyboardWhiteFillBrush_);
    if (FAILED(hr)) {
        return false;
    }
    hr = renderTarget_->CreateSolidColorBrush(
        D2D1::ColorF(0.78f, 0.82f, 0.90f, 1.0f), &keyboardWhitePressedBrush_);
    if (FAILED(hr)) {
        return false;
    }
    hr = renderTarget_->CreateSolidColorBrush(
        D2D1::ColorF(0.10f, 0.12f, 0.16f, 1.0f), &keyboardWhiteTextBrush_);
    if (FAILED(hr)) {
        return false;
    }
    hr = renderTarget_->CreateSolidColorBrush(
        D2D1::ColorF(0.08f, 0.08f, 0.10f, 1.0f), &keyboardBlackFillBrush_);
    if (FAILED(hr)) {
        return false;
    }
    hr = renderTarget_->CreateSolidColorBrush(
        D2D1::ColorF(0.20f, 0.25f, 0.35f, 1.0f), &keyboardBlackPressedBrush_);
    if (FAILED(hr)) {
        return false;
    }
    hr = renderTarget_->CreateSolidColorBrush(
        D2D1::ColorF(0.98f, 0.98f, 0.99f, 1.0f), &keyboardBlackTextBrush_);
    if (FAILED(hr)) {
        return false;
    }
    hr = renderTarget_->CreateSolidColorBrush(
        D2D1::ColorF(0.15f, 0.17f, 0.22f, 1.0f), &keyboardBorderBrush_);
    if (FAILED(hr)) {
        return false;
    }
    hr = renderTarget_->CreateSolidColorBrush(
        D2D1::ColorF(0.85f, 0.46f, 0.18f, 1.0f), &keyboardHoverBrush_);
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
    textBrush_.Reset();
    trackBrush_.Reset();
    fillBrush_.Reset();
    panelBrush_.Reset();
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

    D2D1_COLOR_F clearColor = D2D1::ColorF(0.07f, 0.07f, 0.09f);
    renderTarget_->Clear(clearColor);

    if (panelBrush_) {
        const auto bgRect =
            D2D1::RectF(0.0f, 0.0f, static_cast<float>(width_),
                        static_cast<float>(height_));
        renderTarget_->FillRectangle(bgRect, panelBrush_.Get());
    }

    if (rootLayout_) {
        auto resources = makeResources();
        rootLayout_->draw(resources);
        if (knobPanelNode_) {
            if (auto knob = knobPanelNode_->activeKnob()) {
                knob->drawTooltip(resources.target, resources.trackBrush,
                                  resources.fillBrush, resources.accentBrush,
                                  resources.textBrush, resources.textFormat);
            }
        }
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
    auto topBar = std::make_shared<TopBarNode>();
    topBar->setTitle(L"Satori");
    topBar->setSampleRate(model_.sampleRate);
    topBar->setAudioOnline(model_.audioOnline);
    topBar->setStatusText(model_.status.primary);
    topBar->setSecondaryStatusText(model_.status.secondary);
    topBarNode_ = topBar;

    flowNode_ = std::make_shared<FlowDiagramNode>();
    flowNode_->setDiagramState(model_.diagram);
    flowNode_->setWaveformSamples(model_.waveformSamples);

    knobPanelNode_ = std::make_shared<KnobPanelNode>();
    knobPanelNode_->setDescriptors(model_.sliders);
#if SATORI_UI_DEBUG_ENABLED
    applyDebugOverlayState();
#endif

    keyboardNode_ = std::make_shared<KeyboardNode>();
    keyboardNode_->setColors(keyboardColors_);
    keyboardNode_->setConfig(model_.keyboardConfig, model_.keyCallback);

    auto mainStack = std::make_shared<UIStackPanel>(8.0f);
    mainStack->setItems({
        {flowNode_, {UISizeMode::kPercent, 0.55f, 260.0f}},
        {knobPanelNode_, {UISizeMode::kPercent, 0.45f, 200.0f}},
    });

    auto rootStack = std::make_shared<UIStackPanel>(8.0f);
    rootStack->setItems({
        {topBar, {UISizeMode::kAuto, 0.0f, 40.0f}},
        {mainStack, {UISizeMode::kAuto, 0.0f, 400.0f}},
        {keyboardNode_, {UISizeMode::kFixed, 140.0f, 110.0f}},
    });

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

    if (knobPanelNode_) {
        if (auto panel = knobPanelNode_->debugBoxForPoint(x, y)) {
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
    if (auto flowSelection = checkNode(flowNode_)) {
        return flowSelection;
    }
    if (auto topSelection = checkNode(topBarNode_)) {
        return topSelection;
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
    resources.textBrush = textBrush_.Get();
    resources.trackBrush = trackBrush_.Get();
    resources.fillBrush = fillBrush_.Get();
    resources.panelBrush = panelBrush_.Get();
    resources.gridBrush = gridBrush_.Get();
    resources.textFormat = textFormat_.Get();
    resources.skinId = skinConfig_.id;
    resources.skin = &skinResources_;
    return resources;
}

}  // namespace winui
