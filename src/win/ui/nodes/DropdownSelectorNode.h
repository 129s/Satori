#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <d2d1.h>

#include "win/ui/RenderResources.h"
#include "win/ui/layout/UILayoutNode.h"

namespace winui {

// Dropdown selector with:
// - left/right arrows (wrap)
// - center label (click to open)
// - paginated dropdown list (no scrolling)
class DropdownSelectorNode : public UILayoutNode {
public:
    void setItems(std::vector<std::wstring> items);
    void setSelectedIndex(int index);
    int selectedIndex() const { return selectedIndex_; }
    std::wstring selectedLabel() const;

    void setOnChanged(std::function<void(int)> onChanged);

    void setPageSize(int pageSize);
    bool isOpen() const { return open_; }
    void close();

    float preferredHeight(float) const override { return height_; }
    void arrange(const D2D1_RECT_F& bounds) override;
    void draw(const RenderResources& resources) override;

    // Draws the dropdown list overlay (should be drawn after the main UI to appear on top).
    void drawOverlay(const RenderResources& resources);

    bool onPointerDown(float x, float y) override;
    bool onPointerMove(float x, float y) override;
    void onPointerUp() override;

    // Pointer handling for the overlay list (call from host before routing to root layout).
    bool onOverlayPointerDown(float x, float y);
    bool onOverlayPointerMove(float x, float y);

private:
    bool hit(const D2D1_RECT_F& r, float x, float y) const;
    void notifyChanged();
    void selectWrapped(int newIndex);
    void openAtSelection();

    int pageCount() const;
    D2D1_RECT_F overlayRect() const;
    D2D1_RECT_F itemRect(int localRow) const;
    D2D1_RECT_F prevPageRect() const;
    D2D1_RECT_F nextPageRect() const;

    std::vector<std::wstring> items_;
    int selectedIndex_ = 0;
    std::function<void(int)> onChanged_;

    float height_ = 28.0f;
    float arrowWidth_ = 24.0f;
    float padding_ = 6.0f;

    bool open_ = false;
    int pageSize_ = 6;
    int pageIndex_ = 0;
    std::optional<int> hoverIndex_;
};

}  // namespace winui

