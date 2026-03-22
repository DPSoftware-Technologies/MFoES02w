#pragma once
#include "hwinterface/gt911.h"
#include "font.h"
#include "keyboard_widget.h"
#include <cstdint>
#include <functional>
#include <string>
#include <cstring>

namespace uisys {

class TextEditWidget {
public:
    using Callback = std::function<void(const std::string& text)>;

private:
    int x, y, w, h;
    std::string text;
    std::string placeholder;
    bool focused     = false;
    bool numericOnly = false;
    size_t maxLen    = 64;

    Callback onChange;
    Callback onConfirm;
    Font     font;

    KeyboardWidget* keyboard = nullptr;

    uint32_t colBg          = 0xFF080C08u;  // was 0x0861  →  rgb(8,12,8)
    uint32_t colBgFocused   = 0xFF181818u;  // was 0x18C3  →  rgb(24,24,24)
    uint32_t colBorder      = 0xFF8C8E8Cu;  // was 0x8C71  →  rgb(140,142,140)
    uint32_t colBorderFocus = 0xFF00FF00u;  // was 0x07E0  →  rgb(0,255,0)
    uint32_t colText        = 0xFFFFFFFFu;  // was 0xFFFF  →  rgb(255,255,255)
    uint32_t colPlaceholder = 0xFF8C8E8Cu;  // was 0x8C71  →  rgb(140,142,140)
    uint32_t colCursor      = 0xFF00FF00u;  // was 0x07E0  →  rgb(0,255,0)

    // Cursor blink state (driven by draw call count)
    mutable int blinkCounter = 0;

    bool hitTest(int tx, int ty) const;

    bool visible_widget = true;

public:
    TextEditWidget();
    TextEditWidget(int x, int y, int w, int h,
                   const std::string& placeholder = "",
                   KeyboardWidget* keyboard = nullptr,
                   bool numericOnly = false,
                   Font font = Font::Medium());

    void setVisible(bool v);
    bool isVisible() const;

    void setFont(const Font& f);
    void setKeyboard(KeyboardWidget* kb);
    void setMaxLen(size_t len);
    void setNumericOnly(bool n);
    void setPlaceholder(const std::string& p);
    void setText(const std::string& t);
    void setCallback(Callback cb);
    void setOnConfirm(Callback cb);
    void setColors(uint32_t bg, uint32_t bgFocus, uint32_t border,
                   uint32_t borderFocus, uint32_t text, uint32_t placeholder);

    const std::string& getText()    const;
    bool               isFocused()  const;

    void focus();
    void unfocus();

    void handleEvent(const TouchEventData& e);

    //  Draw — templated, must remain in header 

    template<typename GFX>
    void draw(GFX& gfx) const {
        if (!visible_widget) return;

        uint32_t bg  = focused ? colBgFocused : colBg;
        uint32_t brd = focused ? colBorderFocus : colBorder;

        gfx.fillRect(x, y, w, h, bg);
        gfx.drawRect(x, y, w, h, brd);
        if (focused) gfx.drawRect(x+1, y+1, w-2, h-2, brd);

        font.apply(gfx);

        int ty = y + (h - font.charH()) / 2;
        int tx = x + 8;

        if (text.empty() && !focused) {
            // Placeholder
            gfx.setCursor(tx, ty);
            gfx.setTextColor(colPlaceholder, bg);
            gfx.writeText(placeholder.c_str());
        } else {
            // Text — clip to box width
            int availW   = w - 16 - font.charW();  // leave room for cursor
            int maxChars = availW / font.charW();
            std::string display = text;
            if ((int)display.size() > maxChars)
                display = display.substr(display.size() - maxChars);

            gfx.setCursor(tx, ty);
            gfx.setTextColor(colText, bg);
            gfx.writeText(display.c_str());

            // Blinking cursor
            blinkCounter++;
            if (focused && (blinkCounter / 30) % 2 == 0) {
                int cursorX = tx + font.textWidth((int)display.size());
                gfx.fillRect(cursorX, ty, 2, font.charH(), colCursor);
            }
        }
    }
};

} // namespace uisys
