#pragma once
#include "hwinterface/gt911.h"
#include "font.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <cstdio>

namespace uisys {

class ComboBoxWidget {
public:
    using Callback = std::function<void(int index, const std::string& value)>;

private:
    int x, y, w, h;
    std::vector<std::string> items;
    int  selectedIndex = 0;
    bool isOpen        = false;
    Callback onChange;
    Font     font;

    // Scrollable dropdown state
    int  scrollOffset  = 0;   // first visible item index
    int  maxVisible    = 4;   // max items shown at once
    int  hoveredIndex  = -1;

    uint16_t colBg       = 0x2104;
    uint16_t colBgOpen   = 0x18C3;
    uint16_t colSelected = 0x07E0;
    uint16_t colHover    = 0x3186;
    uint16_t colBorder   = 0xFFFF;
    uint16_t colText     = 0xFFFF;
    uint16_t colArrow    = 0x07E0;
    uint16_t colScrollBg = 0x18C3;
    uint16_t colScrollFg = 0x07E0;

    static constexpr int SCROLL_W    = 12;  // scrollbar width
    static constexpr int ARROW_BTN_H = 24;  // up/down scroll button height

    int itemH() const { return font.charH() + 12; }

    int dropdownH() const {
        int visible = visibleCount();
        return visible * itemH() + (needsScroll() ? 0 : 0);
    }

    int visibleCount() const {
        return (int)items.size() < maxVisible ? (int)items.size() : maxVisible;
    }

    bool needsScroll() const {
        return (int)items.size() > maxVisible;
    }

    bool hitTestHeader(int tx, int ty) const {
        return tx >= x && tx <= x+w && ty >= y && ty <= y+h;
    }

    // Returns item index or -1, accounting for scroll offset
    int hitTestDropdown(int tx, int ty) const {
        if (!isOpen) return -1;
        int dropX = x;
        int dropW = needsScroll() ? w - SCROLL_W : w;
        if (tx < dropX || tx > dropX + dropW) return -1;
        int dropY = y + h;
        int dh    = dropdownH();
        if (ty < dropY || ty > dropY + dh) return -1;
        int row = (ty - dropY) / itemH();
        int idx = scrollOffset + row;
        if (idx < 0 || idx >= (int)items.size()) return -1;
        return idx;
    }

    // Returns true if touch is on scroll up button
    bool hitScrollUp(int tx, int ty) const {
        if (!isOpen || !needsScroll()) return false;
        int sx = x + w - SCROLL_W;
        int sy = y + h;
        return tx >= sx && tx <= sx + SCROLL_W && ty >= sy && ty <= sy + ARROW_BTN_H;
    }

    // Returns true if touch is on scroll down button
    bool hitScrollDown(int tx, int ty) const {
        if (!isOpen || !needsScroll()) return false;
        int sx = x + w - SCROLL_W;
        int sy = y + h + dropdownH() - ARROW_BTN_H;
        return tx >= sx && tx <= sx + SCROLL_W && ty >= sy && ty <= sy + ARROW_BTN_H;
    }

    void clampScroll() {
        int maxOff = (int)items.size() - maxVisible;
        if (maxOff < 0) maxOff = 0;
        if (scrollOffset < 0)      scrollOffset = 0;
        if (scrollOffset > maxOff) scrollOffset = maxOff;
    }

bool visible_widget = true;

public:
    ComboBoxWidget() : x(0), y(0), w(200), h(40) {}

    ComboBoxWidget(int x, int y, int w, int h,
                   const std::vector<std::string>& items,
                   int defaultIndex = 0,
                   Callback cb = nullptr,
                   Font font = Font::Medium(),
                   int maxVisible = 4)
        : x(x), y(y), w(w), h(h),
          items(items), selectedIndex(defaultIndex),
          onChange(cb), font(font), maxVisible(maxVisible)
    {}

        void setVisible(bool v) { visible_widget = v; }
    bool isVisible()   const { return visible_widget; }

