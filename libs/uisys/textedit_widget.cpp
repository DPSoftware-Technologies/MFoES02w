#include "textedit_widget.h"

namespace uisys {

TextEditWidget::TextEditWidget() : x(0), y(0), w(200), h(40) {}

TextEditWidget::TextEditWidget(int x, int y, int w, int h,
                               const std::string& placeholder,
                               KeyboardWidget* keyboard,
                               bool numericOnly, Font font)
    : x(x), y(y), w(w), h(h),
      placeholder(placeholder), numericOnly(numericOnly),
      font(font), keyboard(keyboard) {}

//  Private helpers 

bool TextEditWidget::hitTest(int tx, int ty) const {
    return tx >= x && tx <= x+w && ty >= y && ty <= y+h;
}

//  Public interface 

void TextEditWidget::setVisible(bool v) { visible_widget = v; }
bool TextEditWidget::isVisible()  const { return visible_widget; }

void TextEditWidget::setFont(const Font& f)          { font = f; }
void TextEditWidget::setKeyboard(KeyboardWidget* kb) { keyboard = kb; }
void TextEditWidget::setMaxLen(size_t len)           { maxLen = len; }
void TextEditWidget::setNumericOnly(bool n)          { numericOnly = n; }
void TextEditWidget::setPlaceholder(const std::string& p) { placeholder = p; }
void TextEditWidget::setText(const std::string& t)   { text = t.substr(0, maxLen); }
void TextEditWidget::setCallback(Callback cb)        { onChange = cb; }
void TextEditWidget::setOnConfirm(Callback cb)       { onConfirm = cb; }

void TextEditWidget::setColors(uint32_t bg, uint32_t bgFocus, uint32_t border,
                                uint32_t borderFocus, uint32_t text,
                                uint32_t placeholder) {
    colBg=bg; colBgFocused=bgFocus; colBorder=border;
    colBorderFocus=borderFocus; colText=text; colPlaceholder=placeholder;
}

const std::string& TextEditWidget::getText()   const { return text; }
bool               TextEditWidget::isFocused() const { return focused; }

void TextEditWidget::focus() {
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

void TextEditWidget::unfocus() {
    if (!focused) return;  // guard re-entry
    focused = false;
    if (keyboard && keyboard->isVisible()) {
        keyboard->setCallback(nullptr);  // clear first to prevent re-entry
        keyboard->hide();
    }
    if (onConfirm) onConfirm(text);
}

void TextEditWidget::handleEvent(const TouchEventData& e) {
    if (!visible_widget) return;
    if (e.event == TouchEvent::PRESS) {
        if (hitTest(e.point.x, e.point.y)) {
            if (!focused) focus();
        } else {
            if (focused) unfocus();
        }
    }
}

} // namespace uisys
