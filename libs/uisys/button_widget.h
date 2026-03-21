#pragma once
#include "hwinterface/gt911.h"
#include "font.h"
#include <cstdint>
#include <functional>
#include <string>

namespace uisys {

// ─── Button mode ─────────────────────────────────────────────────────────────

enum class ButtonMode {
    TRIGGER,       // fires once on press
    HOLD,          // fires on hold start, fires again on release
    TOGGLE,        // alternates on/off each press
    TRIGGER_HOLD,  // fires on press AND separately on hold
};

// ─── Callback state values ────────────────────────────────────────────────────
//
// Passed as int to the callback so one callback handles all cases:
//
//  TRIGGER mode:
//    BTN_TRIGGER  (0) — pressed
//
//  HOLD mode:
//    BTN_HOLD_START (3) — hold threshold reached
//    BTN_HOLD_END   (4) — finger released after hold
//    BTN_HOLD_CANCEL(5) — finger moved off button during hold
//
//  TOGGLE mode:
//    BTN_TOGGLE_ON  (1) — toggled ON
//    BTN_TOGGLE_OFF (2) — toggled OFF
//
//  TRIGGER_HOLD mode:
//    BTN_TRIGGER    (0) — pressed (immediate)
//    BTN_HOLD_START (3) — additionally fires when hold threshold reached
//    BTN_HOLD_END   (4) — fires when released after hold
//    BTN_HOLD_CANCEL(5) — fires when finger moves off during hold

namespace ButtonState {
    constexpr int TRIGGER     = 0;  // TRIGGER / TRIGGER_HOLD: pressed
    constexpr int TOGGLE_ON   = 1;  // TOGGLE: turned on
    constexpr int TOGGLE_OFF  = 2;  // TOGGLE: turned off
    constexpr int HOLD_START  = 3;  // HOLD / TRIGGER_HOLD: hold began
    constexpr int HOLD_END    = 4;  // HOLD / TRIGGER_HOLD: released after hold
    constexpr int HOLD_CANCEL = 5;  // HOLD / TRIGGER_HOLD: moved off during hold
    constexpr int PRESS       = 6;  // raw press (available in all modes for monitoring)
    constexpr int RELEASE     = 7;  // raw release
}

// ─── Theme ───────────────────────────────────────────────────────────────────

struct ButtonTheme {
    uint16_t colNormal    = 0x2104;
    uint16_t colPressed   = 0x07E0;
    uint16_t colToggleOn  = 0x001F;
    uint16_t colHolding   = 0xFD20;  // orange — while hold is active
    uint16_t colBorder    = 0xFFFF;
    uint16_t colText      = 0xFFFF;

    static ButtonTheme Default()  { return {}; }
    static ButtonTheme Danger()   {
        ButtonTheme t;
        t.colNormal=0x4000; t.colPressed=0xF800;
        t.colToggleOn=0xF800; t.colHolding=0xFBE0;
        return t;
    }
    static ButtonTheme Military() {
        ButtonTheme t;
        t.colNormal=0x2A00; t.colPressed=0x4C00;
        t.colToggleOn=0x0400; t.colHolding=0x6300;
        t.colBorder=0x8C71;
        return t;
    }
    static ButtonTheme HUD() {
        ButtonTheme t;
        t.colNormal=0x0208; t.colPressed=0x07E0;
        t.colToggleOn=0x07E0; t.colHolding=0xFD20;
        t.colBorder=0x07E0; t.colText=0x07E0;
        return t;
    }
};

// ─── ButtonWidget ─────────────────────────────────────────────────────────────

class ButtonWidget {
public:
    using Callback = std::function<void(int state)>;

private:
    int         x, y, w, h;
    std::string label;
    ButtonMode  mode;
    Callback    onAction;
    ButtonTheme theme;
    Font        font;

    bool visible_widget = true;
    bool pressed        = false;
    bool toggleState    = false;
    bool holdActive     = false;

    bool hitTest(int tx, int ty) const {
        return tx >= x && tx <= x+w && ty >= y && ty <= y+h;
    }

    void fire(int s) { if (onAction) onAction(s); }

public:
    ButtonWidget() : x(0), y(0), w(0), h(0), mode(ButtonMode::TRIGGER) {}

    ButtonWidget(int x, int y, int w, int h,
                 const std::string& label, ButtonMode mode,
                 Callback cb = nullptr,
                 ButtonTheme theme = ButtonTheme::Default(),
                 Font font = Font::Medium())
        : x(x), y(y), w(w), h(h), label(label), mode(mode),
          onAction(cb), theme(theme), font(font) {}

    // ── Setters ───────────────────────────────────────────────────────────────

    void setTheme(const ButtonTheme& t)              { theme = t; }
    void setFont(const Font& f)                      { font  = f; }
    void setCallback(Callback cb)                    { onAction = cb; }
    void setLabel(const std::string& l)              { label = l; }
    void setToggleState(bool s)                      { toggleState = s; }
    void setBounds(int nx,int ny,int nw,int nh)      { x=nx;y=ny;w=nw;h=nh; }
    void setVisible(bool v)                          { visible_widget = v; }

