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
    bool focused    = false;
    bool numericOnly = false;
    size_t maxLen   = 64;

    Callback onChange;
    Callback onConfirm;
    Font     font;

    KeyboardWidget* keyboard = nullptr;  // shared keyboard instance

    uint16_t colBg          = 0x0861;
    uint16_t colBgFocused   = 0x18C3;
    uint16_t colBorder      = 0x8C71;
    uint16_t colBorderFocus = 0x07E0;
    uint16_t colText        = 0xFFFF;
    uint16_t colPlaceholder = 0x8C71;
    uint16_t colCursor      = 0x07E0;

    // Cursor blink state (driven by draw call count)
    mutable int blinkCounter = 0;

    bool hitTest(int tx, int ty) const {
        return tx >= x && tx <= x+w && ty >= y && ty <= y+h;
    }

bool visible_widget = true;

public:
    TextEditWidget() : x(0), y(0), w(200), h(40) {}

    TextEditWidget(int x, int y, int w, int h,
                   const std::string& placeholder = "",
                   KeyboardWidget* keyboard = nullptr,
                   bool numericOnly = false,
                   Font font = Font::Medium())
        : x(x), y(y), w(w), h(h),
          placeholder(placeholder), numericOnly(numericOnly),
          font(font), keyboard(keyboard) {}

        void setVisible(bool v) { visible_widget = v; }
    bool isVisible()   const { return visible_widget; }

    void setFont(const Font& f)         { font = f; }
    void setKeyboard(KeyboardWidget* kb) { keyboard = kb; }
    void setMaxLen(size_t len)           { maxLen = len; }
    void setNumericOnly(bool n)          { numericOnly = n; }
    void setPlaceholder(const std::string& p) { placeholder = p; }
    void setText(const std::string& t)   { text = t.substr(0, maxLen); }
    void setCallback(Callback cb)        { onChange = cb; }
    void setOnConfirm(Callback cb)       { onConfirm = cb; }

    void setColors(uint16_t bg, uint16_t bgFocus, uint16_t border,
                   uint16_t borderFocus, uint16_t text, uint16_t placeholder) {
        colBg=bg; colBgFocused=bgFocus; colBorder=border;
        colBorderFocus=borderFocus; colText=text; colPlaceholder=placeholder;
    }

    const std::string& getText() const { return text; }
    bool isFocused()             const { return focused; }

    void focus() {
        focused = true;
        if (keyboard) {
            keyboard->setCallback([this](const std::string& t) {
                text = t;
                if (onChange) onChange(text);
                // NOTE: never call unfocus() from here — causes re-entry into hide()
            });
            keyboard->setMaxLen(maxLen);
            keyboard->showWithText(text, numericOnly);
        }
    }

    void unfocus() {
        if (!focused) return;  // guard re-entry
        focused = false;
        if (keyboard && keyboard->isVisible()) {
            keyboard->setCallback(nullptr);  // clear first to prevent re-entry
            keyboard->hide();
        }
        if (onConfirm) onConfirm(text);
    }

    void handleEvent(const TouchEventData& e) {
        if (!visible_widget) return;
        if (e.event == TouchEvent::PRESS) {
            if (hitTest(e.point.x, e.point.y)) {
                if (!focused) focus();
            } else {
                if (focused) unfocus();
            }
        }
    }

    template<typename GFX>
    void draw(GFX& gfx) const {
        if (!visible_widget) return;
        uint16_t bg  = focused ? colBgFocused : colBg;
        uint16_t brd = focused ? colBorderFocus : colBorder;

        gfx.fillRect(x, y, w, h, bg);
        // Double border when focused
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
            int availW   = w - 16 - font.charW(); // leave room for cursor
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
