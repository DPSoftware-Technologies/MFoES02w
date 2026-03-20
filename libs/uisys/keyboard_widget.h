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

    uint16_t colKey      = 0x2965;
    uint16_t colKeyPress = 0x07E0;
    uint16_t colSpecial  = 0x18C3;
    uint16_t colBorder   = 0x8C71;
    uint16_t colBg       = 0x1082;
    uint16_t colText     = 0xFFFF;

    int pressedKeyIdx = -1;

    struct Key {
        std::string label;
        std::string shifted;
        std::string action;
        int x, y, w, h;
        bool isSpecial = false;
    };
    std::vector<Key> keys;

    // ── Layout builders ───────────────────────────────────────────────────────

    void buildQWERTY() {
        keys.clear();
        int kw = w / 11;
        int kh = h / 5;
        int pad = 2;

        // Row 0: numbers
        const char* nums    = "1234567890";
        const char* numssh  = "!@#$%^&*()";
        for (int i = 0; i < 10; i++) {
            Key k;
            k.label   = std::string(1, nums[i]);
            k.shifted = std::string(1, numssh[i]);
            k.x=x+i*kw+pad; k.y=y+0*kh+pad; k.w=kw-pad*2; k.h=kh-pad*2;
            keys.push_back(k);
        }
        keys.push_back({"<","","BKSP",x+10*kw+pad,y+0*kh+pad,kw-pad*2,kh-pad*2,true});

        // Row 1: QWERTYUIOP
        const char* r1u="QWERTYUIOP", *r1l="qwertyuiop";
        int off1=kw/4;
        for (int i=0;i<10;i++) {
            Key k;
            k.label=std::string(1,r1l[i]); k.shifted=std::string(1,r1u[i]);
            k.x=x+off1+i*kw+pad; k.y=y+1*kh+pad; k.w=kw-pad*2; k.h=kh-pad*2;
            keys.push_back(k);
        }

        // Row 2: ASDFGHJKL + Enter
        const char* r2u="ASDFGHJKL", *r2l="asdfghjkl";
        int off2=kw/2;
        for (int i=0;i<9;i++) {
            Key k;
            k.label=std::string(1,r2l[i]); k.shifted=std::string(1,r2u[i]);
            k.x=x+off2+i*kw+pad; k.y=y+2*kh+pad; k.w=kw-pad*2; k.h=kh-pad*2;
            keys.push_back(k);
        }
        keys.push_back({"OK","","ENTER",x+off2+9*kw+pad,y+2*kh+pad,kw+kw/2-pad*2,kh-pad*2,true});

        // Row 3: Shift + ZXCVBNM + punctuation
        const char* r3u="ZXCVBNM", *r3l="zxcvbnm";
        int off3=kw;
        keys.push_back({"SH","","SHIFT",x+pad,y+3*kh+pad,kw-pad*2,kh-pad*2,true});
        for (int i=0;i<7;i++) {
            Key k;
            k.label=std::string(1,r3l[i]); k.shifted=std::string(1,r3u[i]);
            k.x=x+off3+i*kw+pad; k.y=y+3*kh+pad; k.w=kw-pad*2; k.h=kh-pad*2;
            keys.push_back(k);
        }
        keys.push_back({".",">","",x+off3+7*kw+pad,y+3*kh+pad,kw-pad*2,kh-pad*2,false});
        keys.push_back({"-","_","",x+off3+8*kw+pad,y+3*kh+pad,kw-pad*2,kh-pad*2,false});

        // Row 4: bottom bar
        keys.push_back({"123","","NUM",  x+pad,      y+4*kh+pad,kw*2-pad*2,  kh-pad*2,true});
        keys.push_back({" ","","SPACE",  x+2*kw+pad, y+4*kh+pad,kw*6-pad*2,  kh-pad*2,false});
        keys.push_back({"@","","",       x+8*kw+pad, y+4*kh+pad,kw-pad*2,    kh-pad*2,false});
        keys.push_back({"X","","CLOSE",  x+9*kw+pad, y+4*kh+pad,kw*2-pad*2,  kh-pad*2,true});
    }

    void buildNumpad() {
        keys.clear();
        int cols=4, rows=4;
        int kw=w/cols, kh=h/rows, pad=3;
        const char* labels[] = {"7","8","9","<","4","5","6","-","1","2","3",".","X","0","00","OK"};
        const char* actions[]= {"","","","BKSP","","","","","","","","","CLOSE","","","ENTER"};
        for (int r=0;r<rows;r++) for (int c=0;c<cols;c++) {
            int i=r*cols+c;
            Key k;
            k.label=labels[i]; k.action=actions[i];
            k.isSpecial=(k.action.size()>0);
            k.x=x+c*kw+pad; k.y=y+r*kh+pad; k.w=kw-pad*2; k.h=kh-pad*2;
            keys.push_back(k);
        }
    }

    void rebuildLayout() {
        if (numericMode) buildNumpad();
        else             buildQWERTY();
    }

    bool hitTestArea(int tx, int ty) const {
        return tx >= x && tx <= x+w && ty >= y && ty <= y+h;
    }

    int hitTestKey(int tx, int ty) const {
        for (int i=0;i<(int)keys.size();i++) {
            const Key& k=keys[i];
            if (tx>=k.x && tx<=k.x+k.w && ty>=k.y && ty<=k.y+k.h) return i;
        }
        return -1;
    }

    void fireChange() {
        if (onChange) onChange(internalBuf);
    }

    void processKey(const Key& k) {
        if (!k.action.empty()) {
            if (k.action == "BKSP") {
                if (!internalBuf.empty()) internalBuf.pop_back();
                fireChange();
            } else if (k.action == "ENTER") {
                // Capture callback, clear it, then fire once, then hide
                auto cb = onChange;
                onChange = nullptr;
                visible  = false;
                pressedKeyIdx = -1;
                if (cb) cb(internalBuf);
                return;
            } else if (k.action == "CLOSE") {
                onChange = nullptr;
                visible  = false;
                pressedKeyIdx = -1;
                return;
            } else if (k.action == "SHIFT") {
                shiftActive = !shiftActive;
            } else if (k.action == "CAPS") {
                capsLock = !capsLock;
            } else if (k.action == "NUM") {
                numericMode = !numericMode;
                rebuildLayout();
            } else if (k.action == "SPACE") {
                if (internalBuf.size() < maxLen) internalBuf.push_back(' ');
                fireChange();
            }
        } else {
            bool upper = shiftActive ^ capsLock;
            std::string ch = (upper && !k.shifted.empty()) ? k.shifted : k.label;
            if (internalBuf.size() < maxLen) internalBuf += ch;
            if (shiftActive) shiftActive = false;
            fireChange();
        }
    }

