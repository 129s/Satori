#include "win/ui/nodes/ModulePreviewNode.h"

#include <algorithm>
#include <cmath>
#include <cwchar>

#include <d2d1helper.h>
#include <wrl/client.h>

#include "win/ui/nodes/DropdownSelectorNode.h"

namespace winui {

namespace {
bool ContainsPoint(const D2D1_RECT_F& rect, float x, float y) {
    return x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom;
}

}  // namespace

ModulePreviewNode::ModulePreviewNode(FlowModule module) : module_(module) {
    if (module_ == FlowModule::kExcitation) {
        waveformSelector_ = std::make_shared<DropdownSelectorNode>();
    }
}

void ModulePreviewNode::setDiagramState(const FlowDiagramState& state) {        
    state_ = state;
    if (module_ == FlowModule::kExcitation && waveformSelector_) {
        waveformSelector_->setSelectedIndex(state_.waveformType);
    }
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

void ModulePreviewNode::arrange(const D2D1_RECT_F& bounds) {
    UILayoutNode::arrange(bounds);
    excitationSelectorRect_ = D2D1::RectF(0, 0, 0, 0);
    excitationScopeRect_ = D2D1::RectF(0, 0, 0, 0);
    if (module_ == FlowModule::kExcitation) {
        const auto viz = computeVizRect();
        const float w = viz.right - viz.left;
        const float h = viz.bottom - viz.top;
        if (w > 0.0f && h > 0.0f) {
            constexpr float dropdownH = 28.0f;
            constexpr float gap = 6.0f;
            excitationSelectorRect_ =
                D2D1::RectF(viz.left, viz.top, viz.right,
                            std::min(viz.bottom, viz.top + dropdownH));
            excitationScopeRect_ = D2D1::RectF(
                viz.left, std::min(viz.bottom, excitationSelectorRect_.bottom + gap),
                viz.right, viz.bottom);
            if (waveformSelector_) {
                waveformSelector_->arrange(excitationSelectorRect_);
            }
        }
    }
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

    const auto vizRect = inner;

    switch (module_) {
        case FlowModule::kExcitation: {
            const auto scopeRect = excitationScopeRect_;
            // Transient scope (already normalized in FlowDiagramNode; do it here again for safety).
            const auto& samples = state_.excitationSamples;
            ID2D1SolidColorBrush* scopeBrush =
                resources.excitationBrush
                    ? resources.excitationBrush
                    : (resources.accentBrush ? resources.accentBrush : resources.gridBrush);
            if (scopeBrush && samples.size() >= 2) {
                const float w = scopeRect.right - scopeRect.left;
                const float h = scopeRect.bottom - scopeRect.top;
                if (w > 0.0f && h > 0.0f) {
                    float peak = 0.0f;
                    for (float s : samples) {
                        peak = std::max(peak, std::abs(s));
                    }
                    const float invPeak = peak > 1e-4f ? (1.0f / peak) : 1.0f;
                    const float midY = scopeRect.top + h * 0.5f;
                    const float scaleY = h * 0.45f;
                    const float step = w / static_cast<float>(samples.size() - 1);

                    std::vector<D2D1_POINT_2F> points;
                    points.reserve(samples.size());
                    for (std::size_t i = 0; i < samples.size(); ++i) {
                        const float x = scopeRect.left + step * static_cast<float>(i);
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
                                        D2D1::Point2F(0.0f, scopeRect.top));
                                    resources.accentFillBrush->SetEndPoint(
                                        D2D1::Point2F(0.0f, scopeRect.bottom));
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

            if (waveformSelector_) {
                waveformSelector_->draw(resources);
            }
            break;
          }
          case FlowModule::kString: {
              if (!resources.accentBrush) {
                  break;
              }

              const float w = vizRect.right - vizRect.left;
              const float h = vizRect.bottom - vizRect.top;
              if (w <= 12.0f || h <= 12.0f) {
                  break;
              }

              // Schematic only: DELAY (top), then DECAY -> LOWPASS -> ALLPASS (bottom).
              const float decay01 =
                  (std::clamp(state_.decay, 0.90f, 0.999f) - 0.90f) / (0.999f - 0.90f);

              ID2D1SolidColorBrush* outlineBrush =
                  resources.gridBrush ? resources.gridBrush : resources.accentBrush;
              ID2D1SolidColorBrush* labelBrush =
                  resources.textBrush ? resources.textBrush : outlineBrush;

              const float wireThickness = highlighted_ ? 1.9f : 1.4f;
              const float outlineThickness = highlighted_ ? 1.6f : 1.2f;
              constexpr float corner = 6.0f;

              auto drawMidArrow = [&](const D2D1_POINT_2F& from, const D2D1_POINT_2F& to,
                                      float opacity) {
                  if (!resources.accentBrush) {
                      return;
                  }
                  const float original = resources.accentBrush->GetOpacity();
                  resources.accentBrush->SetOpacity(opacity);
                  resources.target->DrawLine(from, to, resources.accentBrush, wireThickness);

                  const D2D1_POINT_2F mid{(from.x + to.x) * 0.5f, (from.y + to.y) * 0.5f};
                  const float angle = std::atan2(to.y - from.y, to.x - from.x);
                  const float len = 6.0f;
                  const float spread = 0.55f;
                  const float a1 = angle + 3.1415926f + spread;
                  const float a2 = angle + 3.1415926f - spread;
                  const D2D1_POINT_2F p1{mid.x + std::cos(a1) * len, mid.y + std::sin(a1) * len};
                  const D2D1_POINT_2F p2{mid.x + std::cos(a2) * len, mid.y + std::sin(a2) * len};
                  resources.target->DrawLine(mid, p1, resources.accentBrush, wireThickness);
                  resources.target->DrawLine(mid, p2, resources.accentBrush, wireThickness);

                  resources.accentBrush->SetOpacity(original);
              };

              auto drawLabel = [&](const std::wstring& text, const D2D1_RECT_F& r,
                                   float opacity) {
                  if (!resources.textFormat || !labelBrush) {
                      return;
                  }

                  const float originalOpacity = labelBrush->GetOpacity();
                  labelBrush->SetOpacity(opacity);

                  if (resources.dwriteFactory) {
                      const float w = std::max(1.0f, r.right - r.left);
                      const float h = std::max(1.0f, r.bottom - r.top);
                      Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
                      if (SUCCEEDED(resources.dwriteFactory->CreateTextLayout(
                              text.c_str(), static_cast<UINT32>(text.size()),
                              resources.textFormat, w, h, &layout)) &&
                          layout) {
                          (void)layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
                          (void)layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                          (void)layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                          resources.target->DrawTextLayout(D2D1::Point2F(r.left, r.top),
                                                           layout.Get(), labelBrush,
                                                           D2D1_DRAW_TEXT_OPTIONS_CLIP);
                          labelBrush->SetOpacity(originalOpacity);
                          return;
                      }
                  }

                  resources.target->DrawText(text.c_str(), static_cast<UINT32>(text.size()),
                                             resources.textFormat, r, labelBrush);
                  labelBrush->SetOpacity(originalOpacity);
              };

                const float padX = 6.0f;
                const float padY = 6.0f;
                const float gapX = std::clamp(w * 0.03f, 6.0f, 10.0f);
                const float gapY = std::clamp(h * 0.12f, 16.0f, 22.0f);
                const auto inner = D2D1::RectF(vizRect.left + padX, vizRect.top + padY,
                                              vizRect.right - padX, vizRect.bottom - padY);
                const float innerW = std::max(1.0f, inner.right - inner.left);
                const float innerH = std::max(1.0f, inner.bottom - inner.top);

                const float delayH = std::clamp(innerH * 0.28f, 20.0f, 30.0f);
                const D2D1_RECT_F delayRect =
                    D2D1::RectF(inner.left, inner.top, inner.right, inner.top + delayH);

                float rowY = inner.bottom - std::clamp(innerH * 0.26f, 18.0f, 26.0f);
                const float blockH = std::clamp(innerH * 0.26f, 18.0f, 26.0f);
                rowY = std::clamp(rowY, delayRect.bottom + gapY, inner.bottom - blockH);
                const float blockW =
                    std::max(1.0f, (innerW - 2.0f * gapX) / 3.0f);

              const D2D1_RECT_F decayRect =
                  D2D1::RectF(inner.left, rowY, inner.left + blockW, rowY + blockH);
              const D2D1_RECT_F lowpassRect =
                  D2D1::RectF(decayRect.right + gapX, rowY, decayRect.right + gapX + blockW,
                              rowY + blockH);
              const D2D1_RECT_F allpassRect =
                  D2D1::RectF(lowpassRect.right + gapX, rowY,
                              lowpassRect.right + gapX + blockW, rowY + blockH);

              auto centerBottom = [](const D2D1_RECT_F& r) {
                  return D2D1::Point2F((r.left + r.right) * 0.5f, r.bottom);
              };
              auto centerTop = [](const D2D1_RECT_F& r) {
                  return D2D1::Point2F((r.left + r.right) * 0.5f, r.top);
              };
              auto centerLeft = [](const D2D1_RECT_F& r) {
                  return D2D1::Point2F(r.left, (r.top + r.bottom) * 0.5f);
              };
              auto centerRight = [](const D2D1_RECT_F& r) {
                  return D2D1::Point2F(r.right, (r.top + r.bottom) * 0.5f);
              };

                // Wires: Delay -> Decay -> Lowpass -> Allpass -> (feedback) -> Delay.
                const float inset = 2.0f;
                auto insetPt = [&](D2D1_POINT_2F p, float dx, float dy) {
                    p.x += dx;
                    p.y += dy;
                    return p;
                };

                // Keep the first connector perfectly vertical (no diagonal slant):
                // align the source point on DELAY to DECAY's center x.
                const float decayCenterX = (decayRect.left + decayRect.right) * 0.5f;
                drawMidArrow(D2D1::Point2F(decayCenterX, delayRect.bottom + inset),
                             D2D1::Point2F(decayCenterX, decayRect.top - inset),
                             highlighted_ ? 0.95f : 0.80f);
                drawMidArrow(insetPt(centerRight(decayRect), inset, 0.0f),
                             insetPt(centerLeft(lowpassRect), -inset, 0.0f),
                             highlighted_ ? 0.95f : 0.80f);
                drawMidArrow(insetPt(centerRight(lowpassRect), inset, 0.0f),
                             insetPt(centerLeft(allpassRect), -inset, 0.0f),
                             highlighted_ ? 0.95f : 0.80f);

                // Feedback return (single clean vertical segment above ALLPASS).
                const D2D1_POINT_2F returnFrom =
                    insetPt(centerTop(allpassRect), 0.0f, -inset);
                const D2D1_POINT_2F returnTo =
                    D2D1::Point2F(returnFrom.x, delayRect.bottom + inset);
                drawMidArrow(returnFrom, returnTo, highlighted_ ? 0.70f : 0.55f);

              // Blocks.
              if (outlineBrush) {
                  const float originalOpacity = outlineBrush->GetOpacity();
                  outlineBrush->SetOpacity(highlighted_ ? 0.75f : 0.55f);

                  resources.target->DrawRoundedRectangle(
                      D2D1::RoundedRect(delayRect, corner, corner), outlineBrush,
                      outlineThickness);
                  resources.target->DrawRoundedRectangle(
                      D2D1::RoundedRect(decayRect, corner, corner), outlineBrush,
                      outlineThickness);
                  resources.target->DrawRoundedRectangle(
                      D2D1::RoundedRect(lowpassRect, corner, corner), outlineBrush,
                      outlineThickness);
                  resources.target->DrawRoundedRectangle(
                      D2D1::RoundedRect(allpassRect, corner, corner), outlineBrush,
                      outlineThickness);

                  outlineBrush->SetOpacity(originalOpacity);
              }

              drawLabel(L"DELAY", delayRect, highlighted_ ? 0.90f : 0.75f);
              drawLabel(L"DECAY", decayRect, highlighted_ ? 0.90f : 0.75f);
              drawLabel(L"LOWPASS", lowpassRect, highlighted_ ? 0.90f : 0.75f);
              drawLabel(L"ALLPASS", allpassRect, highlighted_ ? 0.90f : 0.75f);

                break;
            }
        case FlowModule::kBody: {
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

    if (module_ == FlowModule::kExcitation) {
        if (waveformSelector_ && waveformSelector_->onPointerDown(x, y)) {
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
    return inner;
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