    void addItem(const std::string& item) { items.push_back(item); }
    void clearItems() { items.clear(); selectedIndex = 0; scrollOffset = 0; }

    void setItems(const std::vector<std::string>& newItems, int defaultIndex = 0) {
        items = newItems; selectedIndex = defaultIndex;
        scrollOffset = 0; isOpen = false;
    }

    int                getSelectedIndex() const { return selectedIndex; }
    const std::string& getSelectedValue() const {
        static std::string empty = "";
        return items.empty() ? empty : items[selectedIndex];
    }

    void setSelectedIndex(int idx) {
        if (idx >= 0 && idx < (int)items.size()) selectedIndex = idx;
    }

    void setFont(const Font& f)        { font = f; }
    void setMaxVisible(int n)          { maxVisible = n; }
    void setCallback(Callback cb)      { onChange = cb; }
    bool isDropdownOpen() const        { return isOpen; }

    void setColors(uint16_t bg, uint16_t bgOpen, uint16_t selected,
                   uint16_t border, uint16_t text) {
        colBg=bg; colBgOpen=bgOpen; colSelected=selected; colBorder=border; colText=text;
    }

    void handleEvent(const TouchEventData& e) {
        if (!visible_widget) return;
        switch (e.event) {

            case TouchEvent::PRESS:
                if (isOpen) {
                    // Scroll buttons
                    if (hitScrollUp(e.point.x, e.point.y)) {
                        scrollOffset--;
                        clampScroll();
                        break;
                    }
                    if (hitScrollDown(e.point.x, e.point.y)) {
                        scrollOffset++;
                        clampScroll();
                        break;
                    }
                    // Item selection
                    int idx = hitTestDropdown(e.point.x, e.point.y);
                    if (idx >= 0) {
                        selectedIndex = idx;
                        if (onChange) onChange(selectedIndex, items[selectedIndex]);
                    }
                    isOpen = false;
                } else {
                    if (hitTestHeader(e.point.x, e.point.y)) {
                        isOpen = true;
                        // Auto-scroll to selected
                        scrollOffset = selectedIndex;
                        clampScroll();
                    }
                }
                break;

            case TouchEvent::MOVE:
                if (isOpen) {
                    hoveredIndex = hitTestDropdown(e.point.x, e.point.y);

                    // Drag-scroll: if dragging near top/bottom of dropdown
                    int dropY = y + h;
                    int dh    = dropdownH();
                    if (e.point.y < dropY + 20 && e.point.y > dropY) {
                        scrollOffset--;
                        clampScroll();
                    } else if (e.point.y > dropY + dh - 20 && e.point.y < dropY + dh) {
                        scrollOffset++;
                        clampScroll();
                    }
                }
                break;

            case TouchEvent::RELEASE:
                hoveredIndex = -1;
                break;

            default: break;
        }
    }

