#include "spinbox_widget.h"

namespace uisys {

SpinBoxWidget::SpinBoxWidget()
    : x(0), y(0), w(200), h(50), type(SpinBoxType::INT) {}

SpinBoxWidget::SpinBoxWidget(int x, int y, int w, int h,
                             SpinBoxType type,
                             float minVal, float maxVal, float step,
                             float defaultVal,
                             KeyboardWidget* keyboard,
                             bool showButtons, Font font)
    : x(x), y(y), w(w), h(h), type(type),
      value(defaultVal), minVal(minVal), maxVal(maxVal), step(step),
      showButtons(showButtons), font(font), keyboard(keyboard)
{ clamp(); }

//  Private helpers 

int SpinBoxWidget::valueX()    const { return showButtons ? x + BTN_W : x; }
int SpinBoxWidget::valueW()    const { return showButtons ? w - BTN_W*2 : w; }
int SpinBoxWidget::minusBtnX() const { return x; }
int SpinBoxWidget::plusBtnX()  const { return x + w - BTN_W; }

bool SpinBoxWidget::hitValueBox(int tx, int ty) const {
    return tx >= valueX() && tx <= valueX()+valueW() &&
           ty >= y && ty <= y+h;
}
bool SpinBoxWidget::hitPlus(int tx, int ty) const {
    if (!showButtons) return false;
    return tx >= plusBtnX() && tx <= plusBtnX()+BTN_W && ty >= y && ty <= y+h;
}
bool SpinBoxWidget::hitMinus(int tx, int ty) const {
    if (!showButtons) return false;
    return tx >= minusBtnX() && tx <= minusBtnX()+BTN_W && ty >= y && ty <= y+h;
}

void SpinBoxWidget::clamp() {
    if (value < minVal) value = minVal;
    if (value > maxVal) value = maxVal;
    if (type == SpinBoxType::INT) value = std::round(value);
}

void SpinBoxWidget::increment() { value += step; clamp(); if (onChange) onChange(value); }
void SpinBoxWidget::decrement() { value -= step; clamp(); if (onChange) onChange(value); }

std::string SpinBoxWidget::formatValue() const {
    char buf[32];
    if (type == SpinBoxType::INT)
        snprintf(buf, sizeof(buf), "%d", (int)value);
    else
        snprintf(buf, sizeof(buf), "%.*f", decimals, value);
    return std::string(buf);
}

void SpinBoxWidget::commitEdit() {
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
    editing = false;
    editBuf.clear();
    if (keyboard) {
        keyboard->setCallback(nullptr);  // clear before hide to prevent re-entry
        if (keyboard->isVisible()) keyboard->hide();
    }
}

//  Public interface 

void SpinBoxWidget::setVisible(bool v) { visible_widget = v; }
bool SpinBoxWidget::isVisible()  const { return visible_widget; }

void SpinBoxWidget::setFont(const Font& f)          { font = f; }
void SpinBoxWidget::setKeyboard(KeyboardWidget* kb) { keyboard = kb; }
void SpinBoxWidget::setShowButtons(bool s)           { showButtons = s; }
void SpinBoxWidget::setDecimals(int d)               { decimals = d; }
void SpinBoxWidget::setCallback(Callback cb)         { onChange = cb; }
void SpinBoxWidget::setValue(float v)                { value = v; clamp(); }
void SpinBoxWidget::setStep(float s)                 { step = s; }
void SpinBoxWidget::setRange(float mn, float mx)     { minVal = mn; maxVal = mx; clamp(); }

float SpinBoxWidget::getValue()    const { return value; }
int   SpinBoxWidget::getIntValue() const { return (int)std::round(value); }
bool  SpinBoxWidget::isEditing()   const { return editing; }

void SpinBoxWidget::setColors(uint32_t bg, uint32_t bgEdit, uint32_t plus,
                               uint32_t minus, uint32_t border, uint32_t text) {
    colBg=bg; colBgEdit=bgEdit; colBtnPlus=plus;
    colBtnMinus=minus; colBorder=border; colText=text;
}

void SpinBoxWidget::handleEvent(const TouchEventData& e) {
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
                if (keyboard && !editing) {
                    editing = true;
                    editBuf = formatValue();
                    keyboard->setCallback([this](const std::string& t) {
                        editBuf = t;
                    });
                    keyboard->setMaxLen(16);
                    keyboard->showWithText(editBuf, true);
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
            plusPressed  = false;
            minusPressed = false;
            break;

        default: break;
    }
}

} // namespace uisys
