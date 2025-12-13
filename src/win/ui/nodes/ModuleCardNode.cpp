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
}  // namespace

ModuleCardNode::ModuleCardNode(FlowModule module,
                               std::shared_ptr<UILayoutNode> preview,
                               std::shared_ptr<UILayoutNode> controls)
    : module_(module), preview_(std::move(preview)), controls_(std::move(controls)) {}

void ModuleCardNode::setHighlighted(bool highlighted) {
    highlighted_ = highlighted;
}

float ModuleCardNode::preferredHeight(float width) const {
    const float innerWidth = std::max(0.0f, width - padding_ * 2.0f);
    float previewH = preview_ ? preview_->preferredHeight(innerWidth) : 0.0f;
    float controlsH = controls_ ? controls_->preferredHeight(innerWidth) : 0.0f;
    float total = previewH + controlsH;
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

    const float previewPref = preview_ ? preview_->preferredHeight(innerWidth) : 0.0f;
    const float controlsPref = controls_ ? controls_->preferredHeight(innerWidth) : 0.0f;
    float totalPref = previewPref + controlsPref;
    if (previewPref > 0.0f && controlsPref > 0.0f) {
        totalPref += spacing_;
    }

    float previewH = previewPref;
    float controlsH = controlsPref;
    if (totalPref > 1e-3f && totalPref > innerHeight) {
        const float available = std::max(0.0f, innerHeight);
        const float scale = available / totalPref;
        previewH *= scale;
        controlsH *= scale;
    }

    float y = inner_.top;
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

    const auto rounded = D2D1::RoundedRect(bounds, cornerRadius_, cornerRadius_);

    // Shadow (cheap: offset rounded rect fill).
    if (resources.shadowBrush) {
        const float shadowOffsetY = 4.0f;
        auto shadowRect = bounds;
        shadowRect.top += shadowOffsetY;
        shadowRect.bottom += shadowOffsetY;
        const auto shadowRounded =
            D2D1::RoundedRect(shadowRect, cornerRadius_, cornerRadius_);
        resources.target->FillRoundedRectangle(shadowRounded, resources.shadowBrush);
    }

    // Card base.
    if (resources.cardBrush) {
        resources.target->FillRoundedRectangle(rounded, resources.cardBrush);
    }

    // Subtle border for separation.
    if (resources.gridBrush) {
        const float original = resources.gridBrush->GetOpacity();
        resources.gridBrush->SetOpacity(std::min(1.0f, original * 1.6f));
        resources.target->DrawRoundedRectangle(rounded, resources.gridBrush, 1.0f);
        resources.gridBrush->SetOpacity(original);
    }

    // Highlight glow (Room is slightly stronger per spec).
    if (highlighted_ && resources.accentBrush) {
        const float original = resources.accentBrush->GetOpacity();
        const float glow =
            (module_ == FlowModule::kRoom) ? 0.12f : 0.08f;
        resources.accentBrush->SetOpacity(glow);
        resources.target->FillRoundedRectangle(rounded, resources.accentBrush);
        resources.accentBrush->SetOpacity(original);

        resources.target->DrawRoundedRectangle(rounded, resources.accentBrush, 1.8f);
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