public:
    KeyboardWidget() : x(0), y(0), w(0), h(0) { buildQWERTY(); }

    KeyboardWidget(int x, int y, int w, int h, Font font = Font::Medium())
        : x(x), y(y), w(w), h(h), font(font) { buildQWERTY(); }

    void setFont(const Font& f)   { font = f; rebuildLayout(); }
    void setCallback(Callback cb) { onChange = cb; }
    void setMaxLen(size_t len)    { maxLen = len; }

    void setColors(uint16_t key, uint16_t keyPress, uint16_t special,
                   uint16_t border, uint16_t bg, uint16_t text) {
        colKey=key; colKeyPress=keyPress; colSpecial=special;
        colBorder=border; colBg=bg; colText=text;
    }

    void showWithText(const std::string& initialText, bool numericOnly = false) {
        internalBuf = initialText;
        numericMode = numericOnly;
        visible     = true;
        shiftActive = false;
        rebuildLayout();
    }

    // Returns true if keyboard consumed the event (caller should skip routing to other widgets)
    bool handleEvent(const TouchEventData& e) {
        if (!visible) return false;

        switch (e.event) {
            case TouchEvent::PRESS: {
                // Always consume events when visible — prevents bleed-through to widgets below
                int idx = hitTestKey(e.point.x, e.point.y);
                pressedKeyIdx = idx;
                if (idx >= 0) processKey(keys[idx]);
                return true;  // consumed
            }
            case TouchEvent::RELEASE:
                pressedKeyIdx = -1;
                return true;  // consumed
            case TouchEvent::MOVE:
                return true;  // consume moves too
            case TouchEvent::HOLD:
                return true;
        }
        return false;
    }

    void hide() {
        onChange      = nullptr;
        visible       = false;
        pressedKeyIdx = -1;
    }

    bool isVisible()              const { return visible; }
    const std::string& getText()  const { return internalBuf; }

    template<typename GFX>
    void draw(GFX& gfx) const {
        if (!visible) return;

        gfx.fillRect(x, y, w, h, colBg);
        gfx.drawRect(x, y, w, h, colBorder);

        font.apply(gfx);

        for (int i=0;i<(int)keys.size();i++) {
            const Key& k=keys[i];
            bool isPressed=(i==pressedKeyIdx);

            uint16_t bg;
            if (isPressed) bg=colKeyPress;
            else if (k.isSpecial) bg=colSpecial;
            else bg=colKey;

            if ((k.action=="SHIFT"&&shiftActive)||(k.action=="CAPS"&&capsLock)||(k.action=="NUM"&&numericMode))
                bg=colKeyPress;

            gfx.fillRect(k.x,k.y,k.w,k.h,bg);
            gfx.drawRect(k.x,k.y,k.w,k.h,colBorder);

            bool upper=shiftActive^capsLock;
            std::string lbl=(!k.action.empty())?k.label
                :(upper&&!k.shifted.empty())?k.shifted:k.label;

            int tx=k.x+(k.w-font.textWidth((int)lbl.size()))/2;
            int ty=k.y+(k.h-font.charH())/2;
            gfx.setCursor(tx,ty);
            gfx.setTextColor(colText,bg);
            gfx.writeText(lbl.c_str());
        }
    }
};

} // namespace uisys
