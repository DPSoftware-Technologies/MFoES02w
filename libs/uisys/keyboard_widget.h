#pragma once
#include "hwinterface/gt911.h"
#include "font.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <cstring>

namespace uisys {

class KeyboardWidget {
public:
    using Callback = std::function<void(const std::string& text)>;

private:
    int  x, y, w, h;
    bool visible     = false;
    bool shiftActive = false;
    bool capsLock    = false;
    bool numericMode = false;

    std::string internalBuf;
    size_t      maxLen   = 64;
    Callback    onChange;
    Font        font;

    uint32_t colKey      = 0xFF292C29u;  // was 0x2965  →  rgb(41,44,41)
    uint32_t colKeyPress = 0xFF00FF00u;  // was 0x07E0  →  rgb(0,255,0)
    uint32_t colSpecial  = 0xFF181818u;  // was 0x18C3  →  rgb(24,24,24)
    uint32_t colBorder   = 0xFF8C8E8Cu;  // was 0x8C71  →  rgb(140,142,140)
    uint32_t colBg       = 0xFF101010u;  // was 0x1082  →  rgb(16,16,16)
    uint32_t colText     = 0xFFFFFFFFu;  // was 0xFFFF  →  rgb(255,255,255)

    int pressedKeyIdx = -1;

    struct Key {
        std::string label;
        std::string shifted;
        std::string action;
        int x, y, w, h;
        bool isSpecial = false;
    };
    std::vector<Key> keys;

    void buildQWERTY();
    void buildNumpad();
    void rebuildLayout();

    bool hitTestArea(int tx, int ty) const;
    int  hitTestKey(int tx, int ty)  const;
    void fireChange();
    void processKey(const Key& k);

public:
    KeyboardWidget();
    KeyboardWidget(int x, int y, int w, int h, Font font = Font::Medium());

    void setFont(const Font& f);
    void setCallback(Callback cb);
    void setMaxLen(size_t len);
    void setColors(uint32_t key, uint32_t keyPress, uint32_t special,
                   uint32_t border, uint32_t bg, uint32_t text);

    void showWithText(const std::string& initialText, bool numericOnly = false);

    // Returns true if keyboard consumed the event
    bool handleEvent(const TouchEventData& e);

    void hide();

    bool               isVisible() const;
    const std::string& getText()   const;

    //  Draw — templated, must remain in header 

    template<typename GFX>
    void draw(GFX& gfx) const {
        if (!visible) return;

        gfx.fillRect(x, y, w, h, colBg);
        gfx.drawRect(x, y, w, h, colBorder);

        font.apply(gfx);

        for (int i = 0; i < (int)keys.size(); i++) {
            const Key& k = keys[i];
            bool isPressed = (i == pressedKeyIdx);

            uint32_t bg;
            if (isPressed) bg = colKeyPress;
            else if (k.isSpecial) bg = colSpecial;
            else bg = colKey;

            if ((k.action=="SHIFT" && shiftActive) ||
                (k.action=="CAPS"  && capsLock)    ||
                (k.action=="NUM"   && numericMode))
                bg = colKeyPress;

            gfx.fillRect(k.x, k.y, k.w, k.h, bg);
            gfx.drawRect(k.x, k.y, k.w, k.h, colBorder);

            bool upper = shiftActive ^ capsLock;
            std::string lbl = (!k.action.empty()) ? k.label
                : (upper && !k.shifted.empty()) ? k.shifted : k.label;

            int tx = k.x + (k.w - font.textWidth((int)lbl.size())) / 2;
            int ty = k.y + (k.h - font.charH()) / 2;
            gfx.setCursor(tx, ty);
            gfx.setTextColor(colText, bg);
            gfx.writeText(lbl.c_str());
        }
    }
};

} // namespace uisys
