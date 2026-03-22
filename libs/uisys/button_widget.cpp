#include "button_widget.h"

namespace uisys {

//  ButtonTheme factories 

ButtonTheme ButtonTheme::Default() { return {}; }

ButtonTheme ButtonTheme::Danger() {
    ButtonTheme t;
    t.colNormal   = 0xFF420000u;  // was 0x4000  →  rgb(66,0,0)   dark red
    t.colPressed  = 0xFFFF0000u;  // was 0xF800  →  rgb(255,0,0)  bright red
    t.colToggleOn = 0xFFFF0000u;  // was 0xF800  →  rgb(255,0,0)
    t.colHolding  = 0xFFFF7D00u;  // was 0xFBE0  →  rgb(255,125,0) orange-red
    return t;
}

ButtonTheme ButtonTheme::Military() {
    ButtonTheme t;
    t.colNormal   = 0xFF1A2E00u;  // dark olive — slightly brighter for visibility
    t.colPressed  = 0xFF4A8200u;  // mid green
    t.colToggleOn = 0xFF2E6B00u;  // toggled on — clearly green
    t.colHolding  = 0xFF636100u;  // olive amber
    t.colBorder   = 0xFF6B8C6Bu;  // visible olive-grey border
    t.colText     = 0xFFE0FFE0u;  // light green-tinted white — readable on dark
    return t;
}

ButtonTheme ButtonTheme::HUD() {
    ButtonTheme t;
    t.colNormal   = 0xFF002A2Bu;  // dark teal — slightly brighter
    t.colPressed  = 0xFF00FF00u;  // bright green
    t.colToggleOn = 0xFF007A00u;  // mid green when on
    t.colHolding  = 0xFFFFA600u;  // amber
    t.colBorder   = 0xFF00C800u;  // bright green border — visible
    t.colText     = 0xFF00FF00u;  // green text
    return t;
}

//  ButtonWidget 

ButtonWidget::ButtonWidget()
    : x(0), y(0), w(0), h(0), mode(ButtonMode::TRIGGER) {}

ButtonWidget::ButtonWidget(int x, int y, int w, int h,
                           const std::string& label, ButtonMode mode,
                           Callback cb, ButtonTheme theme, Font font)
    : x(x), y(y), w(w), h(h), label(label), mode(mode),
      onAction(cb), theme(theme), font(font) {}

bool ButtonWidget::hitTest(int tx, int ty) const {
    return tx >= x && tx <= x+w && ty >= y && ty <= y+h;
}

void ButtonWidget::fire(int s) { if (onAction) onAction(s); }

//  Setters 

void ButtonWidget::setTheme(const ButtonTheme& t)         { theme = t; }
void ButtonWidget::setFont(const Font& f)                 { font  = f; }
void ButtonWidget::setCallback(Callback cb)               { onAction = cb; }
void ButtonWidget::setLabel(const std::string& l)         { label = l; }
void ButtonWidget::setToggleState(bool s)                 { toggleState = s; }
void ButtonWidget::setBounds(int nx, int ny, int nw, int nh) { x=nx; y=ny; w=nw; h=nh; }
void ButtonWidget::setVisible(bool v)                     { visible_widget = v; }

//  Getters 

bool       ButtonWidget::isVisible()    const { return visible_widget; }
bool       ButtonWidget::isPressed()    const { return pressed; }
bool       ButtonWidget::isToggleOn()   const { return toggleState; }
bool       ButtonWidget::isHoldActive() const { return holdActive; }
ButtonMode ButtonWidget::getMode()      const { return mode; }

//  Event handler 

void ButtonWidget::handleEvent(const TouchEventData& e) {
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

} // namespace uisys