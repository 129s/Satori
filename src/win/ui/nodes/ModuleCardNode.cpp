#include "win/ui/nodes/ModuleCardNode.h"

#include <algorithm>

namespace winui {

namespace {
bool IsRectValid(const D2D1_RECT_F& r) {
    return r.right > r.left && r.bottom > r.top;
}

D2D1_RECT_F Inset(const D2D1_RECT_F& r, float inset) {
    return D2D1::RectF(r.left + inset, r.top + inset, r.right - inset,
                       r.bottom - inset);
}

bool ContainsPoint(const D2D1_RECT_F& rect, float x, float y) {
    return x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom;
}

struct TextAlignmentGuard {
    explicit TextAlignmentGuard(IDWriteTextFormat* format,
                                DWRITE_TEXT_ALIGNMENT textAlignment,
                                DWRITE_PARAGRAPH_ALIGNMENT paragraphAlignment)
        : format_(format),
          oldText_(format ? format->GetTextAlignment()
                          : DWRITE_TEXT_ALIGNMENT_LEADING),
          oldPara_(format ? format->GetParagraphAlignment()
                          : DWRITE_PARAGRAPH_ALIGNMENT_NEAR) {
        if (format_) {
            (void)format_->SetTextAlignment(textAlignment);
            (void)format_->SetParagraphAlignment(paragraphAlignment);
        }
    }
    ~TextAlignmentGuard() {
        if (format_) {
            (void)format_->SetTextAlignment(oldText_);
            (void)format_->SetParagraphAlignment(oldPara_);
        }
    }

