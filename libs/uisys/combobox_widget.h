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

    uint32_t colBg       = 0xFF212021u;  // was 0x2104  →  rgb(33,32,33)
    uint32_t colBgOpen   = 0xFF181818u;  // was 0x18C3  →  rgb(24,24,24)
    uint32_t colSelected = 0xFF00FF00u;  // was 0x07E0  →  rgb(0,255,0)
    uint32_t colHover    = 0xFF313031u;  // was 0x3186  →  rgb(49,48,49)
    uint32_t colBorder   = 0xFFFFFFFFu;  // was 0xFFFF  →  rgb(255,255,255)
    uint32_t colText     = 0xFFFFFFFFu;  // was 0xFFFF  →  rgb(255,255,255)
    uint32_t colArrow    = 0xFF00FF00u;  // was 0x07E0  →  rgb(0,255,0)
    uint32_t colScrollBg = 0xFF181818u;  // was 0x18C3  →  rgb(24,24,24)
    uint32_t colScrollFg = 0xFF00FF00u;  // was 0x07E0  →  rgb(0,255,0)

    static constexpr int SCROLL_W    = 12;  // scrollbar width
    static constexpr int ARROW_BTN_H = 24;  // up/down scroll button height

    int  itemH()       const;
    int  dropdownH()   const;
    int  visibleCount() const;
    bool needsScroll() const;

    bool hitTestHeader(int tx, int ty) const;
    int  hitTestDropdown(int tx, int ty) const;
    bool hitScrollUp(int tx, int ty)   const;
    bool hitScrollDown(int tx, int ty) const;
    void clampScroll();

    bool visible_widget = true;

public:
    ComboBoxWidget();
    ComboBoxWidget(int x, int y, int w, int h,
                   const std::vector<std::string>& items,
                   int defaultIndex = 0,
                   Callback cb = nullptr,
                   Font font = Font::Medium(),
                   int maxVisible = 4);

    void setVisible(bool v);
    bool isVisible() const;

    void               addItem(const std::string& item);
    void               clearItems();
    void               setItems(const std::vector<std::string>& newItems, int defaultIndex = 0);
    int                getSelectedIndex() const;
    const std::string& getSelectedValue() const;
    void               setSelectedIndex(int idx);

    void setFont(const Font& f);
    void setMaxVisible(int n);
    void setCallback(Callback cb);
    bool isDropdownOpen() const;

    void setColors(uint32_t bg, uint32_t bgOpen, uint32_t selected,
                   uint32_t border, uint32_t text);

    void handleEvent(const TouchEventData& e);

    //  Draw — templated, must remain in header 

    template<typename GFX>
    void draw(GFX& gfx) const {
        if (!visible_widget) return;

        //  Header 
        uint32_t headerBg = isOpen ? colBgOpen : colBg;
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

        //  Dropdown 
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

            uint32_t rowBg;
            if (itemIdx == selectedIndex)  rowBg = colSelected;
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

        //  Scrollbar 
        if (needsScroll()) {
            int sx = x + w - SCROLL_W;

            // Track
            gfx.fillRect(sx, dropY, SCROLL_W, dh, colScrollBg);
            gfx.drawRect(sx, dropY, SCROLL_W, dh, colBorder);

            // Up button
            gfx.fillRect(sx, dropY, SCROLL_W, ARROW_BTN_H,
                scrollOffset > 0 ? colScrollFg : colScrollBg);
            int ax = sx + SCROLL_W/2 - 3;
            int ay = dropY + ARROW_BTN_H/2 - 2;
            gfx.drawLine(ax,   ay+4, ax+3, ay,   colText);
            gfx.drawLine(ax+3, ay,   ax+6, ay+4, colText);

            // Down button
            int downY  = dropY + dh - ARROW_BTN_H;
            int maxOff = (int)items.size() - maxVisible;
            gfx.fillRect(sx, downY, SCROLL_W, ARROW_BTN_H,
                scrollOffset < maxOff ? colScrollFg : colScrollBg);
            ax = sx + SCROLL_W/2 - 3;
            ay = downY + ARROW_BTN_H/2 - 2;
            gfx.drawLine(ax,   ay,   ax+3, ay+4, colText);
            gfx.drawLine(ax+3, ay+4, ax+6, ay,   colText);

            // Scroll thumb
            int thumbAreaH = dh - 2 * ARROW_BTN_H;
            if (thumbAreaH > 0 && items.size() > 0) {
                float thumbRatio = (float)maxVisible / (float)items.size();
                int   thumbH     = (int)(thumbAreaH * thumbRatio);
                if (thumbH < 8) thumbH = 8;
                float scrollRatio = (float)scrollOffset / (float)((int)items.size() - maxVisible);
                int   thumbY      = dropY + ARROW_BTN_H + (int)(scrollRatio * (thumbAreaH - thumbH));
                gfx.fillRect(sx+2, thumbY, SCROLL_W-4, thumbH, colText);
            }
        }
    }
};

} // namespace uisys
