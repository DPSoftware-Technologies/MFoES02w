#include "combobox_widget.h"

namespace uisys {

ComboBoxWidget::ComboBoxWidget() : x(0), y(0), w(200), h(40) {}

ComboBoxWidget::ComboBoxWidget(int x, int y, int w, int h,
                               const std::vector<std::string>& items,
                               int defaultIndex, Callback cb,
                               Font font, int maxVisible)
    : x(x), y(y), w(w), h(h),
      items(items), selectedIndex(defaultIndex),
      onChange(cb), font(font), maxVisible(maxVisible)
{}

//  Private helpers 

int  ComboBoxWidget::itemH()        const { return font.charH() + 12; }
int  ComboBoxWidget::visibleCount() const { return (int)items.size() < maxVisible ? (int)items.size() : maxVisible; }
bool ComboBoxWidget::needsScroll()  const { return (int)items.size() > maxVisible; }

int ComboBoxWidget::dropdownH() const {
    return visibleCount() * itemH();
}

bool ComboBoxWidget::hitTestHeader(int tx, int ty) const {
    return tx >= x && tx <= x+w && ty >= y && ty <= y+h;
}

int ComboBoxWidget::hitTestDropdown(int tx, int ty) const {
    if (!isOpen) return -1;
    int dropW = needsScroll() ? w - SCROLL_W : w;
    if (tx < x || tx > x + dropW) return -1;
    int dropY = y + h;
    int dh    = dropdownH();
    if (ty < dropY || ty > dropY + dh) return -1;
    int row = (ty - dropY) / itemH();
    int idx = scrollOffset + row;
    if (idx < 0 || idx >= (int)items.size()) return -1;
    return idx;
}

bool ComboBoxWidget::hitScrollUp(int tx, int ty) const {
    if (!isOpen || !needsScroll()) return false;
    int sx = x + w - SCROLL_W;
    int sy = y + h;
    return tx >= sx && tx <= sx + SCROLL_W && ty >= sy && ty <= sy + ARROW_BTN_H;
}

bool ComboBoxWidget::hitScrollDown(int tx, int ty) const {
    if (!isOpen || !needsScroll()) return false;
    int sx = x + w - SCROLL_W;
    int sy = y + h + dropdownH() - ARROW_BTN_H;
    return tx >= sx && tx <= sx + SCROLL_W && ty >= sy && ty <= sy + ARROW_BTN_H;
}

void ComboBoxWidget::clampScroll() {
    int maxOff = (int)items.size() - maxVisible;
    if (maxOff < 0)        maxOff = 0;
    if (scrollOffset < 0)  scrollOffset = 0;
    if (scrollOffset > maxOff) scrollOffset = maxOff;
}

//  Public interface 

void ComboBoxWidget::setVisible(bool v) { visible_widget = v; }
bool ComboBoxWidget::isVisible()  const { return visible_widget; }

void ComboBoxWidget::addItem(const std::string& item) { items.push_back(item); }

void ComboBoxWidget::clearItems() {
    items.clear(); selectedIndex = 0; scrollOffset = 0;
}

void ComboBoxWidget::setItems(const std::vector<std::string>& newItems, int defaultIndex) {
    items = newItems; selectedIndex = defaultIndex;
    scrollOffset = 0; isOpen = false;
}

int ComboBoxWidget::getSelectedIndex() const { return selectedIndex; }

const std::string& ComboBoxWidget::getSelectedValue() const {
    static std::string empty = "";
    return items.empty() ? empty : items[selectedIndex];
}

void ComboBoxWidget::setSelectedIndex(int idx) {
    if (idx >= 0 && idx < (int)items.size()) selectedIndex = idx;
}

void ComboBoxWidget::setFont(const Font& f)    { font = f; }
void ComboBoxWidget::setMaxVisible(int n)      { maxVisible = n; }
void ComboBoxWidget::setCallback(Callback cb)  { onChange = cb; }
bool ComboBoxWidget::isDropdownOpen()    const { return isOpen; }

void ComboBoxWidget::setColors(uint32_t bg, uint32_t bgOpen, uint32_t selected,
                                uint32_t border, uint32_t text) {
    colBg=bg; colBgOpen=bgOpen; colSelected=selected; colBorder=border; colText=text;
}

void ComboBoxWidget::handleEvent(const TouchEventData& e) {
    if (!visible_widget) return;
    switch (e.event) {

        case TouchEvent::PRESS:
            if (isOpen) {
                if (hitScrollUp(e.point.x, e.point.y)) {
                    scrollOffset--; clampScroll(); break;
                }
                if (hitScrollDown(e.point.x, e.point.y)) {
                    scrollOffset++; clampScroll(); break;
                }
                int idx = hitTestDropdown(e.point.x, e.point.y);
                if (idx >= 0) {
                    selectedIndex = idx;
                    if (onChange) onChange(selectedIndex, items[selectedIndex]);
                }
                isOpen = false;
            } else {
                if (hitTestHeader(e.point.x, e.point.y)) {
                    isOpen = true;
                    scrollOffset = selectedIndex;
                    clampScroll();
                }
            }
            break;

        case TouchEvent::MOVE:
            if (isOpen) {
                hoveredIndex = hitTestDropdown(e.point.x, e.point.y);

                // Drag-scroll near top/bottom edge
                int dropY = y + h;
                int dh    = dropdownH();
                if (e.point.y < dropY + 20 && e.point.y > dropY) {
                    scrollOffset--; clampScroll();
                } else if (e.point.y > dropY + dh - 20 && e.point.y < dropY + dh) {
                    scrollOffset++; clampScroll();
                }
            }
            break;

        case TouchEvent::RELEASE:
            hoveredIndex = -1;
            break;

        default: break;
    }
}

} // namespace uisys