    TextAlignmentGuard(const TextAlignmentGuard&) = delete;
    TextAlignmentGuard& operator=(const TextAlignmentGuard&) = delete;

private:
    IDWriteTextFormat* format_ = nullptr;
    DWRITE_TEXT_ALIGNMENT oldText_{};
    DWRITE_PARAGRAPH_ALIGNMENT oldPara_{};
};

}  // namespace

ModuleCardNode::ModuleCardNode(FlowModule module,
                               std::shared_ptr<UILayoutNode> preview,
                               std::shared_ptr<UILayoutNode> controls)
    : module_(module), preview_(std::move(preview)), controls_(std::move(controls)) {}

void ModuleCardNode::setHighlighted(bool highlighted) {
    highlighted_ = highlighted;
}

void ModuleCardNode::setTitleBar(std::wstring title, float fontSize) {
    title_ = std::move(title);
    const float h = std::max(18.0f, fontSize * 1.15f + 10.0f);
    titleBarHeight_ = std::clamp(h, 22.0f, 34.0f);
}

float ModuleCardNode::preferredHeight(float width) const {
    const float innerWidth = std::max(0.0f, width - padding_ * 2.0f);
    const float titleH = title_.empty() ? 0.0f : titleBarHeight_;
    float previewH = preview_ ? preview_->preferredHeight(innerWidth) : 0.0f;
    float controlsH = controls_ ? controls_->preferredHeight(innerWidth) : 0.0f;
    float total = titleH + previewH + controlsH;
    if (titleH > 0.0f && previewH > 0.0f) {
        total += titleSpacing_;
    }
    if (previewH > 0.0f && controlsH > 0.0f) {
        total += spacing_;
    }
    return total + padding_ * 2.0f;
}

void ModuleCardNode::arrange(const D2D1_RECT_F& bounds) {
    UILayoutNode::arrange(bounds);

    inner_ = Inset(bounds, padding_);
    if (!IsRectValid(inner_)) {
        return;
    }

    const float innerWidth = inner_.right - inner_.left;
    const float innerHeight = inner_.bottom - inner_.top;

    const float titlePref = title_.empty() ? 0.0f : titleBarHeight_;
    const float previewPref = preview_ ? preview_->preferredHeight(innerWidth) : 0.0f;
    const float controlsPref = controls_ ? controls_->preferredHeight(innerWidth) : 0.0f;
    float totalPref = titlePref + previewPref + controlsPref;
    if (titlePref > 0.0f && previewPref > 0.0f) {
        totalPref += titleSpacing_;
    }
    if (previewPref > 0.0f && controlsPref > 0.0f) {
        totalPref += spacing_;
    }

    float titleH = titlePref;
    float previewH = previewPref;
    float controlsH = controlsPref;
    if (totalPref > 1e-3f && totalPref > innerHeight) {
        const float available = std::max(0.0f, innerHeight);
        const float scale = available / totalPref;
        titleH *= scale;
        previewH *= scale;
        controlsH *= scale;
    }

    float y = inner_.top;
    titleRect_ = D2D1::RectF(0, 0, 0, 0);
    if (titleH > 0.0f) {
        titleRect_ = D2D1::RectF(inner_.left, y, inner_.right, y + titleH);
        y += titleH;
    }
    if (titleH > 0.0f && previewH > 0.0f) {
        y += titleSpacing_;
    }
    if (preview_ && previewH > 0.0f) {
        preview_->arrange(D2D1::RectF(inner_.left, y, inner_.right, y + previewH));
        y += previewH;
    }
    if (previewPref > 0.0f && controlsPref > 0.0f) {
        y += spacing_;
    }
    if (controls_ && controlsH > 0.0f) {
        controls_->arrange(D2D1::RectF(inner_.left, y, inner_.right, inner_.bottom));
    }
}

void ModuleCardNode::draw(const RenderResources& resources) {
    if (!resources.target) {
        return;
    }

    auto bounds = bounds_;
    if (!IsRectValid(bounds)) {
        return;
    }

    // Shadow (cheap: offset rect fill).
    if (resources.shadowBrush) {
        const float shadowOffsetY = 4.0f;
        auto shadowRect = bounds;
        shadowRect.top += shadowOffsetY;
        shadowRect.bottom += shadowOffsetY;
        resources.target->FillRectangle(shadowRect, resources.shadowBrush);
    }

    // Card base.
    if (resources.cardBrush) {
        resources.target->FillRectangle(bounds, resources.cardBrush);
    }

    // Highlight glow (Room is slightly stronger per spec).
    if (highlighted_ && resources.accentBrush) {
        const float original = resources.accentBrush->GetOpacity();
        const float glow = (module_ == FlowModule::kRoom) ? 0.10f : 0.07f;
        resources.accentBrush->SetOpacity(glow);
        resources.target->FillRectangle(bounds, resources.accentBrush);
        resources.accentBrush->SetOpacity(original);
    }

    if (!title_.empty() && resources.textFormat && IsRectValid(titleRect_)) {
        ID2D1SolidColorBrush* fill =
            resources.panelBrush ? resources.panelBrush : resources.trackBrush;
        ID2D1SolidColorBrush* text =
            resources.textBrush ? resources.textBrush : resources.gridBrush;
        if (fill && text) {
            resources.target->FillRectangle(titleRect_, fill);
            const TextAlignmentGuard align(resources.textFormat,
                                           DWRITE_TEXT_ALIGNMENT_CENTER,
                                           DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            resources.target->DrawText(title_.c_str(),
                                       static_cast<UINT32>(title_.size()),
                                       resources.textFormat, titleRect_, text);
        }
    }

    if (preview_) {
        preview_->draw(resources);
    }
    if (controls_) {
        controls_->draw(resources);
    }
}

bool ModuleCardNode::onPointerDown(float x, float y) {
    if (!ContainsPoint(bounds_, x, y)) {
        return false;
    }
    if (preview_ && preview_->onPointerDown(x, y)) {
        return true;
    }
    if (controls_ && controls_->onPointerDown(x, y)) {
        return true;
    }
    return false;
}

bool ModuleCardNode::onPointerMove(float x, float y) {
    bool handled = false;
    if (preview_ && preview_->onPointerMove(x, y)) {
        handled = true;
    }
    if (controls_ && controls_->onPointerMove(x, y)) {
        handled = true;
    }
    return handled;
}

void ModuleCardNode::onPointerUp() {
    if (preview_) {
        preview_->onPointerUp();
    }
    if (controls_) {
        controls_->onPointerUp();
    }
}

}  // namespace winui
