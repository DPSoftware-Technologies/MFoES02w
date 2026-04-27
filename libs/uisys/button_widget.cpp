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
    t.colNormal   = 0xFF1A2E00u;
    t.colPressed  = 0xFF4A8200u;
    t.colToggleOn = 0xFF2E6B00u;
    t.colHolding  = 0xFF636100u;
    t.colBorder   = 0xFF6B8C6Bu;
    t.colText     = 0xFFE0FFE0u;
    t.colSwipeFill  = 0xFF4A8200u;  // olive green
    t.colSwipeTrack = 0xFF0D1600u;  // near-black olive
    return t;
}

ButtonTheme ButtonTheme::HUD() {
    ButtonTheme t;
    t.colNormal   = 0xFF002A2Bu;
    t.colPressed  = 0xFF00FF00u;
    t.colToggleOn = 0xFF007A00u;
    t.colHolding  = 0xFFFFA600u;
    t.colBorder   = 0xFF00C800u;
    t.colText     = 0xFF00FF00u;
    t.colSwipeFill  = 0xFF00FF00u;  // bright green
    t.colSwipeTrack = 0xFF001400u;  // very dark green
    return t;
}

//  ButtonWidget 

ButtonWidget::ButtonWidget()
    : x(0), y(0), w(0), h(0), mode(ButtonMode::TRIGGER), 
      _swipeDurationMs(1000), _swipePressedAt(0), _swipeProgress(0.f), 
      _swipeFired(false) {}

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
                fire(ButtonState::TRIGGER);

            } else if (mode == ButtonMode::TOGGLE) {
                toggleState = !toggleState;
                fire(toggleState ? ButtonState::TOGGLE_ON : ButtonState::TOGGLE_OFF);

            } else if (mode == ButtonMode::HOLD_SWIPE) {
                _swipePressedAt = 0;   // seeded on first update() call
                _swipeProgress  = 0.f;
                _swipeFired     = false;
            }
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
                if ((mode == ButtonMode::HOLD || mode == ButtonMode::TRIGGER_HOLD)
                    && holdActive) {
                    holdActive = false;
                    fire(ButtonState::HOLD_CANCEL);
                }
                if (mode == ButtonMode::HOLD_SWIPE && !_swipeFired) {
                    _swipeProgress = 0.f;
                    fire(ButtonState::SWIPE_CANCEL);
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
            if (mode == ButtonMode::HOLD_SWIPE && !_swipeFired) {
                _swipeProgress = 0.f;
                fire(ButtonState::SWIPE_CANCEL);
            }
            break;
    }
}

//  HOLD_SWIPE update — call every frame with millis()

void ButtonWidget::update(uint32_t nowMs) {
    if (mode != ButtonMode::HOLD_SWIPE) return;
    if (!pressed || _swipeFired)        return;
    if (_swipeDurationMs == 0)          return;  // Safety: prevent division by zero

    if (_swipePressedAt == 0)
        _swipePressedAt = nowMs;  // latch on first frame after press

    uint32_t elapsed = nowMs - _swipePressedAt;
    _swipeProgress = (float)elapsed / (float)_swipeDurationMs;
    if (_swipeProgress > 1.f) _swipeProgress = 1.f;

    if (_swipeProgress >= 1.f) {
        _swipeFired = true;
        fire(ButtonState::SWIPE_DONE);
    }
}

} // namespace uisys