    // ── Getters ───────────────────────────────────────────────────────────────

    bool isVisible()    const { return visible_widget; }
    bool isPressed()    const { return pressed; }
    bool isToggleOn()   const { return toggleState; }
    bool isHoldActive() const { return holdActive; }
    ButtonMode getMode() const { return mode; }

    // ── Event handler ─────────────────────────────────────────────────────────

    void handleEvent(const TouchEventData& e) {
        if (!visible_widget) return;

        switch (e.event) {

            case TouchEvent::PRESS:
                if (!hitTest(e.point.x, e.point.y)) break;
                pressed = true;

                if (mode == ButtonMode::TRIGGER) {
                    fire(ButtonState::TRIGGER);

                } else if (mode == ButtonMode::TRIGGER_HOLD) {
                    fire(ButtonState::TRIGGER);  // fires immediately on press

                } else if (mode == ButtonMode::TOGGLE) {
                    toggleState = !toggleState;
                    fire(toggleState ? ButtonState::TOGGLE_ON : ButtonState::TOGGLE_OFF);
                }
                // HOLD: nothing fires on press alone
                break;

            case TouchEvent::HOLD:
                if (!pressed || !hitTest(e.point.x, e.point.y)) break;

                if ((mode == ButtonMode::HOLD || mode == ButtonMode::TRIGGER_HOLD)
                    && !holdActive) {
                    holdActive = true;
                    fire(ButtonState::HOLD_START);
                }
                break;

            case TouchEvent::MOVE:
                if (!pressed) break;
                if (!hitTest(e.point.x, e.point.y)) {
                    // Finger left button area
                    if ((mode == ButtonMode::HOLD || mode == ButtonMode::TRIGGER_HOLD)
                        && holdActive) {
                        holdActive = false;
                        fire(ButtonState::HOLD_CANCEL);
                    }
                    pressed = false;
                }
                break;

            case TouchEvent::RELEASE:
                if (!pressed) break;
                pressed = false;

                if ((mode == ButtonMode::HOLD || mode == ButtonMode::TRIGGER_HOLD)
                    && holdActive) {
                    holdActive = false;
                    fire(ButtonState::HOLD_END);
                }
                break;
        }
    }

    // ── Draw ─────────────────────────────────────────────────────────────────

    template<typename GFX>
    void draw(GFX& gfx) const {
        if (!visible_widget) return;

        // Background color by state
        uint16_t bg;
        if (mode == ButtonMode::TOGGLE) {
            bg = toggleState ? theme.colToggleOn : theme.colNormal;
            if (pressed) bg = theme.colPressed;
        } else if (holdActive) {
            bg = theme.colHolding;
        } else if (pressed) {
            bg = theme.colPressed;
        } else {
            bg = theme.colNormal;
        }

        // Body
        gfx.fillRect(x, y, w, h, bg);

        // Border — thicker when active
        bool active = pressed || holdActive || toggleState;
        if (active) {
            gfx.drawRect(x,   y,   w,   h,   theme.colBorder);
            gfx.drawRect(x+1, y+1, w-2, h-2, theme.colBorder);
        } else {
            gfx.drawRect(x, y, w, h, theme.colBorder);
        }

        // Label
        font.apply(gfx);
        int tx = x + (w - font.textWidth((int)label.size())) / 2;
        int ty = y + (h - font.charH()) / 2;
        gfx.setCursor(tx, ty);
        gfx.setTextColor(theme.colText, bg);
        gfx.writeText(label.c_str());

        // Mode indicator dot — top right corner
        uint16_t dotCol = 0x0000;
        switch (mode) {
            case ButtonMode::TRIGGER:
                dotCol = gfx.color565(0xFF, 0xFF, 0x00);  // yellow
                break;
            case ButtonMode::HOLD:
                dotCol = holdActive
                    ? gfx.color565(0xFF, 0x80, 0x00)       // orange active
                    : gfx.color565(0x60, 0x30, 0x00);      // orange dim
                break;
            case ButtonMode::TOGGLE:
                dotCol = toggleState
                    ? gfx.color565(0x00, 0xFF, 0xFF)        // cyan ON
                    : gfx.color565(0x00, 0x40, 0x40);       // cyan dim
                break;
            case ButtonMode::TRIGGER_HOLD:
                // Split dot: half yellow (trigger) half orange (hold)
                dotCol = holdActive
                    ? gfx.color565(0xFF, 0x80, 0x00)        // orange when holding
                    : gfx.color565(0xFF, 0xC0, 0x00);       // gold = both modes
                break;
        }
        gfx.fillCircle(x+w-8, y+8, 4, dotCol);
    }
};

} // namespace uisys