#pragma once
#include "hwinterface/gt911.h"
#include "font.h"
#include "keyboard_widget.h"
#include <cstdint>
#include <functional>
#include <string>
#include <cstdio>
#include <cmath>
#include <stdexcept>

namespace uisys {

enum class SpinBoxType { INT, FLOAT };

class SpinBoxWidget {
public:
    using Callback = std::function<void(float value)>;

private:
    int x, y, w, h;
    SpinBoxType type;

    float value    = 0.0f;
    float minVal   = 0.0f;
    float maxVal   = 100.0f;
    float step     = 1.0f;
    int   decimals = 2;       // decimal places for FLOAT display

    bool showButtons = true;  // show +/- buttons

    Callback onChange;
    Font     font;

    KeyboardWidget* keyboard = nullptr;
    std::string     editBuf;
    bool            editing  = false;

    uint16_t colBg       = 0x0861;
    uint16_t colBgEdit   = 0x18C3;
    uint16_t colBtnPlus  = 0x0320;  // dark green
    uint16_t colBtnMinus = 0x4000;  // dark red
    uint16_t colBtnPress = 0x07E0;
    uint16_t colBorder   = 0x8C71;
    uint16_t colText     = 0xFFFF;

    bool plusPressed  = false;
    bool minusPressed = false;

    static constexpr int BTN_W = 40;

    // Layout helpers
    int valueX() const { return showButtons ? x + BTN_W : x; }
    int valueW() const { return showButtons ? w - BTN_W*2 : w; }

    int minusBtnX() const { return x; }
    int plusBtnX()  const { return x + w - BTN_W; }

    bool hitValueBox(int tx, int ty) const {
        return tx >= valueX() && tx <= valueX()+valueW() &&
               ty >= y && ty <= y+h;
    }
    bool hitPlus(int tx, int ty) const {
        if (!showButtons) return false;
        return tx >= plusBtnX() && tx <= plusBtnX()+BTN_W &&
               ty >= y && ty <= y+h;
    }
    bool hitMinus(int tx, int ty) const {
        if (!showButtons) return false;
        return tx >= minusBtnX() && tx <= minusBtnX()+BTN_W &&
               ty >= y && ty <= y+h;
    }

    void clamp() {
        if (value < minVal) value = minVal;
        if (value > maxVal) value = maxVal;
        if (type == SpinBoxType::INT) value = std::round(value);
    }

    void increment() { value += step; clamp(); if (onChange) onChange(value); }
    void decrement() { value -= step; clamp(); if (onChange) onChange(value); }

    std::string formatValue() const {
        char buf[32];
        if (type == SpinBoxType::INT)
            snprintf(buf, sizeof(buf), "%d", (int)value);
        else
            snprintf(buf, sizeof(buf), "%.*f", decimals, value);
        return std::string(buf);
    }

    void commitEdit() {
        if (!editBuf.empty()) {
            try {
                float v = std::stof(editBuf);
                value = v;
                clamp();
                if (onChange) onChange(value);
            } catch (...) {
                // bad input — keep previous value silently
            }
        }
        // else: empty input = keep current value
        editing = false;
        editBuf.clear();
        if (keyboard) {
            keyboard->setCallback(nullptr);  // clear before hide to prevent re-entry
            if (keyboard->isVisible()) keyboard->hide();
        }
    }

bool visible_widget = true;

public:
    SpinBoxWidget() : x(0), y(0), w(200), h(50), type(SpinBoxType::INT) {}

    SpinBoxWidget(int x, int y, int w, int h,
                  SpinBoxType type,
                  float minVal, float maxVal, float step = 1.0f,
                  float defaultVal = 0.0f,
                  KeyboardWidget* keyboard = nullptr,
                  bool showButtons = true,
                  Font font = Font::Medium())
        : x(x), y(y), w(w), h(h), type(type),
          value(defaultVal), minVal(minVal), maxVal(maxVal), step(step),
          showButtons(showButtons), font(font), keyboard(keyboard)
    { clamp(); }

        void setVisible(bool v) { visible_widget = v; }
    bool isVisible()   const { return visible_widget; }

    void setFont(const Font& f)          { font = f; }
    void setKeyboard(KeyboardWidget* kb)  { keyboard = kb; }
    void setShowButtons(bool s)           { showButtons = s; }
    void setDecimals(int d)               { decimals = d; }
    void setCallback(Callback cb)         { onChange = cb; }
    void setValue(float v)                { value = v; clamp(); }
    void setStep(float s)                 { step = s; }
    void setRange(float mn, float mx)     { minVal = mn; maxVal = mx; clamp(); }