    template<typename GFX>
    void draw(GFX& gfx) const {
        if (!visible_widget) return;
        // ── Header ────────────────────────────────────────────────────────────
        uint16_t headerBg = isOpen ? colBgOpen : colBg;
        gfx.fillRect(x, y, w, h, headerBg);
        gfx.drawRect(x, y, w, h, isOpen ? colSelected : colBorder);

        font.apply(gfx);
        if (!items.empty()) {
            gfx.setCursor(x + 8, y + (h - font.charH()) / 2);
            gfx.setTextColor(colText, headerBg);
            gfx.writeText(items[selectedIndex].c_str());
        }

        // Chevron arrow
        int arrowX = x + w - 18;
        int arrowY = y + (h - 8) / 2;
        if (isOpen) {
            gfx.drawLine(arrowX,   arrowY+5, arrowX+4, arrowY,   colArrow);
            gfx.drawLine(arrowX+4, arrowY,   arrowX+8, arrowY+5, colArrow);
        } else {
            gfx.drawLine(arrowX,   arrowY,   arrowX+4, arrowY+5, colArrow);
            gfx.drawLine(arrowX+4, arrowY+5, arrowX+8, arrowY,   colArrow);
        }

        if (!isOpen) return;

        // ── Dropdown ─────────────────────────────────────────────────────────
        int dropY   = y + h;
        int dh      = dropdownH();
        int dropW   = needsScroll() ? w - SCROLL_W : w;
        int visible = visibleCount();

        gfx.fillRect(x, dropY, dropW, dh, colBg);
        gfx.drawRect(x, dropY, dropW, dh, colBorder);

        for (int i = 0; i < visible; i++) {
            int itemIdx = scrollOffset + i;
            if (itemIdx >= (int)items.size()) break;

            int iy = dropY + i * itemH();

            uint16_t rowBg;
            if (itemIdx == selectedIndex) rowBg = colSelected;
            else if (itemIdx == hoveredIndex) rowBg = colHover;
            else rowBg = colBg;

            gfx.fillRect(x+1, iy+1, dropW-2, itemH()-1, rowBg);

            // Divider
            if (i > 0)
                gfx.drawLine(x+4, iy, x+dropW-4, iy, colBorder);

            // Text
            font.apply(gfx);
            gfx.setCursor(x + 8, iy + (itemH() - font.charH()) / 2);
            gfx.setTextColor(colText, rowBg);
            gfx.writeText(items[itemIdx].c_str());

            // Checkmark on selected
            if (itemIdx == selectedIndex) {
                int ckx = x + dropW - 16;
                int cky = iy + itemH() / 2;
                gfx.drawLine(ckx,   cky,   ckx+3, cky+3, colText);
                gfx.drawLine(ckx+3, cky+3, ckx+7, cky-2, colText);
            }
        }

        // ── Scrollbar ─────────────────────────────────────────────────────────
        if (needsScroll()) {
            int sx = x + w - SCROLL_W;

            // Track
            gfx.fillRect(sx, dropY, SCROLL_W, dh, colScrollBg);
            gfx.drawRect(sx, dropY, SCROLL_W, dh, colBorder);

            // Up button
            gfx.fillRect(sx, dropY, SCROLL_W, ARROW_BTN_H,
                scrollOffset > 0 ? colScrollFg : colScrollBg);
            // Up arrow symbol
            int ax = sx + SCROLL_W/2 - 3;
            int ay = dropY + ARROW_BTN_H/2 - 2;
            gfx.drawLine(ax,   ay+4, ax+3, ay,   colText);
            gfx.drawLine(ax+3, ay,   ax+6, ay+4, colText);

            // Down button
            int downY = dropY + dh - ARROW_BTN_H;
            int maxOff = (int)items.size() - maxVisible;
            gfx.fillRect(sx, downY, SCROLL_W, ARROW_BTN_H,
                scrollOffset < maxOff ? colScrollFg : colScrollBg);
            // Down arrow symbol
            ax = sx + SCROLL_W/2 - 3;
            ay = downY + ARROW_BTN_H/2 - 2;
            gfx.drawLine(ax,   ay,   ax+3, ay+4, colText);
            gfx.drawLine(ax+3, ay+4, ax+6, ay,   colText);

            // Scroll thumb
            int thumbAreaH = dh - 2 * ARROW_BTN_H;
            if (thumbAreaH > 0 && items.size() > 0) {
                float thumbRatio = (float)maxVisible / (float)items.size();
                int thumbH = (int)(thumbAreaH * thumbRatio);
                if (thumbH < 8) thumbH = 8;
                float scrollRatio = (float)scrollOffset / (float)((int)items.size() - maxVisible);
                int thumbY = dropY + ARROW_BTN_H + (int)(scrollRatio * (thumbAreaH - thumbH));
                gfx.fillRect(sx+2, thumbY, SCROLL_W-4, thumbH, colText);
            }
        }
    }
};

} // namespace uisys
