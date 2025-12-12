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
    const float padding = 8.0f;
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

    // Soft highlight overlay to make the active module feel "connected" to its knobs.
    if (highlighted_ && resources.accentBrush) {
        const float original = resources.accentBrush->GetOpacity();
        resources.accentBrush->SetOpacity(0.10f);
        resources.target->FillRectangle(bounds_, resources.accentBrush);
        resources.accentBrush->SetOpacity(original);
        resources.target->DrawRectangle(bounds_, resources.accentBrush, 1.8f);
    }

    const float padding = 10.0f;
    const auto inner =
        D2D1::RectF(bounds_.left + padding, bounds_.top + padding,
                    bounds_.right - padding, bounds_.bottom - padding);

    const float innerWidth = inner.right - inner.left;
    const float innerHeight = inner.bottom - inner.top;
    if (innerWidth <= 0.0f || innerHeight <= 0.0f) {
        return;
    }

    // Top visualization region (leave room for title).
    const float titleHeight = 28.0f;
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

                                const float originalOpacity = scopeBrush->GetOpacity();
                                scopeBrush->SetOpacity(highlighted_ ? 0.20f : 0.12f);
                                resources.target->FillGeometry(geometry.Get(), scopeBrush);
                                scopeBrush->SetOpacity(originalOpacity);
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
                // Three vertical bars: Decay / Brightness / Dispersion
                const float w = vizRect.right - vizRect.left;
                const float h = vizRect.bottom - vizRect.top;
                const float gap = 8.0f;
                const float barW = (w - 2.0f * gap) / 3.0f;
                auto drawBar = [&](int idx, float value01) {
                    value01 = std::clamp(value01, 0.0f, 1.0f);
                    const float x0 = vizRect.left + idx * (barW + gap);
                    const float y1 = vizRect.bottom;
                    const float y0 = y1 - h * (0.15f + 0.8f * value01);
                    resources.target->FillRectangle(
                        D2D1::RectF(x0, y0, x0 + barW, y1),
                        resources.accentBrush);
                };
                // Map decay [0.90,0.999] to 0..1 for visualization.
                const float decay01 =
                    (std::clamp(state_.decay, 0.90f, 0.999f) - 0.90f) / (0.999f - 0.90f);
                drawBar(0, decay01);
                drawBar(1, state_.brightness);
                drawBar(2, state_.dispersionAmount);
            }
            break;
        }
        case FlowModule::kBody: {
            DrawTitle(resources.target, resources.textBrush, resources.gridBrush,
                      resources.accentBrush, resources.textFormat, bounds_,
                      L"BODY", highlighted_);
            if (resources.accentBrush) {
                // Simple tone meter.
                const float w = vizRect.right - vizRect.left;
                const float h = vizRect.bottom - vizRect.top;
                const float v = std::clamp(state_.bodyTone, 0.0f, 1.0f);
                const float barH = h * (0.2f + 0.7f * v);
                const float x0 = vizRect.left + w * 0.25f;
                const float x1 = vizRect.right - w * 0.25f;
                resources.target->FillRectangle(
                    D2D1::RectF(x0, vizRect.bottom - barH, x1, vizRect.bottom),
                    resources.accentBrush);
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
    const float padding = 10.0f;
    const auto inner =
        D2D1::RectF(bounds_.left + padding, bounds_.top + padding,
                    bounds_.right - padding, bounds_.bottom - padding);
    const float innerWidth = inner.right - inner.left;
    const float innerHeight = inner.bottom - inner.top;
    if (innerWidth <= 0.0f || innerHeight <= 0.0f) {
        return D2D1::RectF(0, 0, 0, 0);
    }
    const float titleHeight = 28.0f;
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
