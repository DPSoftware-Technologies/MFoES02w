#pragma once
#include "font.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "hwinterface/gt911.h"


namespace uisys {

// ===== Dialog mode ============================================================
//
// Notice       — single button (default: "OK")
// YesNo        — two buttons  (default: "Yes", "No")
// YesNoCancel  — three buttons (default: "Yes", "No", "Cancel")
//
enum class DialogMode {
    Notice,
    YesNo,
    YesNoCancel,
};

// ===== Output state ===========================================================
//
// Cancel = 0   — third button / dismissed
// No     = 1   — second button / negative
// Yes    = 2   — first button  / affirmative / OK
//
namespace DialogResult {
    constexpr int Cancel = 0;
    constexpr int No     = 1;
    constexpr int Yes    = 2;
}

// ===== Ask state ==============================================================

enum class DialogState {
    NotAsked,
    Asking,
    Asked,
};

// ===== Icon type ==============================================================
//
// Each icon has a matching default title bar color:
//   None        → dark grey    0xFF2A2A2A
//   Info        → blue         0xFF1565C0
//   Warning     → amber        0xFF8A6000
//   Error       → dark red     0xFF7A1A1A
//   Question    → teal         0xFF005F61
//
enum class DialogIcon {
    None,
    Info,
    Warning,
    Error,
    Question,
};

/// Returns the default title bar color for a given icon.
inline uint32_t dialogIconTitleBarColor(DialogIcon icon) {
    switch (icon) {
        case DialogIcon::Info:     return 0xFF1565C0u;  // blue
        case DialogIcon::Warning:  return 0xFF8A6000u;  // amber
        case DialogIcon::Error:    return 0xFF7A1A1Au;  // dark red
        case DialogIcon::Question: return 0xFF005F61u;  // teal
        case DialogIcon::None:
        default:                   return 0xFF2A2A2Au;  // dark grey
    }
}

// ===== DialogWidget ===========================================================

class DialogWidget {
public:
    using Callback = std::function<void(int result)>;

private:
    // Layout
    int boxX, boxY, boxW, boxH;

    // Content
    std::string     title;
    std::string     description;
    DialogMode      mode        = DialogMode::YesNoCancel;
    DialogIcon      icon        = DialogIcon::Warning;
    DialogState     state       = DialogState::NotAsked;

    // Custom button labels (empty = use mode defaults)
    std::string     labelBtn0;   // Cancel / third
    std::string     labelBtn1;   // No     / second
    std::string     labelBtn2;   // Yes/OK / first

    // Feature flags
    bool _buttonsHidden  = false;  ///< hide button row + separator

    // Colors
    uint32_t colOverlay    = 0x80000000u;  // dimmed background
    uint32_t colBox        = 0xFF1E1E1Eu;  // dialog box background
    uint32_t colBorder     = 0xFF5A5A5Au;  // box border
    uint32_t colTitleBar   = 0xFF2A2A2Au;  // title bar background — set from icon by default
    uint32_t colTitle      = 0xFFFFFFFFu;  // title text color
    uint32_t colDesc       = 0xFFCCCCCCu;  // description text color
    uint32_t colSpacer     = 0xFF3A3A3Au;  // spacer line color in description
    uint32_t colSeparator  = 0xFF3A3A3Au;  // line above buttons

    uint32_t colBtnYes     = 0xFF1A4A1Au;
    uint32_t colBtnYesPr   = 0xFF2E8B2Eu;
    uint32_t colBtnNo      = 0xFF2A2A2Au;
    uint32_t colBtnNoPr    = 0xFF505050u;
    uint32_t colBtnCancel  = 0xFF3A1A1Au;
    uint32_t colBtnCancelPr= 0xFF7A2020u;
    uint32_t colBtnBorder  = 0xFF6A6A6Au;
    uint32_t colBtnText    = 0xFFFFFFFFu;

    Font     titleFont = Font::Large();
    Font     descFont  = Font::Medium();
    Font     btnFont   = Font::Medium();

    Callback onResult;

    int pressedBtn = -1;

    // Internal helpers
    struct BtnLayout { int x, y, w, h; std::string label; int result; };
    std::vector<BtnLayout> _buildButtons() const;
    int _hitTestBtn(int tx, int ty) const;

