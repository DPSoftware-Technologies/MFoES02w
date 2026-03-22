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
    int   decimals = 2;

    bool showButtons = true;

    Callback onChange;
    Font     font;

    KeyboardWidget* keyboard = nullptr;
    std::string     editBuf;
    bool            editing  = false;

    uint32_t colBg       = 0xFF080C08u;  // was 0x0861  →  rgb(8,12,8)
    uint32_t colBgEdit   = 0xFF181818u;  // was 0x18C3  →  rgb(24,24,24)
    uint32_t colBtnPlus  = 0xFF006500u;  // was 0x0320  →  rgb(0,101,0)
    uint32_t colBtnMinus = 0xFF420000u;  // was 0x4000  →  rgb(66,0,0)
    uint32_t colBtnPress = 0xFF00FF00u;  // was 0x07E0  →  rgb(0,255,0)
    uint32_t colBorder   = 0xFF8C8E8Cu;  // was 0x8C71  →  rgb(140,142,140)
    uint32_t colText     = 0xFFFFFFFFu;  // was 0xFFFF  →  rgb(255,255,255)

    bool plusPressed  = false;
    bool minusPressed = false;

    static constexpr int BTN_W = 40;

    int  valueX()    const;
    int  valueW()    const;
    int  minusBtnX() const;
    int  plusBtnX()  const;

    bool hitValueBox(int tx, int ty) const;
    bool hitPlus(int tx, int ty)     const;
    bool hitMinus(int tx, int ty)    const;

    void clamp();
    void increment();
    void decrement();
    std::string formatValue() const;
    void commitEdit();

    bool visible_widget = true;

public:
    SpinBoxWidget();
    SpinBoxWidget(int x, int y, int w, int h,
                  SpinBoxType type,
                  float minVal, float maxVal, float step = 1.0f,
                  float defaultVal = 0.0f,
                  KeyboardWidget* keyboard = nullptr,
                  bool showButtons = true,
                  Font font = Font::Medium());

    void setVisible(bool v);
    bool isVisible() const;

    void setFont(const Font& f);
    void setKeyboard(KeyboardWidget* kb);
    void setShowButtons(bool s);
    void setDecimals(int d);
    void setCallback(Callback cb);
    void setValue(float v);
    void setStep(float s);
    void setRange(float mn, float mx);

    float getValue()    const;
    int   getIntValue() const;
    bool  isEditing()   const;

    void setColors(uint32_t bg, uint32_t bgEdit, uint32_t plus,
                   uint32_t minus, uint32_t border, uint32_t text);

    void handleEvent(const TouchEventData& e);

    //  Draw — templated, must remain in header 

    template<typename GFX>
    void draw(GFX& gfx) const {
        if (!visible_widget) return;

        //  Value box 
        uint32_t vbg = editing ? colBgEdit : colBg;
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

        //  Minus button 
        uint32_t mbg = minusPressed ? colBtnPress : colBtnMinus;
        gfx.fillRect(minusBtnX(), y, BTN_W, h, mbg);
        gfx.drawRect(minusBtnX(), y, BTN_W, h, colBorder);
        int my = y + h/2;
        int mx = minusBtnX() + BTN_W/2;
        gfx.drawLine(mx-8, my,   mx+8, my,   colText);
        gfx.drawLine(mx-8, my+1, mx+8, my+1, colText);

        //  Plus button 
        uint32_t pbg = plusPressed ? colBtnPress : colBtnPlus;
        gfx.fillRect(plusBtnX(), y, BTN_W, h, pbg);
        gfx.drawRect(plusBtnX(), y, BTN_W, h, colBorder);
        int py = y + h/2;
        int px = plusBtnX() + BTN_W/2;
        gfx.drawLine(px-8, py,   px+8, py,   colText);
        gfx.drawLine(px-8, py+1, px+8, py+1, colText);
        gfx.drawLine(px,   py-8, px,   py+8, colText);
        gfx.drawLine(px+1, py-8, px+1, py+8, colText);

        //  Range indicators (dim buttons at limits) 
        // Darken by blending 50% black over the button color
        if (value <= minVal)
            gfx.fillRect(minusBtnX(), y, BTN_W, h, (colBtnMinus & 0x00FEFEFEu) >> 1 | 0xFF000000u);
        if (value >= maxVal)
            gfx.fillRect(plusBtnX(),  y, BTN_W, h, (colBtnPlus  & 0x00FEFEFEu) >> 1 | 0xFF000000u);
    }
};

} // namespace uisys
