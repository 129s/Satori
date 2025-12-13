#include "win/ui/nodes/ModulePreviewNode.h"

#include <algorithm>
#include <cmath>

#include <d2d1helper.h>
#include <wrl/client.h>

namespace winui {

namespace {
bool ContainsPoint(const D2D1_RECT_F& rect, float x, float y) {
    return x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom;
}

void DrawTitle(ID2D1HwndRenderTarget* target,
               ID2D1SolidColorBrush* textBrush,
               ID2D1SolidColorBrush* gridBrush,
               ID2D1SolidColorBrush* accentBrush,
               IDWriteTextFormat* textFormat,
               const D2D1_RECT_F& rect,
               const wchar_t* title,
               bool highlighted) {
    if (!target || !textFormat || !title) {
        return;
    }
    const float padding = 4.0f;
    const auto titleRect =
        D2D1::RectF(rect.left + padding, rect.top + padding,
                    rect.right - padding, rect.top + padding + 20.0f);
    ID2D1SolidColorBrush* brush =
        highlighted && accentBrush ? accentBrush : (textBrush ? textBrush : gridBrush);
    if (!brush) {
        return;
    }
    target->DrawText(title, static_cast<UINT32>(wcslen(title)), textFormat, titleRect,
                     brush);
}

}  // namespace

ModulePreviewNode::ModulePreviewNode(FlowModule module) : module_(module) {}

void ModulePreviewNode::setDiagramState(const FlowDiagramState& state) {
    state_ = state;
}

void ModulePreviewNode::setWaveformSamples(const std::vector<float>& samples) {
    waveformView_.setSamples(samples);
}

void ModulePreviewNode::setHighlighted(bool highlighted) {
    highlighted_ = highlighted;
}

void ModulePreviewNode::setOnSelected(std::function<void(FlowModule)> callback) {
    onSelected_ = std::move(callback);
}

void ModulePreviewNode::setOnPickPositionChanged(
    std::function<void(float)> callback) {
    onPickPositionChanged_ = std::move(callback);
}

void ModulePreviewNode::setPickPositionRange(float min, float max) {
    if (max > min) {
        pickMin_ = min;
        pickMax_ = max;
    }
}

float ModulePreviewNode::preferredHeight(float) const {
    return 260.0f;
}

void ModulePreviewNode::draw(const RenderResources& resources) {
    if (!resources.target) {
        return;
    }

    auto* panelBrush = resources.panelBrush ? resources.panelBrush : resources.trackBrush;

    if (panelBrush) {
        resources.target->FillRectangle(bounds_, panelBrush);
    }

    const float padding = 6.0f;
    const auto inner =
        D2D1::RectF(bounds_.left + padding, bounds_.top + padding,
                    bounds_.right - padding, bounds_.bottom - padding);

    const float innerWidth = inner.right - inner.left;
    const float innerHeight = inner.bottom - inner.top;
    if (innerWidth <= 0.0f || innerHeight <= 0.0f) {
        return;
    }

    // Top visualization region (leave room for title).
    const float titleHeight = 26.0f;
    const float vizTop = inner.top + titleHeight;
    const float vizHeight = std::max(0.0f, innerHeight - titleHeight);
    const auto vizRect =
        D2D1::RectF(inner.left, vizTop, inner.right, vizTop + vizHeight);

    switch (module_) {
        case FlowModule::kExcitation: {
            DrawTitle(resources.target, resources.textBrush, resources.gridBrush,
                      resources.accentBrush, resources.textFormat, bounds_,
                      L"EXCITATION", highlighted_);

            // Transient scope (already normalized in FlowDiagramNode; do it here again for safety).
            const auto& samples = state_.excitationSamples;
            ID2D1SolidColorBrush* scopeBrush =
                resources.excitationBrush
                    ? resources.excitationBrush
                    : (resources.accentBrush ? resources.accentBrush : resources.gridBrush);
            if (scopeBrush && samples.size() >= 2) {
                const float w = vizRect.right - vizRect.left;
                const float h = vizRect.bottom - vizRect.top;
                if (w > 0.0f && h > 0.0f) {
                    float peak = 0.0f;
                    for (float s : samples) {
                        peak = std::max(peak, std::abs(s));
                    }
                    const float invPeak = peak > 1e-4f ? (1.0f / peak) : 1.0f;
                    const float midY = vizRect.top + h * 0.5f;
                    const float scaleY = h * 0.45f;
                    const float step = w / static_cast<float>(samples.size() - 1);

                    std::vector<D2D1_POINT_2F> points;
                    points.reserve(samples.size());
                    for (std::size_t i = 0; i < samples.size(); ++i) {
                        const float x = vizRect.left + step * static_cast<float>(i);
                        const float y =
                            midY -
                            std::clamp(samples[i] * invPeak, -1.0f, 1.0f) * scaleY;
                        points.push_back(D2D1::Point2F(x, y));
                    }

                    // Filled transient area (low opacity) to reduce "noisy line" feeling.
                    Microsoft::WRL::ComPtr<ID2D1Factory> factory;
                    resources.target->GetFactory(&factory);
                    if (factory && points.size() >= 2) {
                        Microsoft::WRL::ComPtr<ID2D1PathGeometry> geometry;
                        if (SUCCEEDED(factory->CreatePathGeometry(&geometry)) && geometry) {
                            Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
                            if (SUCCEEDED(geometry->Open(&sink)) && sink) {
                                sink->SetFillMode(D2D1_FILL_MODE_ALTERNATE);
                                sink->BeginFigure(D2D1::Point2F(points.front().x, midY),
                                                  D2D1_FIGURE_BEGIN_FILLED);
                                for (const auto& p : points) {
                                    sink->AddLine(p);
                                }
                                sink->AddLine(D2D1::Point2F(points.back().x, midY));
                                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                                (void)sink->Close();

                                // Gradient fill under the curve (adds depth without looking like a debug scope).
                                if (resources.accentFillBrush) {
                                    const float originalOpacity =
                                        resources.accentFillBrush->GetOpacity();
                                    resources.accentFillBrush->SetStartPoint(
                                        D2D1::Point2F(0.0f, vizRect.top));
                                    resources.accentFillBrush->SetEndPoint(
                                        D2D1::Point2F(0.0f, vizRect.bottom));
                                    resources.accentFillBrush->SetOpacity(
                                        highlighted_ ? 1.0f : 0.75f);
                                    resources.target->FillGeometry(geometry.Get(),
                                                                   resources.accentFillBrush);
                                    resources.accentFillBrush->SetOpacity(originalOpacity);
                                } else {
                                    const float originalOpacity = scopeBrush->GetOpacity();
                                    scopeBrush->SetOpacity(highlighted_ ? 0.20f : 0.12f);
                                    resources.target->FillGeometry(geometry.Get(), scopeBrush);
                                    scopeBrush->SetOpacity(originalOpacity);
                                }
                            }
                        }
                    }

                    // Outline.
                    const float thickness = highlighted_ ? 1.9f : 1.5f;
                    const float original = scopeBrush->GetOpacity();
                    scopeBrush->SetOpacity(highlighted_ ? 1.0f : 0.85f);
                    for (std::size_t i = 1; i < points.size(); ++i) {
                        resources.target->DrawLine(points[i - 1], points[i],
                                                   scopeBrush, thickness);
                    }
                    scopeBrush->SetOpacity(original);
                }
            }

            // Position slider: a "string" track + pick handle.
            const float y = vizRect.bottom - 18.0f;
            ID2D1SolidColorBrush* trackBrush =
                resources.gridBrush ? resources.gridBrush : resources.trackBrush;
            if (trackBrush) {
                const float original = trackBrush->GetOpacity();
                trackBrush->SetOpacity(0.55f);
                resources.target->DrawLine(D2D1::Point2F(vizRect.left, y),
                                           D2D1::Point2F(vizRect.right, y),
                                           trackBrush,
                                           highlighted_ ? 2.0f : 1.6f);
                trackBrush->SetOpacity(original);
            }
            if (resources.accentBrush) {
                const float range = std::max(1e-4f, pickMax_ - pickMin_);
                const float pos01 =
                    std::clamp((state_.pickPosition - pickMin_) / range, 0.0f, 1.0f);
                const float x =
                    vizRect.left + (vizRect.right - vizRect.left) * pos01;
                const float handleR = highlighted_ ? 6.0f : 5.0f;
                resources.target->FillEllipse(
                    D2D1::Ellipse(D2D1::Point2F(x, y), handleR, handleR),
                    resources.accentBrush);
                resources.target->DrawLine(D2D1::Point2F(x, y - 7.0f),
                                           D2D1::Point2F(x, y + 7.0f),
                                           resources.accentBrush,
                                           highlighted_ ? 2.4f : 2.0f);
            }
            break;
        }
        case FlowModule::kString: {
            DrawTitle(resources.target, resources.textBrush, resources.gridBrush,
                      resources.accentBrush, resources.textFormat, bounds_,
                      L"STRING", highlighted_);
            if (resources.accentBrush) {
                // Thin-line spectrum style (instrument-like, not chunky debug bars).
                const float w = vizRect.right - vizRect.left;
                const float h = vizRect.bottom - vizRect.top;
                if (w > 0.0f && h > 0.0f) {
                    const int lineCount = 36;
                    const float step = w / static_cast<float>(std::max(1, lineCount - 1));

                    const float decay01 =
                        (std::clamp(state_.decay, 0.90f, 0.999f) - 0.90f) / (0.999f - 0.90f);
                    const float brightness = std::clamp(state_.brightness, 0.0f, 1.0f);
                    const float dispersion = std::clamp(state_.dispersionAmount, 0.0f, 1.0f);

                    const float originalOpacity = resources.accentBrush->GetOpacity();
                    resources.accentBrush->SetOpacity(highlighted_ ? 0.90f : 0.70f);

                    for (int i = 0; i < lineCount; ++i) {
                        const float t =
                            static_cast<float>(i) / static_cast<float>(std::max(1, lineCount - 1));
                        const float harmonic = static_cast<float>(i) + 1.0f;

                        // Simple, deterministic "harmonic" envelope controlled by key string params.
                        const float rolloff =
                            0.06f + (1.0f - brightness) * 0.08f;
                        float amp = std::exp(-harmonic * rolloff);
                        amp *= 0.45f + 0.55f * decay01;
                        amp *= 0.35f + 0.65f * brightness;

                        // Dispersion introduces mild comb-like ripples.
                        const float ripple =
                            1.0f - 0.25f * dispersion +
                            0.25f * std::sin((t * 18.0f + dispersion * 2.0f) * 6.2831853f) * dispersion;
                        amp *= std::clamp(ripple, 0.2f, 1.2f);
                        amp = std::clamp(amp, 0.0f, 1.0f);

                        const float lineH = h * (0.10f + 0.85f * amp);
                        const float x = vizRect.left + step * static_cast<float>(i);
                        resources.target->DrawLine(D2D1::Point2F(x, vizRect.bottom),
                                                   D2D1::Point2F(x, vizRect.bottom - lineH),
                                                   resources.accentBrush, 1.0f);
                    }

                    resources.accentBrush->SetOpacity(originalOpacity);
                }
            }
            break;
        }
        case FlowModule::kBody: {
            DrawTitle(resources.target, resources.textBrush, resources.gridBrush,
                      resources.accentBrush, resources.textFormat, bounds_,
                      L"BODY", highlighted_);
            if (resources.accentBrush) {
                // Bell-curve-like response curve (Tone shifts, Size changes width).
                const float w = vizRect.right - vizRect.left;
                const float h = vizRect.bottom - vizRect.top;
                if (w > 0.0f && h > 0.0f) {
                    const float tone = std::clamp(state_.bodyTone, 0.0f, 1.0f);
                    const float size = std::clamp(state_.bodySize, 0.0f, 1.0f);

                    const float centerX = vizRect.left + w * (0.20f + 0.60f * tone);
                    const float sigma = w * (0.06f + 0.22f * size);
                    const float invSigma = sigma > 1e-4f ? (1.0f / sigma) : 1.0f;

                    constexpr int kPointCount = 80;
                    const float step = w / static_cast<float>(kPointCount - 1);

                    std::vector<D2D1_POINT_2F> points;
                    points.reserve(kPointCount);
                    for (int i = 0; i < kPointCount; ++i) {
                        const float x = vizRect.left + step * static_cast<float>(i);
                        const float dx = (x - centerX) * invSigma;
                        const float y01 = std::exp(-0.5f * dx * dx);  // 0..1
                        const float y = vizRect.bottom - h * (0.10f + 0.82f * y01);
                        points.push_back(D2D1::Point2F(x, y));
                    }

                    // Filled under-curve gradient (adds depth).
                    if (resources.accentFillBrush) {
                        Microsoft::WRL::ComPtr<ID2D1Factory> factory;
                        resources.target->GetFactory(&factory);
                        if (factory) {
                            Microsoft::WRL::ComPtr<ID2D1PathGeometry> geometry;
                            if (SUCCEEDED(factory->CreatePathGeometry(&geometry)) && geometry) {
                                Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
                                if (SUCCEEDED(geometry->Open(&sink)) && sink) {
                                    sink->SetFillMode(D2D1_FILL_MODE_ALTERNATE);
                                    sink->BeginFigure(D2D1::Point2F(points.front().x, vizRect.bottom),
                                                      D2D1_FIGURE_BEGIN_FILLED);
                                    for (const auto& p : points) {
                                        sink->AddLine(p);
                                    }
                                    sink->AddLine(D2D1::Point2F(points.back().x, vizRect.bottom));
                                    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                                    (void)sink->Close();

                                    const float originalOpacity =
                                        resources.accentFillBrush->GetOpacity();
                                    resources.accentFillBrush->SetStartPoint(
                                        D2D1::Point2F(0.0f, vizRect.top));
                                    resources.accentFillBrush->SetEndPoint(
                                        D2D1::Point2F(0.0f, vizRect.bottom));
                                    resources.accentFillBrush->SetOpacity(highlighted_ ? 1.0f : 0.75f);
                                    resources.target->FillGeometry(geometry.Get(),
                                                                   resources.accentFillBrush);
                                    resources.accentFillBrush->SetOpacity(originalOpacity);
                                }
                            }
                        }
                    }

                    // Curve stroke.
                    const float originalOpacity = resources.accentBrush->GetOpacity();
                    resources.accentBrush->SetOpacity(highlighted_ ? 1.0f : 0.90f);
                    const float thickness = highlighted_ ? 1.8f : 1.5f;
                    for (std::size_t i = 1; i < points.size(); ++i) {
                        resources.target->DrawLine(points[i - 1], points[i],
                                                   resources.accentBrush, thickness);
                    }
                    resources.accentBrush->SetOpacity(originalOpacity);
                }
            }
            break;
        }
        case FlowModule::kRoom: {
            DrawTitle(resources.target, resources.textBrush, resources.gridBrush,
                      resources.accentBrush, resources.textFormat, bounds_,
                      L"ROOM / FX", highlighted_);
            // Reuse waveform view in-room, similar to old FlowDiagramNode.
            waveformView_.setBounds(vizRect);
            if (resources.panelBrush && resources.gridBrush &&
                (resources.accentBrush || resources.excitationBrush)) {
                waveformView_.draw(resources.target,
                                   resources.panelBrush,
                                   resources.gridBrush,
                                   resources.accentBrush ? resources.accentBrush
                                                         : resources.gridBrush);
            } else if (resources.gridBrush && resources.accentBrush) {
                waveformView_.draw(resources.target,
                                   resources.gridBrush,
                                   resources.gridBrush,
                                   resources.accentBrush);
            }
            if (resources.accentBrush) {
                const float room = std::clamp(state_.roomAmount, 0.0f, 1.0f);
                const float barW = highlighted_ ? 8.0f : 6.0f;
                const float barH = (vizRect.bottom - vizRect.top) * (0.2f + 0.6f * room);
                const float x = vizRect.right - barW - 2.0f;
                const float y = vizRect.bottom - barH;
                resources.target->FillRectangle(D2D1::RectF(x, y, x + barW, vizRect.bottom),
                                                resources.accentBrush);
            }
            break;
        }
        case FlowModule::kNone:
        default:
            break;
    }
}

bool ModulePreviewNode::onPointerDown(float x, float y) {
    if (!ContainsPoint(bounds_, x, y)) {
        return false;
    }

    if (module_ == FlowModule::kExcitation && onPickPositionChanged_) {
        const auto track = computePickTrackRect();
        if (ContainsPoint(track, x, y)) {
            draggingPickPosition_ = true;
            onPickPositionChanged_(pickPositionFromX(x));
            return true;
        }
    }

    if (onSelected_) {
        onSelected_(module_);
        return true;
    }
    return false;
}

D2D1_RECT_F ModulePreviewNode::computeVizRect() const {
    const float padding = 6.0f;
    const auto inner =
        D2D1::RectF(bounds_.left + padding, bounds_.top + padding,
                    bounds_.right - padding, bounds_.bottom - padding);
    const float innerWidth = inner.right - inner.left;
    const float innerHeight = inner.bottom - inner.top;
    if (innerWidth <= 0.0f || innerHeight <= 0.0f) {
        return D2D1::RectF(0, 0, 0, 0);
    }
    const float titleHeight = 26.0f;
    const float vizTop = inner.top + titleHeight;
    const float vizHeight = std::max(0.0f, innerHeight - titleHeight);
    return D2D1::RectF(inner.left, vizTop, inner.right, vizTop + vizHeight);
}

D2D1_RECT_F ModulePreviewNode::computePickTrackRect() const {
    const auto viz = computeVizRect();
    const float w = viz.right - viz.left;
    const float h = viz.bottom - viz.top;
    if (w <= 0.0f || h <= 0.0f) {
        return D2D1::RectF(0, 0, 0, 0);
    }
    const float y = viz.bottom - 18.0f;
    return D2D1::RectF(viz.left, y - 12.0f, viz.right, y + 12.0f);
}

float ModulePreviewNode::pickPositionFromX(float x) const {
    const auto viz = computeVizRect();
    const float w = viz.right - viz.left;
    if (w <= 1e-4f) {
        return pickMin_;
    }
    float t = (x - viz.left) / w;
    t = std::clamp(t, 0.0f, 1.0f);
    return pickMin_ + t * (pickMax_ - pickMin_);
}

bool ModulePreviewNode::onPointerMove(float x, float y) {
    (void)y;
    if (module_ == FlowModule::kExcitation && draggingPickPosition_ &&
        onPickPositionChanged_) {
        onPickPositionChanged_(pickPositionFromX(x));
        return true;
    }
    return false;
}

void ModulePreviewNode::onPointerUp() {
    draggingPickPosition_ = false;
}

}  // namespace winui
