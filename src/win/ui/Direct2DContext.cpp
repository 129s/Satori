#include "win/ui/Direct2DContext.h"

#include <algorithm>
#include <cwchar>
#include <sstream>

#include <d2d1helper.h>

#include "win/ui/NunitoFont.h"
#include "win/ui/RenderResources.h"
#include "win/ui/UIModel.h"
#include "win/ui/nodes/FlowDiagramNode.h"
#include "win/ui/nodes/KeyboardNode.h"
#include "win/ui/nodes/KnobPanelNode.h"
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

}  // namespace

Direct2DContext::Direct2DContext() {
    skinConfig_ = MakeDefaultSkinConfig();
    skinResources_.config = skinConfig_;
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
    if (rootLayout_) {
        return rootLayout_->onPointerDown(x, y);
    }
    return false;
}

bool Direct2DContext::onPointerMove(float x, float y) {
    if (rootLayout_) {
        return rootLayout_->onPointerMove(x, y);
    }
    return false;
}

void Direct2DContext::onPointerUp() {
    if (rootLayout_) {
        rootLayout_->onPointerUp();
    }
}

void Direct2DContext::setLayoutDebugEnabled(bool enabled) {
    layoutDebugEnabled_ = enabled;
}

void Direct2DContext::toggleLayoutDebug() {
    layoutDebugEnabled_ = !layoutDebugEnabled_;
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
    return true;
}

void Direct2DContext::discardDeviceResources() {
    accentBrush_.Reset();
    textBrush_.Reset();
    trackBrush_.Reset();
    fillBrush_.Reset();
    panelBrush_.Reset();
    gridBrush_.Reset();
    renderTarget_.Reset();
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
        rootLayout_->draw(makeResources());
        drawLayoutDebug();
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
    topBarNode_ = topBar;

    flowNode_ = std::make_shared<FlowDiagramNode>();
    flowNode_->setDiagramState(model_.diagram);
    flowNode_->setWaveformSamples(model_.waveformSamples);

    knobPanelNode_ = std::make_shared<KnobPanelNode>();
    knobPanelNode_->setDescriptors(model_.sliders);

    keyboardNode_ = std::make_shared<KeyboardNode>();
    keyboardNode_->setKeys(model_.keys, model_.keyCallback);

    auto mainStack = std::make_shared<UIStackPanel>(8.0f);
    mainStack->setItems({
        {flowNode_, {UISizeMode::kPercent, 0.55f, 260.0f}},
        {knobPanelNode_, {UISizeMode::kPercent, 0.45f, 200.0f}},
    });

    auto rootStack = std::make_shared<UIStackPanel>(8.0f);
    rootStack->setItems({
        {topBar, {UISizeMode::kFixed, 40.0f, 32.0f}},
        {mainStack, {UISizeMode::kAuto, 0.0f, 400.0f}},
        {keyboardNode_, {UISizeMode::kFixed, 120.0f, 110.0f}},
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

void Direct2DContext::drawLayoutDebug() {
    if (!layoutDebugEnabled_ || !renderTarget_ || !gridBrush_) {
        return;
    }

    auto drawRect = [&](const D2D1_RECT_F& rect) {
        renderTarget_->DrawRectangle(rect, gridBrush_.Get(), 1.0f);
    };

    drawRect(rootLayout_->bounds());
    if (topBarNode_) {
        drawRect(topBarNode_->bounds());
    }
    if (flowNode_) {
        drawRect(flowNode_->bounds());
    }
    if (knobPanelNode_) {
        drawRect(knobPanelNode_->bounds());
    }
    if (keyboardNode_) {
        drawRect(keyboardNode_->bounds());
    }
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
