#include "slider_widget.h"

namespace uisys {

SliderWidget::SliderWidget()
    : x(0), y(0), w(200), h(40), orientation(SliderOrientation::HORIZONTAL) {}

SliderWidget::SliderWidget(int x, int y, int w, int h,
                           SliderOrientation ori, const std::string& label,
                           float minVal, float maxVal,
                           Callback cb, Font font)
    : x(x), y(y), w(w), h(h), orientation(ori),
      minVal(minVal), maxVal(maxVal), label(label), onChange(cb), font(font) {}

//  Private helpers 

bool SliderWidget::trackHitTest(int tx, int ty) const {
    return tx >= x && tx <= x+w && ty >= y && ty <= y+h;
}

void SliderWidget::getThumbRect(int& ox, int& oy, int& ow, int& oh) const {
    if (orientation == SliderOrientation::HORIZONTAL) {
        ox = x + (int)(value * (w - THUMB_SIZE));
        oy = y + (h - THUMB_SIZE) / 2;
    } else {
        ox = x + (w - THUMB_SIZE) / 2;
        oy = y + (int)((1.0f - value) * (h - THUMB_SIZE));
    }
    ow = oh = THUMB_SIZE;
}

float SliderWidget::pointToValue(int tx, int ty) const {
    float v;
    if (orientation == SliderOrientation::HORIZONTAL)
        v = (float)(tx - x - THUMB_SIZE/2) / (float)(w - THUMB_SIZE);
    else
        v = 1.0f - (float)(ty - y - THUMB_SIZE/2) / (float)(h - THUMB_SIZE);
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

float SliderWidget::mappedValue() const { return minVal + value * (maxVal - minVal); }

//  Public interface 

void SliderWidget::setValue(float v) {
    value = (v - minVal) / (maxVal - minVal);
    value = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
}

float SliderWidget::getValue()      const { return mappedValue(); }
float SliderWidget::getNormalized() const { return value; }

void SliderWidget::setFont(const Font& f)    { font = f; }
void SliderWidget::setVisible(bool v)        { visible_widget = v; }
bool SliderWidget::isVisible()         const { return visible_widget; }
void SliderWidget::setCallback(Callback cb)  { onChange = cb; }

void SliderWidget::setColors(uint32_t track, uint32_t fill, uint32_t thumb,
                              uint32_t border, uint32_t text) {
    colTrack=track; colFill=fill; colThumb=thumb; colBorder=border; colText=text;
}

void SliderWidget::handleEvent(const TouchEventData& e) {
    if (!visible_widget) return;
    switch (e.event) {
        case TouchEvent::PRESS:
            if (trackHitTest(e.point.x, e.point.y)) {
                dragging = true;
                value = pointToValue(e.point.x, e.point.y);
                if (onChange) onChange(mappedValue());
            }
            break;
        case TouchEvent::MOVE:
        case TouchEvent::HOLD:
            // Keep updating even if finger moves outside track bounds.
            // Only RELEASE stops dragging — not leaving the track area.
            if (dragging) {
                value = pointToValue(e.point.x, e.point.y);
                if (onChange) onChange(mappedValue());
            }
            break;
        case TouchEvent::RELEASE:
            // Only stop dragging on finger lift
            if (dragging) {
                dragging = false;
                value = pointToValue(e.point.x, e.point.y);
                if (onChange) onChange(mappedValue());
            }
            break;
        default: break;
    }
}

} // namespace uisys
