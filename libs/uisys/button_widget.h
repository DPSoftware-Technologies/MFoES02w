#pragma once
#include "hwinterface/gt911.h"
#include "font.h"
#include <cstdint>
#include <functional>
#include <string>

namespace uisys {

enum class ButtonMode { TRIGGER, HOLD, TOGGLE };

struct ButtonTheme {
    uint16_t colNormal   = 0x2104;
    uint16_t colPressed  = 0x07E0;
    uint16_t colToggleOn = 0x001F;
    uint16_t colBorder   = 0xFFFF;
    uint16_t colText     = 0xFFFF;

    static ButtonTheme Default()  { return {}; }
    static ButtonTheme Danger()   { ButtonTheme t; t.colNormal=0x4000; t.colPressed=0xF800; t.colToggleOn=0xF800; return t; }
    static ButtonTheme Military() { ButtonTheme t; t.colNormal=0x2A00; t.colPressed=0x4C00; t.colToggleOn=0x0400; t.colBorder=0x8C71; return t; }
    static ButtonTheme HUD()      { ButtonTheme t; t.colNormal=0x0208; t.colPressed=0x07E0; t.colToggleOn=0x07E0; t.colBorder=0x07E0; t.colText=0x07E0; return t; }
};

class ButtonWidget {
public:
    using Callback = std::function<void(bool state)>;

private:
    int         x, y, w, h;
    std::string label;
    ButtonMode  mode;
    Callback    onAction;
    ButtonTheme theme;
    Font        font;

    bool pressed     = false;
    bool toggleState = false;
    bool holdActive  = false;

    bool hitTest(int tx, int ty) const {
        return tx >= x && tx <= x+w && ty >= y && ty <= y+h;
    }
    void fire(bool s) { if (onAction) onAction(s); }

bool visible_widget = true;

public:
    ButtonWidget() : x(0), y(0), w(0), h(0), mode(ButtonMode::TRIGGER) {}

    ButtonWidget(int x, int y, int w, int h,
                 const std::string& label, ButtonMode mode,
                 Callback cb = nullptr,
                 ButtonTheme theme = ButtonTheme::Default(),
                 Font font = Font::Medium())
        : x(x), y(y), w(w), h(h), label(label), mode(mode),
          onAction(cb), theme(theme), font(font) {}

    void setTheme(const ButtonTheme& t) { theme = t; }
    void setFont(const Font& f)         { font  = f; }
    void setCallback(Callback cb)       { onAction = cb; }
        void setVisible(bool v) { visible_widget = v; }
    bool isVisible()   const { return visible_widget; }

    void setLabel(const std::string& l) { label = l; }
    void setToggleState(bool s)         { toggleState = s; }
    void setBounds(int nx,int ny,int nw,int nh) { x=nx;y=ny;w=nw;h=nh; }

    bool isPressed()    const { return pressed; }
    bool isToggleOn()   const { return toggleState; }
    bool isHoldActive() const { return holdActive; }

    void handleEvent(const TouchEventData& e) {
        if (!visible_widget) return;
        switch (e.event) {
            case TouchEvent::PRESS:
                if (!hitTest(e.point.x, e.point.y)) break;
                pressed = true;
                if (mode == ButtonMode::TRIGGER) fire(true);
                else if (mode == ButtonMode::TOGGLE) { toggleState = !toggleState; fire(toggleState); }
                break;
            case TouchEvent::HOLD:
                if (!pressed || !hitTest(e.point.x, e.point.y)) break;
                if (mode == ButtonMode::HOLD && !holdActive) { holdActive = true; fire(true); }
                break;
            case TouchEvent::MOVE:
                if (pressed && !hitTest(e.point.x, e.point.y)) {
                    if (mode == ButtonMode::HOLD && holdActive) { holdActive = false; fire(false); }
                    pressed = false;
                }
                break;
            case TouchEvent::RELEASE:
                if (!pressed) break;
                pressed = false;
                if (mode == ButtonMode::HOLD && holdActive) { holdActive = false; fire(false); }
                break;
        }
    }

    template<typename GFX>
    void draw(GFX& gfx) const {
        if (!visible_widget) return;
        uint16_t bg;
        if (mode == ButtonMode::TOGGLE)     bg = toggleState ? theme.colToggleOn : theme.colNormal;
        else if (pressed || holdActive)     bg = theme.colPressed;
        else                                bg = theme.colNormal;

        gfx.fillRect(x, y, w, h, bg);
        if (pressed || holdActive || toggleState) {
            gfx.drawRect(x,   y,   w,   h,   theme.colBorder);
            gfx.drawRect(x+1, y+1, w-2, h-2, theme.colBorder);
        } else {
            gfx.drawRect(x, y, w, h, theme.colBorder);
        }

        font.apply(gfx);
        int tx = x + (w - font.textWidth((int)label.size())) / 2;
        int ty = y + (h - font.charH()) / 2;
        gfx.setCursor(tx, ty);
        gfx.setTextColor(theme.colText, bg);
        gfx.writeText(label.c_str());

        // Mode dot
        uint16_t dotCol;
        switch (mode) {
            case ButtonMode::TRIGGER: dotCol = gfx.color565(0xFF,0xFF,0x00); break;
            case ButtonMode::HOLD:    dotCol = holdActive ? gfx.color565(0xFF,0x80,0x00) : gfx.color565(0x60,0x30,0x00); break;
            case ButtonMode::TOGGLE:  dotCol = toggleState ? gfx.color565(0x00,0xFF,0xFF) : gfx.color565(0x00,0x40,0x40); break;
        }
        gfx.fillCircle(x+w-8, y+8, 4, dotCol);
    }
};

} // namespace uisys