    float getValue()    const { return value; }
    int   getIntValue() const { return (int)std::round(value); }
    bool  isEditing()   const { return editing; }

    void setColors(uint16_t bg, uint16_t bgEdit, uint16_t plus,
                   uint16_t minus, uint16_t border, uint16_t text) {
        colBg=bg; colBgEdit=bgEdit; colBtnPlus=plus;
        colBtnMinus=minus; colBorder=border; colText=text;
    }

    void handleEvent(const TouchEventData& e) {
        if (!visible_widget) return;
        switch (e.event) {

            case TouchEvent::PRESS:
                if (hitPlus(e.point.x, e.point.y)) {
                    plusPressed = true;
                    if (!editing) increment();
                } else if (hitMinus(e.point.x, e.point.y)) {
                    minusPressed = true;
                    if (!editing) decrement();
                } else if (hitValueBox(e.point.x, e.point.y)) {
                    // Open keyboard for direct input
                    if (keyboard && !editing) {
                        editing = true;
                        editBuf = formatValue();
                        keyboard->setCallback([this](const std::string& t) {
                            editBuf = t;  // keyboard owns buffer, we copy on each keystroke
                        });
                        keyboard->setMaxLen(16);
                        keyboard->showWithText(editBuf, true); // safe copy — no raw pointer
                    }
                } else if (editing) {
                    commitEdit();
                }
                break;

            case TouchEvent::HOLD:
                // Hold +/- for fast repeat
                if (plusPressed  && hitPlus(e.point.x,  e.point.y)) increment();
                if (minusPressed && hitMinus(e.point.x, e.point.y)) decrement();
                break;

            case TouchEvent::RELEASE:
                if (plusPressed || minusPressed) {
                    plusPressed  = false;
                    minusPressed = false;
                }
                break;

            default: break;
        }
    }

    template<typename GFX>
    void draw(GFX& gfx) const {
        if (!visible_widget) return;
        // ── Value box ─────────────────────────────────────────────────────────
        uint16_t vbg = editing ? colBgEdit : colBg;
        gfx.fillRect(valueX(), y, valueW(), h, vbg);
        gfx.drawRect(valueX(), y, valueW(), h, colBorder);
        if (editing) gfx.drawRect(valueX()+1, y+1, valueW()-2, h-2, colBorder);

        font.apply(gfx);
        std::string display = editing ? editBuf : formatValue();
        int tx = valueX() + (valueW() - font.textWidth((int)display.size())) / 2;
        int ty = y + (h - font.charH()) / 2;
        gfx.setCursor(tx, ty);
        gfx.setTextColor(colText, vbg);
        gfx.writeText(display.c_str());

        if (!showButtons) return;

        // ── Minus button ──────────────────────────────────────────────────────
        uint16_t mbg = minusPressed ? colBtnPress : colBtnMinus;
        gfx.fillRect(minusBtnX(), y, BTN_W, h, mbg);
        gfx.drawRect(minusBtnX(), y, BTN_W, h, colBorder);
        // Minus symbol
        int my = y + h/2;
        int mx = minusBtnX() + BTN_W/2;
        gfx.drawLine(mx-8, my, mx+8, my, colText);
        gfx.drawLine(mx-8, my+1, mx+8, my+1, colText);

        // ── Plus button ───────────────────────────────────────────────────────
        uint16_t pbg = plusPressed ? colBtnPress : colBtnPlus;
        gfx.fillRect(plusBtnX(), y, BTN_W, h, pbg);
        gfx.drawRect(plusBtnX(), y, BTN_W, h, colBorder);
        // Plus symbol
        int py = y + h/2;
        int px = plusBtnX() + BTN_W/2;
        gfx.drawLine(px-8, py,   px+8, py,   colText);
        gfx.drawLine(px-8, py+1, px+8, py+1, colText);
        gfx.drawLine(px,   py-8, px,   py+8, colText);
        gfx.drawLine(px+1, py-8, px+1, py+8, colText);

        // ── Range indicators ──────────────────────────────────────────────────
        // Dim minus when at min, dim plus when at max
        if (value <= minVal)
            gfx.fillRect(minusBtnX(), y, BTN_W, h, (uint16_t)(colBtnMinus >> 1 & 0x7BEF));
        if (value >= maxVal)
            gfx.fillRect(plusBtnX(), y, BTN_W, h, (uint16_t)(colBtnPlus >> 1 & 0x7BEF));
    }
};

} // namespace uisys