    // Icon drawing
    template<typename GFX>
    void _drawIcon(GFX& gfx, int ix, int iy, int size) const {
        switch (icon) {
            case DialogIcon::Info: {
                gfx.fillCircle(ix, iy, size/2, 0xFF1565C0u);
                gfx.drawCircle(ix, iy, size/2, 0xFF90CAF9u);
                gfx.fillRect(ix-2, iy-size/5,   4, size/2+2, 0xFFFFFFFFu);
                gfx.fillRect(ix-2, iy-size/2+4,  4, 4,        0xFFFFFFFFu);
                break;
            }
            case DialogIcon::Warning: {
                int hs = size/2;
                for (int i = 0; i < 4; i++)
                    gfx.drawTriangle(ix, iy-hs+i, ix-hs+i, iy+hs-i, ix+hs-i, iy+hs-i, 0xFFFFD600u);
                gfx.fillTriangle(ix, iy-hs+4, ix-hs+4, iy+hs-4, ix+hs-4, iy+hs-4, 0xFFFBC02Du);
                gfx.fillRect(ix-2, iy-hs/2+6, 4, hs-2, 0xFF212121u);
                gfx.fillRect(ix-3, iy+hs-12,  6, 6,    0xFF212121u);
                break;
            }
            case DialogIcon::Error: {
                gfx.fillCircle(ix, iy, size/2, 0xFFC62828u);
                gfx.drawCircle(ix, iy, size/2, 0xFFEF9A9Au);
                int r = size/4;
                gfx.drawLine(ix-r,   iy-r, ix+r,   iy+r, 0xFFFFFFFFu);
                gfx.drawLine(ix-r+1, iy-r, ix+r+1, iy+r, 0xFFFFFFFFu);
                gfx.drawLine(ix+r,   iy-r, ix-r,   iy+r, 0xFFFFFFFFu);
                gfx.drawLine(ix+r+1, iy-r, ix-r+1, iy+r, 0xFFFFFFFFu);
                break;
            }
            case DialogIcon::Question: {
                gfx.fillCircle(ix, iy, size/2, 0xFF00696Bu);
                gfx.drawCircle(ix, iy, size/2, 0xFF80CBCBu);
                gfx.fillRect(ix-2, iy-2,       4, size/4, 0xFFFFFFFFu);
                gfx.fillRect(ix-2, iy+size/4,  4, 4,      0xFFFFFFFFu);
                break;
            }
            case DialogIcon::None:
            default: break;
        }
    }

public:
    DialogWidget();
    DialogWidget(int screenW, int screenH,
                 const std::string& title,
                 const std::string& description,
                 DialogMode mode = DialogMode::YesNoCancel,
                 DialogIcon icon = DialogIcon::Warning,
                 Callback   cb   = nullptr);

    // ===== Configuration =====================================================

    void setTitle      (const std::string& t)  { title = t; }
    void setDescription(const std::string& d)  { description = d; }
    void setMode       (DialogMode m)           { mode = m; }
    void setIcon       (DialogIcon i)           { icon = i; colTitleBar = dialogIconTitleBarColor(i); }
    void setCallback   (Callback cb)            { onResult = cb; }

    /// Title bar background color.
    /// By default this is derived from the icon (see dialogIconTitleBarColor).
    /// Call this AFTER setIcon() to override.
    void setTitleBarColor(uint32_t c)           { colTitleBar = c; }

    /// Title text color (default white).
    void setTitleColor  (uint32_t c)            { colTitle  = c; }
    void setDescColor   (uint32_t c)            { colDesc   = c; }
    void setBoxColor    (uint32_t c)            { colBox    = c; }
    void setOverlayColor(uint32_t c)            { colOverlay = c; }

    /// Hide the button row and separator line.
    /// Useful for progress/info dialogs that dismiss themselves programmatically.
    void hideButtons()  { _buttonsHidden = true;  }
    void showButtons()  { _buttonsHidden = false; }
    bool areButtonsHidden() const { return _buttonsHidden; }

    /// Override button labels. Pass "" to keep the mode default.
    /// btn2 = Yes/OK   btn1 = No   btn0 = Cancel
    void setButtonLabels(const std::string& btn2,
                         const std::string& btn1 = "",
                         const std::string& btn0 = "");

    void setTitleFont(const Font& f) { titleFont = f; }
    void setDescFont (const Font& f) { descFont  = f; }
    void setBtnFont  (const Font& f) { btnFont   = f; }

    /// Reposition the dialog box (default: screen-centered).
    void setBounds(int x, int y, int w, int h) { boxX=x; boxY=y; boxW=w; boxH=h; }

    // ===== State =============================================================

    void fire();

    /// Dismiss programmatically (e.g. from a progress dialog).
    void dismiss(int result = DialogResult::Cancel);

    bool        isVisible() const { return state == DialogState::Asking; }
    DialogState getState()  const { return state; }

    // ===== Event handling ====================================================

    bool handleEvent(const TouchEventData& e);

    // ===== Draw ==============================================================
    //
    // Description spacer lines:
    //   Any line in the description that starts with "---" is rendered as a
    //   horizontal rule instead of text.  Use "\n---\n" to insert a spacer:
    //     "First section\n---\nSecond section"
    //

