#include "win/ui/nodes/DropdownSelectorNode.h"

#include <algorithm>
#include <string_view>

#include <d2d1helper.h>
#include <wrl/client.h>

namespace winui {

namespace {
float RectWidth(const D2D1_RECT_F& r) { return r.right - r.left; }
float RectHeight(const D2D1_RECT_F& r) { return r.bottom - r.top; }

std::wstring ToSingleLine(std::wstring_view text) {
    std::wstring out;
    out.reserve(text.size());
    bool lastSpace = true;
    for (wchar_t ch : text) {
        if (ch == L'\r' || ch == L'\n' || ch == L'\t') {
            ch = L' ';
        }
        const bool isSpace = ch == L' ';
        if (isSpace) {
            if (lastSpace) {
                continue;
            }
            lastSpace = true;
        } else {
            lastSpace = false;
        }
        out.push_back(ch);
    }
    while (!out.empty() && out.front() == L' ') {
        out.erase(out.begin());
    }
    while (!out.empty() && out.back() == L' ') {
        out.pop_back();
    }
    return out;
}

void DrawSingleLineEllipsized(const RenderResources& resources,
                              std::wstring_view text, const D2D1_RECT_F& rect,
                              ID2D1Brush* brush,
                              DWRITE_TEXT_ALIGNMENT alignment) {
    if (!resources.target || !resources.textFormat || !brush) {
        return;
    }

    const float w = std::max(0.0f, RectWidth(rect));
    const float h = std::max(0.0f, RectHeight(rect));
    if (w <= 1.0f || h <= 1.0f) {
        return;
    }

    std::wstring line = ToSingleLine(text);
    if (line.empty()) {
        return;
    }

    auto drawFallback = [&]() {
        const std::size_t maxChars =
            static_cast<std::size_t>(std::max(1.0f, w / 8.0f));
        if (line.size() > maxChars) {
            if (maxChars <= 1) {
                line = L"…";
            } else {
                line = line.substr(0, maxChars - 1) + L"…";
            }
        }

        resources.target->PushAxisAlignedClip(rect,
                                             D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        const auto oldText = resources.textFormat->GetTextAlignment();
        const auto oldPara = resources.textFormat->GetParagraphAlignment();
        (void)resources.textFormat->SetTextAlignment(alignment);
        (void)resources.textFormat->SetParagraphAlignment(
            DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        resources.target->DrawText(line.c_str(), static_cast<UINT32>(line.size()),
                                   resources.textFormat, rect, brush);
        (void)resources.textFormat->SetTextAlignment(oldText);
        (void)resources.textFormat->SetParagraphAlignment(oldPara);
        resources.target->PopAxisAlignedClip();
    };

    if (!resources.dwriteFactory) {
        drawFallback();
        return;
    }

    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    if (FAILED(resources.dwriteFactory->CreateTextLayout(
            line.c_str(), static_cast<UINT32>(line.size()), resources.textFormat,
            w, h, &layout)) ||
        !layout) {
        drawFallback();
        return;
    }

    (void)layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    (void)layout->SetTextAlignment(alignment);
    (void)layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    DWRITE_TRIMMING trimming{};
    trimming.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER;

    Microsoft::WRL::ComPtr<IDWriteInlineObject> ellipsis;
    if (SUCCEEDED(resources.dwriteFactory->CreateEllipsisTrimmingSign(
            resources.textFormat, &ellipsis)) &&
        ellipsis) {
        (void)layout->SetTrimming(&trimming, ellipsis.Get());
    }

    resources.target->PushAxisAlignedClip(rect,
                                         D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    resources.target->DrawTextLayout(D2D1::Point2F(rect.left, rect.top),
                                     layout.Get(), brush,
                                     D2D1_DRAW_TEXT_OPTIONS_CLIP);
    resources.target->PopAxisAlignedClip();
}

struct TextAlignmentGuard {
    explicit TextAlignmentGuard(IDWriteTextFormat* format,
                                DWRITE_TEXT_ALIGNMENT textAlignment,
                                DWRITE_PARAGRAPH_ALIGNMENT paragraphAlignment)
        : format_(format),
          oldText_(format ? format->GetTextAlignment() : DWRITE_TEXT_ALIGNMENT_LEADING),
          oldPara_(format ? format->GetParagraphAlignment() : DWRITE_PARAGRAPH_ALIGNMENT_NEAR) {
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
    if (!onChanged_) {
        close();
    }
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
    auto* text = resources.textBrush ? resources.textBrush : resources.gridBrush;
    if (!bg || !text) {
        return;
    }

    const bool enabled = static_cast<bool>(onChanged_);
    const float originalOpacity = text->GetOpacity();
    if (!enabled) {
        text->SetOpacity(originalOpacity * 0.45f);
    }

    // Base box.
    resources.target->FillRectangle(bounds_, bg);

    const auto leftArrow = D2D1::RectF(bounds_.left, bounds_.top, bounds_.left + arrowWidth_, bounds_.bottom);
    const auto rightArrow = D2D1::RectF(bounds_.right - arrowWidth_, bounds_.top, bounds_.right, bounds_.bottom);
    const auto labelRect = D2D1::RectF(leftArrow.right, bounds_.top, rightArrow.left, bounds_.bottom);

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
        const auto textRect =
            D2D1::RectF(labelRect.left + padding_, labelRect.top, labelRect.right - padding_,
                        labelRect.bottom);
        DrawSingleLineEllipsized(resources, label, textRect, text,
                                 DWRITE_TEXT_ALIGNMENT_CENTER);

    text->SetOpacity(originalOpacity);
}

void DropdownSelectorNode::drawOverlay(const RenderResources& resources) {
    if (!open_) {
        return;
    }
    if (!resources.target || !resources.textFormat) {
        return;
    }
    auto* bg = resources.cardBrush ? resources.cardBrush : resources.panelBrush;
    auto* text = resources.textBrush ? resources.textBrush : resources.gridBrush;
    auto* accent = resources.accentBrush ? resources.accentBrush : text;
    if (!bg || !text) {
        return;
    }

    const auto r = overlayRect();
    resources.target->FillRectangle(r, bg);

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
        DrawSingleLineEllipsized(resources, name, tr, selected ? accent : text,
                                 DWRITE_TEXT_ALIGNMENT_LEADING);
        }

    // Page controls (paginated list; no scrolling).
    const auto prev = prevPageRect();
    const auto next = nextPageRect();

    const bool hasPrev = pageIndex_ > 0;
    const bool hasNext = pages > 0 && pageIndex_ < pages - 1;

    const wchar_t* prevText = hasPrev ? L"Prev" : L"Prev";
    const wchar_t* nextText = hasNext ? L"Next" : L"Next";

    auto drawButton = [&](const D2D1_RECT_F& br, const wchar_t* label, bool enabled) {
        ID2D1SolidColorBrush* b = enabled ? text : text;
        const float original = b->GetOpacity();
        if (!enabled) {
            b->SetOpacity(original * 0.35f);
        }
        const auto tr = D2D1::RectF(br.left + padding_, br.top + 2.0f, br.right - padding_, br.bottom - 2.0f);
        resources.target->DrawText(label, static_cast<UINT32>(wcslen(label)),
                                   resources.textFormat, tr, b);
        b->SetOpacity(original);
    };
    drawButton(prev, prevText, hasPrev);
    drawButton(next, nextText, hasNext);
}

bool DropdownSelectorNode::onPointerDown(float x, float y) {
    if (!hit(bounds_, x, y)) {
        return false;
    }
    if (!onChanged_) {
        return true;
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
