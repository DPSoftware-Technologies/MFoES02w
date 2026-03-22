#pragma once
#include "hwinterface/gt911.h"
#include "font.h"
#include <cstdint>
#include <functional>
#include <string>

namespace uisys {

//  Button mode 

enum class ButtonMode {
    TRIGGER,       // fires once on press
    HOLD,          // fires on hold start, fires again on release
    TOGGLE,        // alternates on/off each press
    TRIGGER_HOLD,  // fires on press AND separately on hold
};

//  Callback state values 
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

//  Theme 

struct ButtonTheme {
    uint32_t colNormal   = 0xFF212021u;  // was 0x2104  →  rgb(33,32,33)
    uint32_t colPressed  = 0xFF00FF00u;  // was 0x07E0  →  rgb(0,255,0)
    uint32_t colToggleOn = 0xFF0000FFu;  // was 0x001F  →  rgb(0,0,255)
    uint32_t colHolding  = 0xFFFFA600u;  // was 0xFD20  →  rgb(255,166,0)
    uint32_t colBorder   = 0xFFFFFFFFu;  // was 0xFFFF  →  rgb(255,255,255)
    uint32_t colText     = 0xFFFFFFFFu;  // was 0xFFFF  →  rgb(255,255,255)

    static ButtonTheme Default();
    static ButtonTheme Danger();
    static ButtonTheme Military();
    static ButtonTheme HUD();
};

//  ButtonWidget 

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

    bool hitTest(int tx, int ty) const;
    void fire(int s);

public:
    ButtonWidget();
    ButtonWidget(int x, int y, int w, int h,
                 const std::string& label, ButtonMode mode,
                 Callback cb = nullptr,
                 ButtonTheme theme = ButtonTheme::Default(),
                 Font font = Font::Medium());

    //  Setters 

    void setTheme(const ButtonTheme& t);
    void setFont(const Font& f);
    void setCallback(Callback cb);
    void setLabel(const std::string& l);
    void setToggleState(bool s);
    void setPressed(bool s) { pressed = s; };
    void setBounds(int nx, int ny, int nw, int nh);
    void setVisible(bool v);

    //  Getters 

    bool       isVisible()    const;
    bool       isPressed()    const;
    bool       isToggleOn()   const;
    bool       isHoldActive() const;
    ButtonMode getMode()      const;

    //  Event handler 

    void handleEvent(const TouchEventData& e);

    //  Draw — templated, must remain in header 

    template<typename GFX>
    void draw(GFX& gfx) const {
        if (!visible_widget) return;

        // Background color by state
        uint32_t bg;
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
        uint32_t dotCol = GFX_BLACK;
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
                dotCol = holdActive
                    ? gfx.color565(0xFF, 0x80, 0x00)        // orange when holding
                    : gfx.color565(0xFF, 0xC0, 0x00);       // gold = both modes
                break;
        }
        gfx.fillCircle(x+w-8, y+8, 4, dotCol);
    }
};

} // namespace uisys