#include "dial_widget.h"

namespace uisys {

DialWidget::DialWidget() : cx(0), cy(0), radius(50) {}

DialWidget::DialWidget(int cx, int cy, int radius, const std::string& label,
                       float minVal, float maxVal,
                       Callback cb, Font font)
    : cx(cx), cy(cy), radius(radius), minVal(minVal), maxVal(maxVal),
      label(label), onChange(cb), font(font) {}

//  Private helpers 

float DialWidget::degToRad(float d) const { return d * PI / 180.0f; }

float DialWidget::valueToAngle(float v) const { return START_ANGLE + v * SWEEP; }

float DialWidget::pointToValue(int tx, int ty) const {
    float dx  = (float)(tx - cx);
    float dy  = (float)(ty - cy);
    float ang = std::atan2(dy, dx) * 180.0f / PI;
    if (ang < 0) ang += 360.0f;
    float rel = ang - START_ANGLE;
    if (rel < 0) rel += 360.0f;
    if (rel > SWEEP) rel = (rel - SWEEP < 360.0f - rel) ? SWEEP : 0.0f;
    return rel / SWEEP;
}

bool DialWidget::hitTest(int tx, int ty) const {
    float dx = (float)(tx - cx), dy = (float)(ty - cy);
    return std::sqrt(dx*dx + dy*dy) <= (float)radius;
}

void  DialWidget::clampValue()   { if (value < 0.0f) value = 0.0f; if (value > 1.0f) value = 1.0f; }
float DialWidget::mappedValue() const { return minVal + value * (maxVal - minVal); }

//  Public interface 

void  DialWidget::setValue(float v) { value = (v - minVal) / (maxVal - minVal); clampValue(); }
float DialWidget::getValue()       const { return mappedValue(); }
float DialWidget::getNormalized()  const { return value; }

void DialWidget::setFont(const Font& f)    { font = f; }
void DialWidget::setVisible(bool v)        { visible_widget = v; }
bool DialWidget::isVisible()         const { return visible_widget; }
void DialWidget::setCallback(Callback cb)  { onChange = cb; }

void DialWidget::setColors(uint32_t bg, uint32_t arc, uint32_t knob, uint32_t text) {
    colBg=bg; colArc=arc; colKnob=knob; colText=text;
}

void DialWidget::handleEvent(const TouchEventData& e) {
    if (!visible_widget) return;
    switch (e.event) {
        case TouchEvent::PRESS:
            if (hitTest(e.point.x, e.point.y)) {
                dragging = true;
                value = pointToValue(e.point.x, e.point.y);
                clampValue();
                if (onChange) onChange(mappedValue());
            }
            break;
        case TouchEvent::MOVE:
            if (dragging) {
                value = pointToValue(e.point.x, e.point.y);
                clampValue();
                if (onChange) onChange(mappedValue());
            }
            break;
        case TouchEvent::RELEASE:
            dragging = false;
            break;
        default: break;
    }
}

} // namespace uisys
