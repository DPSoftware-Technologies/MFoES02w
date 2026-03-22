#include "dialog_widget.h"

namespace uisys {

// ===== Constructors ===========================================================

DialogWidget::DialogWidget()
    : boxX(200), boxY(150), boxW(500), boxH(260)
{
    colTitleBar = dialogIconTitleBarColor(icon);
}

DialogWidget::DialogWidget(int screenW, int screenH,
                           const std::string& title,
                           const std::string& description,
                           DialogMode mode,
                           DialogIcon icon,
                           Callback   cb)
    : title(title), description(description),
      mode(mode), icon(icon), onResult(cb)
{
    // Title bar color from icon — can be overridden after construction
    colTitleBar = dialogIconTitleBarColor(icon);

    // Center the box on screen
    boxW = (screenW > 800) ? 560 : screenW * 7 / 10;
    boxH = 280;
    boxX = (screenW - boxW) / 2;
    boxY = (screenH - boxH) / 2;
}

// ===== Button layout ==========================================================

static constexpr int BTN_H   = 40;
static constexpr int BTN_GAP = 12;

std::vector<DialogWidget::BtnLayout> DialogWidget::_buildButtons() const {
    auto lbl2 = !labelBtn2.empty() ? labelBtn2
        : (mode == DialogMode::Notice ? "OK" : "Yes");
    auto lbl1 = !labelBtn1.empty() ? labelBtn1 : "No";
    auto lbl0 = !labelBtn0.empty() ? labelBtn0 : "Cancel";

    std::vector<BtnLayout> btns;

    int totalBtns = (mode == DialogMode::Notice)  ? 1
                  : (mode == DialogMode::YesNo)    ? 2
                  :                                  3;

    int btnW   = 110;
    int totalW = totalBtns * btnW + (totalBtns - 1) * BTN_GAP;
    int startX = boxX + (boxW - totalW) / 2;
    int btnY   = boxY + boxH - BTN_H - 12;

    switch (mode) {
        case DialogMode::Notice:
            btns.push_back({ startX, btnY, btnW, BTN_H, lbl2, DialogResult::Yes });
            break;
        case DialogMode::YesNo:
            btns.push_back({ startX,              btnY, btnW, BTN_H, lbl1, DialogResult::No  });
            btns.push_back({ startX+btnW+BTN_GAP, btnY, btnW, BTN_H, lbl2, DialogResult::Yes });
            break;
        case DialogMode::YesNoCancel:
            btns.push_back({ startX,                   btnY, btnW, BTN_H, lbl0, DialogResult::Cancel });
            btns.push_back({ startX+btnW+BTN_GAP,      btnY, btnW, BTN_H, lbl1, DialogResult::No     });
            btns.push_back({ startX+(btnW+BTN_GAP)*2,  btnY, btnW, BTN_H, lbl2, DialogResult::Yes    });
            break;
    }

    return btns;
}

// ===== Hit test ===============================================================

int DialogWidget::_hitTestBtn(int tx, int ty) const {
    if (_buttonsHidden) return -1;
    for (auto& b : _buildButtons())
        if (tx >= b.x && tx <= b.x+b.w && ty >= b.y && ty <= b.y+b.h)
            return b.result;
    return -1;
}

// ===== setButtonLabels ========================================================

void DialogWidget::setButtonLabels(const std::string& btn2,
                                   const std::string& btn1,
                                   const std::string& btn0) {
    labelBtn2 = btn2;
    labelBtn1 = btn1;
    labelBtn0 = btn0;
}

// ===== fire / dismiss =========================================================

void DialogWidget::fire() {
    state      = DialogState::Asking;
    pressedBtn = -1;
}

void DialogWidget::dismiss(int result) {
    if (state != DialogState::Asking) return;
    state      = DialogState::Asked;
    pressedBtn = -1;
    if (onResult) onResult(result);
}

// ===== handleEvent ============================================================

bool DialogWidget::handleEvent(const TouchEventData& e) {
    if (state != DialogState::Asking) return false;

    switch (e.event) {
        case TouchEvent::PRESS:
            pressedBtn = _hitTestBtn(e.point.x, e.point.y);
            return true;   // always consume when visible

        case TouchEvent::RELEASE: {
            int idx = _hitTestBtn(e.point.x, e.point.y);
            pressedBtn = -1;
            if (idx >= 0) {
                state = DialogState::Asked;
                if (onResult) onResult(idx);
            }
            return true;
        }

        case TouchEvent::MOVE:
        case TouchEvent::HOLD:
            return true;
    }

    return false;
}

} // namespace uisys