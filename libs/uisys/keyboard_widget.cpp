#include "keyboard_widget.h"

namespace uisys {

KeyboardWidget::KeyboardWidget() : x(0), y(0), w(0), h(0) { buildQWERTY(); }

KeyboardWidget::KeyboardWidget(int x, int y, int w, int h, Font font)
    : x(x), y(y), w(w), h(h), font(font) { buildQWERTY(); }

//  Layout builders 

void KeyboardWidget::buildQWERTY() {
    keys.clear();
    int kw  = w / 11;
    int kh  = h / 5;
    int pad = 2;

    // Row 0: numbers
    const char* nums   = "1234567890";
    const char* numssh = "!@#$%^&*()";
    for (int i = 0; i < 10; i++) {
        Key k;
        k.label   = std::string(1, nums[i]);
        k.shifted = std::string(1, numssh[i]);
        k.x=x+i*kw+pad; k.y=y+0*kh+pad; k.w=kw-pad*2; k.h=kh-pad*2;
        keys.push_back(k);
    }
    keys.push_back({"<","","BKSP",x+10*kw+pad,y+0*kh+pad,kw-pad*2,kh-pad*2,true});

    // Row 1: QWERTYUIOP
    const char* r1u = "QWERTYUIOP", *r1l = "qwertyuiop";
    int off1 = kw/4;
    for (int i = 0; i < 10; i++) {
        Key k;
        k.label   = std::string(1, r1l[i]);
        k.shifted = std::string(1, r1u[i]);
        k.x=x+off1+i*kw+pad; k.y=y+1*kh+pad; k.w=kw-pad*2; k.h=kh-pad*2;
        keys.push_back(k);
    }

    // Row 2: ASDFGHJKL + Enter
    const char* r2u = "ASDFGHJKL", *r2l = "asdfghjkl";
    int off2 = kw/2;
    for (int i = 0; i < 9; i++) {
        Key k;
        k.label   = std::string(1, r2l[i]);
        k.shifted = std::string(1, r2u[i]);
        k.x=x+off2+i*kw+pad; k.y=y+2*kh+pad; k.w=kw-pad*2; k.h=kh-pad*2;
        keys.push_back(k);
    }
    keys.push_back({"OK","","ENTER",x+off2+9*kw+pad,y+2*kh+pad,kw+kw/2-pad*2,kh-pad*2,true});

    // Row 3: Shift + ZXCVBNM + punctuation
    const char* r3u = "ZXCVBNM", *r3l = "zxcvbnm";
    int off3 = kw;
    keys.push_back({"SH","","SHIFT",x+pad,y+3*kh+pad,kw-pad*2,kh-pad*2,true});
    for (int i = 0; i < 7; i++) {
        Key k;
        k.label   = std::string(1, r3l[i]);
        k.shifted = std::string(1, r3u[i]);
        k.x=x+off3+i*kw+pad; k.y=y+3*kh+pad; k.w=kw-pad*2; k.h=kh-pad*2;
        keys.push_back(k);
    }
    keys.push_back({".",">","",x+off3+7*kw+pad,y+3*kh+pad,kw-pad*2,kh-pad*2,false});
    keys.push_back({"-","_","",x+off3+8*kw+pad,y+3*kh+pad,kw-pad*2,kh-pad*2,false});

    // Row 4: bottom bar
    keys.push_back({"123","","NUM",  x+pad,      y+4*kh+pad, kw*2-pad*2,  kh-pad*2, true});
    keys.push_back({" ","","SPACE",  x+2*kw+pad, y+4*kh+pad, kw*6-pad*2,  kh-pad*2, false});
    keys.push_back({"@","","",       x+8*kw+pad, y+4*kh+pad, kw-pad*2,    kh-pad*2, false});
    keys.push_back({"X","","CLOSE",  x+9*kw+pad, y+4*kh+pad, kw*2-pad*2,  kh-pad*2, true});
}

void KeyboardWidget::buildNumpad() {
    keys.clear();
    int cols=4, rows=4;
    int kw=w/cols, kh=h/rows, pad=3;
    const char* labels[]  = {"7","8","9","<","4","5","6","-","1","2","3",".","X","0","00","OK"};
    const char* actions[] = {"","","","BKSP","","","","","","","","","CLOSE","","","ENTER"};
    for (int r=0; r<rows; r++) for (int c=0; c<cols; c++) {
        int i = r*cols + c;
        Key k;
        k.label     = labels[i];
        k.action    = actions[i];
        k.isSpecial = (k.action.size() > 0);
        k.x=x+c*kw+pad; k.y=y+r*kh+pad; k.w=kw-pad*2; k.h=kh-pad*2;
        keys.push_back(k);
    }
}

void KeyboardWidget::rebuildLayout() {
    if (numericMode) buildNumpad();
    else             buildQWERTY();
}

//  Hit testing 

bool KeyboardWidget::hitTestArea(int tx, int ty) const {
    return tx >= x && tx <= x+w && ty >= y && ty <= y+h;
}

int KeyboardWidget::hitTestKey(int tx, int ty) const {
    for (int i = 0; i < (int)keys.size(); i++) {
        const Key& k = keys[i];
        if (tx >= k.x && tx <= k.x+k.w && ty >= k.y && ty <= k.y+k.h) return i;
    }
    return -1;
}

//  Internal logic 

void KeyboardWidget::fireChange() {
    if (onChange) onChange(internalBuf);
}

void KeyboardWidget::processKey(const Key& k) {
    if (!k.action.empty()) {
        if (k.action == "BKSP") {
            if (!internalBuf.empty()) internalBuf.pop_back();
            fireChange();
        } else if (k.action == "ENTER") {
            // Capture callback, clear it, then fire once, then hide
            auto cb = onChange;
            onChange      = nullptr;
            visible       = false;
            pressedKeyIdx = -1;
            if (cb) cb(internalBuf);
            return;
        } else if (k.action == "CLOSE") {
            onChange      = nullptr;
            visible       = false;
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

//  Public interface 

void KeyboardWidget::setFont(const Font& f)    { font = f; rebuildLayout(); }
void KeyboardWidget::setCallback(Callback cb)  { onChange = cb; }
void KeyboardWidget::setMaxLen(size_t len)     { maxLen = len; }

void KeyboardWidget::setColors(uint32_t key, uint32_t keyPress, uint32_t special,
                                uint32_t border, uint32_t bg, uint32_t text) {
    colKey=key; colKeyPress=keyPress; colSpecial=special;
    colBorder=border; colBg=bg; colText=text;
}

void KeyboardWidget::showWithText(const std::string& initialText, bool numericOnly) {
    internalBuf = initialText;
    numericMode = numericOnly;
    visible     = true;
    shiftActive = false;
    rebuildLayout();
}

bool KeyboardWidget::handleEvent(const TouchEventData& e) {
    if (!visible) return false;

    switch (e.event) {
        case TouchEvent::PRESS: {
            // Always consume events when visible — prevents bleed-through
            int idx = hitTestKey(e.point.x, e.point.y);
            pressedKeyIdx = idx;
            if (idx >= 0) processKey(keys[idx]);
            return true;
        }
        case TouchEvent::RELEASE:
            pressedKeyIdx = -1;
            return true;
        case TouchEvent::MOVE:
            return true;
        case TouchEvent::HOLD:
            return true;
    }
    return false;
}

void KeyboardWidget::hide() {
    onChange      = nullptr;
    visible       = false;
    pressedKeyIdx = -1;
}

bool               KeyboardWidget::isVisible() const { return visible; }
const std::string& KeyboardWidget::getText()   const { return internalBuf; }

} // namespace uisys