    template<typename GFX>
    void draw(GFX& gfx) const {
        if (state != DialogState::Asking) return;

        // Dim the entire screen
        gfx.fillScreen(colOverlay);

        // Box shadow
        gfx.fillRect(boxX+4, boxY+4, boxW, boxH, 0x60000000u);

        // Box background + border
        gfx.fillRect(boxX, boxY, boxW, boxH, colBox);
        gfx.drawRect(boxX, boxY, boxW, boxH, colBorder);
        gfx.drawRect(boxX+1, boxY+1, boxW-2, boxH-2, colBorder);

        // Title bar — color from icon by default
        int titleH = titleFont.charH() + 20;
        gfx.fillRect(boxX+2, boxY+2, boxW-4, titleH, colTitleBar);

        // Title text
        titleFont.apply(gfx);
        int titleTX = boxX + 16;
        int titleTY = boxY + (titleH - titleFont.charH()) / 2;
        gfx.setCursor(titleTX, titleTY);
        gfx.setTextColor(colTitle, colTitleBar);
        gfx.writeText(title.c_str());

        // Icon
        int contentY = boxY + titleH + 16;
        int iconSize = 40;
        int iconX    = boxX + 28 + iconSize/2;
        int iconY    = contentY + iconSize/2;
        if (icon != DialogIcon::None)
            _drawIcon(gfx, iconX, iconY, iconSize);

        // Description — word-wrap + spacer line support
        descFont.apply(gfx);
        int descX = (icon != DialogIcon::None) ? iconX + iconSize/2 + 16 : boxX + 16;
        int descY = contentY + 8;
        int descW = boxX + boxW - descX - 16;

        // Split description into raw lines first (respecting \n)
        std::vector<std::string> rawLines;
        {
            std::string cur;
            for (size_t ci = 0; ci <= description.size(); ci++) {
                char ch = (ci < description.size()) ? description[ci] : '\n';
                if (ch == '\n') { rawLines.push_back(cur); cur.clear(); }
                else cur += ch;
            }
        }

        // Word-wrap each raw line; spacer lines ("---...") pass through as-is
        std::vector<std::string> lines;
        for (auto& raw : rawLines) {
            if (raw.size() >= 3 && raw[0]=='-' && raw[1]=='-' && raw[2]=='-') {
                lines.push_back("---");  // spacer sentinel
                continue;
            }
            // Word-wrap
            std::string word, line;
            for (size_t ci = 0; ci <= raw.size(); ci++) {
                char ch = (ci < raw.size()) ? raw[ci] : ' ';
                if (ch == ' ' || ci == raw.size()) {
                    if (!word.empty()) {
                        std::string test = line.empty() ? word : line + " " + word;
                        if (descFont.textWidth((int)test.size()) > descW && !line.empty()) {
                            lines.push_back(line); line = word;
                        } else { line = test; }
                        word.clear();
                    }
                } else { word += ch; }
            }
            if (!line.empty()) lines.push_back(line);
        }

        int lineH = descFont.charH() + 4;
        for (size_t li = 0; li < lines.size(); li++) {
            int ly = descY + (int)li * lineH;
            if (lines[li] == "---") {
                // Render as horizontal rule
                int mx = descX + (boxX + boxW - 16 - descX) / 2;
                int rw = boxX + boxW - 16 - descX;
                gfx.drawLine(descX, ly + lineH/2, descX + rw, ly + lineH/2, colSpacer);
                gfx.drawLine(descX, ly + lineH/2 + 1, descX + rw, ly + lineH/2 + 1, colSpacer);
                (void)mx;
            } else {
                gfx.setCursor(descX, ly);
                gfx.setTextColor(colDesc, colBox);
                gfx.writeText(lines[li].c_str());
            }
        }

        if (_buttonsHidden) return;

        // Separator line above buttons
        int sepY = boxY + boxH - 60;
        gfx.drawLine(boxX+8, sepY, boxX+boxW-8, sepY, colSeparator);

        // Buttons
        auto btns = _buildButtons();
        for (auto& b : btns) {
            bool isPressed = (pressedBtn == b.result);
            uint32_t bg, bdr;
            if (b.result == DialogResult::Yes) {
                bg  = isPressed ? colBtnYesPr   : colBtnYes;
                bdr = 0xFF4CAF50u;
            } else if (b.result == DialogResult::No) {
                bg  = isPressed ? colBtnNoPr     : colBtnNo;
                bdr = colBtnBorder;
            } else {
                bg  = isPressed ? colBtnCancelPr : colBtnCancel;
                bdr = 0xFF8B3A3Au;
            }

            gfx.fillRect(b.x, b.y, b.w, b.h, bg);
            gfx.drawRect(b.x, b.y, b.w, b.h, bdr);
            if (isPressed)
                gfx.drawRect(b.x+1, b.y+1, b.w-2, b.h-2, bdr);

            btnFont.apply(gfx);
            int tx = b.x + (b.w - btnFont.textWidth((int)b.label.size())) / 2;
            int ty = b.y + (b.h - btnFont.charH()) / 2;
            gfx.setCursor(tx, ty);
            gfx.setTextColor(colBtnText, bg);
            gfx.writeText(b.label.c_str());
        }
    }
};

} // namespace uisys