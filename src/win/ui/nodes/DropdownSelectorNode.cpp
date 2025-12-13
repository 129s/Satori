#include "win/ui/nodes/DropdownSelectorNode.h"

#include <algorithm>

#include <d2d1helper.h>

namespace winui {

namespace {
float RectWidth(const D2D1_RECT_F& r) { return r.right - r.left; }
float RectHeight(const D2D1_RECT_F& r) { return r.bottom - r.top; }
}  // namespace

void DropdownSelectorNode::setItems(std::vector<std::wstring> items) {
    items_ = std::move(items);
    selectedIndex_ = std::clamp(selectedIndex_, 0, std::max(0, static_cast<int>(items_.size()) - 1));
    pageIndex_ = 0;
    hoverIndex_.reset();
}

void DropdownSelectorNode::setSelectedIndex(int index) {
    if (items_.empty()) {
        selectedIndex_ = 0;
        return;
    }
    index = std::clamp(index, 0, static_cast<int>(items_.size()) - 1);
    if (index == selectedIndex_) {
        return;
    }
    selectedIndex_ = index;
    notifyChanged();
}

std::wstring DropdownSelectorNode::selectedLabel() const {
    if (items_.empty()) {
        return L"(none)";
    }
    const int idx = std::clamp(selectedIndex_, 0, static_cast<int>(items_.size()) - 1);
    return items_[static_cast<std::size_t>(idx)];
}

void DropdownSelectorNode::setOnChanged(std::function<void(int)> onChanged) {
    onChanged_ = std::move(onChanged);
}

void DropdownSelectorNode::setPageSize(int pageSize) {
    pageSize_ = std::max(1, pageSize);
    pageIndex_ = std::min(pageIndex_, std::max(0, pageCount() - 1));
}

void DropdownSelectorNode::close() {
    open_ = false;
    hoverIndex_.reset();
}

void DropdownSelectorNode::arrange(const D2D1_RECT_F& bounds) {
    UILayoutNode::arrange(bounds);
}

bool DropdownSelectorNode::hit(const D2D1_RECT_F& r, float x, float y) const {
    return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
}

void DropdownSelectorNode::notifyChanged() {
    if (onChanged_) {
        onChanged_(selectedIndex_);
    }
}

void DropdownSelectorNode::selectWrapped(int newIndex) {
    const int count = static_cast<int>(items_.size());
    if (count <= 0) {
        selectedIndex_ = 0;
        return;
    }
    newIndex %= count;
    if (newIndex < 0) newIndex += count;
    if (newIndex == selectedIndex_) {
        return;
    }
    selectedIndex_ = newIndex;
    notifyChanged();
}

int DropdownSelectorNode::pageCount() const {
    const int count = static_cast<int>(items_.size());
    if (count <= 0) {
        return 0;
    }
    return (count + pageSize_ - 1) / pageSize_;
}

void DropdownSelectorNode::openAtSelection() {
    open_ = true;
    hoverIndex_.reset();
    if (pageSize_ > 0) {
        pageIndex_ = selectedIndex_ / pageSize_;
        pageIndex_ = std::clamp(pageIndex_, 0, std::max(0, pageCount() - 1));
    } else {
        pageIndex_ = 0;
    }
}

D2D1_RECT_F DropdownSelectorNode::overlayRect() const {
    const float gap = 4.0f;
    const float itemH = 22.0f;
    const float pageH = 22.0f;
    const float w = RectWidth(bounds_);
    const float h = itemH * static_cast<float>(pageSize_) + pageH;
    return D2D1::RectF(bounds_.left, bounds_.bottom + gap, bounds_.left + w,
                       bounds_.bottom + gap + h);
}

D2D1_RECT_F DropdownSelectorNode::itemRect(int localRow) const {
    const float itemH = 22.0f;
    const auto r = overlayRect();
    const float top = r.top + itemH * static_cast<float>(localRow);
    return D2D1::RectF(r.left, top, r.right, top + itemH);
}

D2D1_RECT_F DropdownSelectorNode::prevPageRect() const {
    const auto r = overlayRect();
    const float pageH = 22.0f;
    const float mid = (r.left + r.right) * 0.5f;
    return D2D1::RectF(r.left, r.bottom - pageH, mid, r.bottom);
}

D2D1_RECT_F DropdownSelectorNode::nextPageRect() const {
    const auto r = overlayRect();
    const float pageH = 22.0f;
    const float mid = (r.left + r.right) * 0.5f;
    return D2D1::RectF(mid, r.bottom - pageH, r.right, r.bottom);
}

void DropdownSelectorNode::draw(const RenderResources& resources) {
    if (!resources.target || !resources.textFormat) {
        return;
    }
    auto* bg = resources.panelBrush ? resources.panelBrush : resources.trackBrush;
    auto* border = resources.gridBrush ? resources.gridBrush : resources.trackBrush;
    auto* text = resources.textBrush ? resources.textBrush : resources.gridBrush;
    if (!bg || !border || !text) {
        return;
    }

    // Base box.
    const auto rounded = D2D1::RoundedRect(bounds_, 4.0f, 4.0f);
    resources.target->FillRoundedRectangle(rounded, bg);
    resources.target->DrawRoundedRectangle(rounded, border, 1.0f);

    const auto leftArrow = D2D1::RectF(bounds_.left, bounds_.top, bounds_.left + arrowWidth_, bounds_.bottom);
    const auto rightArrow = D2D1::RectF(bounds_.right - arrowWidth_, bounds_.top, bounds_.right, bounds_.bottom);
    const auto labelRect = D2D1::RectF(leftArrow.right, bounds_.top, rightArrow.left, bounds_.bottom);

    // Arrow separators.
    resources.target->DrawLine(D2D1::Point2F(leftArrow.right, bounds_.top + 4.0f),
                               D2D1::Point2F(leftArrow.right, bounds_.bottom - 4.0f),
                               border, 1.0f);
    resources.target->DrawLine(D2D1::Point2F(rightArrow.left, bounds_.top + 4.0f),
                               D2D1::Point2F(rightArrow.left, bounds_.bottom - 4.0f),
                               border, 1.0f);

    // Arrow glyphs.
    auto drawArrow = [&](const D2D1_RECT_F& r, bool left) {
        const float cx = (r.left + r.right) * 0.5f;
        const float cy = (r.top + r.bottom) * 0.5f;
        const float s = 5.0f;
        if (left) {
            resources.target->DrawLine(D2D1::Point2F(cx + s, cy - s),
                                       D2D1::Point2F(cx - s, cy),
                                       text, 1.6f);
            resources.target->DrawLine(D2D1::Point2F(cx - s, cy),
                                       D2D1::Point2F(cx + s, cy + s),
                                       text, 1.6f);
        } else {
            resources.target->DrawLine(D2D1::Point2F(cx - s, cy - s),
                                       D2D1::Point2F(cx + s, cy),
                                       text, 1.6f);
            resources.target->DrawLine(D2D1::Point2F(cx + s, cy),
                                       D2D1::Point2F(cx - s, cy + s),
                                       text, 1.6f);
        }
    };
    drawArrow(leftArrow, true);
    drawArrow(rightArrow, false);

    // Label.
    const std::wstring label = selectedLabel();
    const auto textRect = D2D1::RectF(labelRect.left + padding_, labelRect.top + 3.0f,
                                      labelRect.right - padding_, labelRect.bottom - 3.0f);
    resources.target->DrawText(label.c_str(), static_cast<UINT32>(label.size()),
                               resources.textFormat, textRect, text);
}

void DropdownSelectorNode::drawOverlay(const RenderResources& resources) {
    if (!open_) {
        return;
    }
    if (!resources.target || !resources.textFormat) {
        return;
    }
    auto* bg = resources.cardBrush ? resources.cardBrush : resources.panelBrush;
    auto* border = resources.gridBrush ? resources.gridBrush : resources.trackBrush;
    auto* text = resources.textBrush ? resources.textBrush : resources.gridBrush;
    auto* accent = resources.accentBrush ? resources.accentBrush : text;
    if (!bg || !border || !text) {
        return;
    }

    const auto r = overlayRect();
    const auto rounded = D2D1::RoundedRect(r, 4.0f, 4.0f);
    resources.target->FillRoundedRectangle(rounded, bg);
    resources.target->DrawRoundedRectangle(rounded, border, 1.0f);

    const int count = static_cast<int>(items_.size());
    const int pages = pageCount();
    pageIndex_ = std::clamp(pageIndex_, 0, std::max(0, pages - 1));
    const int start = pageIndex_ * pageSize_;
    const int end = std::min(count, start + pageSize_);

    for (int row = 0; row < pageSize_; ++row) {
        const int idx = start + row;
        const auto rowRect = itemRect(row);
        if (idx >= end) {
            break;
        }
        const bool hovered = hoverIndex_ && *hoverIndex_ == idx;
        const bool selected = idx == selectedIndex_;

        if (hovered || selected) {
            const float original = accent->GetOpacity();
            accent->SetOpacity(selected ? 0.18f : 0.10f);
            resources.target->FillRectangle(rowRect, accent);
            accent->SetOpacity(original);
        }

        const auto tr = D2D1::RectF(rowRect.left + padding_, rowRect.top + 2.0f,
                                    rowRect.right - padding_, rowRect.bottom - 2.0f);
        const auto& name = items_[static_cast<std::size_t>(idx)];
        resources.target->DrawText(name.c_str(), static_cast<UINT32>(name.size()),
                                   resources.textFormat, tr,
                                   selected ? accent : text);
    }

    // Page controls (paginated list; no scrolling).
    const auto prev = prevPageRect();
    const auto next = nextPageRect();
    resources.target->DrawLine(D2D1::Point2F(prev.right, prev.top),
                               D2D1::Point2F(prev.right, prev.bottom), border, 1.0f);

    const bool hasPrev = pageIndex_ > 0;
    const bool hasNext = pages > 0 && pageIndex_ < pages - 1;

    const wchar_t* prevText = hasPrev ? L"Prev" : L"Prev";
    const wchar_t* nextText = hasNext ? L"Next" : L"Next";

    auto drawButton = [&](const D2D1_RECT_F& br, const wchar_t* label, bool enabled) {
        ID2D1SolidColorBrush* b = enabled ? text : border;
        const auto tr = D2D1::RectF(br.left + padding_, br.top + 2.0f, br.right - padding_, br.bottom - 2.0f);
        resources.target->DrawText(label, static_cast<UINT32>(wcslen(label)),
                                   resources.textFormat, tr, b);
    };
    drawButton(prev, prevText, hasPrev);
    drawButton(next, nextText, hasNext);
}

bool DropdownSelectorNode::onPointerDown(float x, float y) {
    if (!hit(bounds_, x, y)) {
        return false;
    }
    if (items_.empty()) {
        return true;
    }
    const auto leftArrow = D2D1::RectF(bounds_.left, bounds_.top, bounds_.left + arrowWidth_, bounds_.bottom);
    const auto rightArrow = D2D1::RectF(bounds_.right - arrowWidth_, bounds_.top, bounds_.right, bounds_.bottom);
    const auto labelRect = D2D1::RectF(leftArrow.right, bounds_.top, rightArrow.left, bounds_.bottom);

    if (hit(leftArrow, x, y)) {
        selectWrapped(selectedIndex_ - 1);
        return true;
    }
    if (hit(rightArrow, x, y)) {
        selectWrapped(selectedIndex_ + 1);
        return true;
    }
    if (hit(labelRect, x, y)) {
        if (open_) {
            close();
        } else {
            openAtSelection();
        }
        return true;
    }
    return true;
}

bool DropdownSelectorNode::onPointerMove(float, float) { return false; }

void DropdownSelectorNode::onPointerUp() {}

bool DropdownSelectorNode::onOverlayPointerDown(float x, float y) {
    if (!open_) {
        return false;
    }
    const auto r = overlayRect();
    if (!hit(r, x, y) && !hit(bounds_, x, y)) {
        // Click outside closes and consumes (so underlying controls don't also react).
        close();
        return true;
    }
    if (!hit(r, x, y)) {
        // Base area click handled by normal onPointerDown.
        return false;
    }

    const int pages = pageCount();
    const auto prev = prevPageRect();
    const auto next = nextPageRect();
    if (hit(prev, x, y)) {
        if (pageIndex_ > 0) {
            --pageIndex_;
            hoverIndex_.reset();
        }
        return true;
    }
    if (hit(next, x, y)) {
        if (pages > 0 && pageIndex_ < pages - 1) {
            ++pageIndex_;
            hoverIndex_.reset();
        }
        return true;
    }

    const int start = pageIndex_ * pageSize_;
    const int count = static_cast<int>(items_.size());
    const int end = std::min(count, start + pageSize_);
    for (int row = 0; row < pageSize_; ++row) {
        const int idx = start + row;
        if (idx >= end) {
            break;
        }
        if (hit(itemRect(row), x, y)) {
            selectedIndex_ = idx;
            notifyChanged();
            close();
            return true;
        }
    }
    return true;
}

bool DropdownSelectorNode::onOverlayPointerMove(float x, float y) {
    if (!open_) {
        return false;
    }
    const auto r = overlayRect();
    if (!hit(r, x, y)) {
        if (hoverIndex_) {
            hoverIndex_.reset();
            return true;
        }
        return false;
    }
    const int start = pageIndex_ * pageSize_;
    const int count = static_cast<int>(items_.size());
    const int end = std::min(count, start + pageSize_);

    std::optional<int> newHover;
    for (int row = 0; row < pageSize_; ++row) {
        const int idx = start + row;
        if (idx >= end) {
            break;
        }
        if (hit(itemRect(row), x, y)) {
            newHover = idx;
            break;
        }
    }
    const bool changed = newHover != hoverIndex_;
    hoverIndex_ = newHover;
    return changed;
}

}  // namespace winui

