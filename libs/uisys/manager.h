#pragma once
#include "font.h"
#include <vector>
#include <deque>
#include <string>
#include <functional>
#include <algorithm>
#include <unistd.h>

#include "keyboard_widget.h"

#include "button_widget.h"
#include "slider_widget.h"
#include "dial_widget.h"
#include "combobox_widget.h"
#include "textedit_widget.h"
#include "spinbox_widget.h"
#include "dialog_widget.h"

namespace uisys {

class Manager {
public:
    using PumpFn = std::function<void()>;

private:
    struct BtnEntry   { std::string id; ButtonWidget   widget; };
    struct SldrEntry  { std::string id; SliderWidget   widget; };
    struct DialEntry  { std::string id; DialWidget     widget; };
    struct ComboEntry { std::string id; ComboBoxWidget widget; };
    struct EditEntry  { std::string id; TextEditWidget widget; };
    struct SpinEntry  { std::string id; SpinBoxWidget  widget; };

    std::vector<BtnEntry>   _buttons;
    std::vector<SldrEntry>  _sliders;
    std::vector<DialEntry>  _dials;
    std::vector<ComboEntry> _combos;
    std::deque<EditEntry>   _edits;  // deque: no realloc = stable string* pointers
    std::deque<SpinEntry>   _spins;  // deque: no realloc = stable editBuf* pointers

    KeyboardWidget _keyboard;

    // Dialogs — stored by id, drawn on top of everything
    struct DlgEntry { std::string id; DialogWidget widget; };
    std::deque<DlgEntry> _dialogs;

    PumpFn _pumpFn;   ///< optional render+event pump for quickFireDialog

    // Per-type finders — defined in manager.cpp
    BtnEntry*   findIn(std::vector<BtnEntry>&   v, const std::string& id);
    SldrEntry*  findIn(std::vector<SldrEntry>&  v, const std::string& id);
    DialEntry*  findIn(std::vector<DialEntry>&  v, const std::string& id);
    ComboEntry* findIn(std::vector<ComboEntry>& v, const std::string& id);
    EditEntry*  findIn(std::deque<EditEntry>&   v, const std::string& id);
    SpinEntry*  findIn(std::deque<SpinEntry>&   v, const std::string& id);
    DlgEntry*   findIn(std::deque<DlgEntry>&    v, const std::string& id);

public:
    // kb_x, kb_y, kb_w, kb_h — position/size of the shared keyboard
    Manager(int kb_x = 0, int kb_y = 400, int kb_w = 1280, int kb_h = 320,
            Font kb_font = Font::Medium());

    KeyboardWidget& keyboard();

    // ===== Pump function =====================================================
    //
    // quickFireDialog() needs to spin the render+event loop internally while
    // it waits for the user to respond.  Register your frame loop here once
    // at startup:
    //
    //   ui.setPumpFn([&](){
    //       // poll touch events
    //       TouchEventData e;
    //       while (touch.poll(e)) ui.handleEvent(e);
    //       // draw frame
    //       gfx.fillScreen(GFX_BLACK);
    //       ui.draw(gfx);
    //       gfx.swapBuffers();
    //   });
    //
    // If no pump function is set, quickFireDialog falls back to a simple
    // busy-wait (dialog draws but no touch events — less ideal).
    //
    void setPumpFn(PumpFn fn) { _pumpFn = fn; }

    //  Add 

    ButtonWidget& addButton(const std::string& id,
                            int x, int y, int w, int h,
                            const std::string& label, ButtonMode mode,
                            ButtonWidget::Callback cb = nullptr,
                            ButtonTheme theme = ButtonTheme::Default(),
                            Font font = Font::Medium())
    {
        auto* e = findIn(_buttons, id); if (e) return e->widget;
        _buttons.push_back({id, ButtonWidget(x,y,w,h,label,mode,cb,theme,font)});
        return _buttons.back().widget;
    }

    SliderWidget& addSlider(const std::string& id,
                            int x, int y, int w, int h,
                            SliderOrientation ori, const std::string& label,
                            float mn=0.f, float mx=1.f,
                            SliderWidget::Callback cb=nullptr,
                            Font font=Font::Medium())
    {
        auto* e = findIn(_sliders, id); if (e) return e->widget;
        _sliders.push_back({id, SliderWidget(x,y,w,h,ori,label,mn,mx,cb,font)});
        return _sliders.back().widget;
    }

    DialWidget& addDial(const std::string& id,
                        int cx, int cy, int radius, const std::string& label,
                        float mn=0.f, float mx=1.f,
                        DialWidget::Callback cb=nullptr,
                        Font font=Font::Medium())
    {
        auto* e = findIn(_dials, id); if (e) return e->widget;
        _dials.push_back({id, DialWidget(cx,cy,radius,label,mn,mx,cb,font)});
        return _dials.back().widget;
    }

    ComboBoxWidget& addComboBox(const std::string& id,
                                int x, int y, int w, int h,
                                const std::vector<std::string>& items,
                                int defaultIdx=0,
                                ComboBoxWidget::Callback cb=nullptr,
                                Font font=Font::Medium(), int maxVisible=4)
    {
        auto* e = findIn(_combos, id); if (e) return e->widget;
        _combos.push_back({id, ComboBoxWidget(x,y,w,h,items,defaultIdx,cb,font,maxVisible)});
        return _combos.back().widget;
    }

    TextEditWidget& addTextEdit(const std::string& id,
                                int x, int y, int w, int h,
                                const std::string& placeholder = "",
                                bool numericOnly = false,
                                Font font = Font::Medium())
    {
        auto* e = findIn(_edits, id); if (e) return e->widget;
        _edits.push_back({id, TextEditWidget(x,y,w,h,placeholder,&_keyboard,numericOnly,font)});
        return _edits.back().widget;
    }

    SpinBoxWidget& addSpinBox(const std::string& id,
                              int x, int y, int w, int h,
                              SpinBoxType type,
                              float mn, float mx, float step=1.f,
                              float defaultVal=0.f,
                              bool showButtons=true,
                              Font font=Font::Medium())
    {
        auto* e = findIn(_spins, id); if (e) return e->widget;
        _spins.push_back({id, SpinBoxWidget(x,y,w,h,type,mn,mx,step,defaultVal,&_keyboard,showButtons,font)});
        return _spins.back().widget;
    }

    /// Add a dialog. screenW/H are used to auto-center it.
    DialogWidget& addDialog(const std::string& id,
                            int screenW, int screenH,
                            const std::string& title,
                            const std::string& description,
                            DialogMode mode     = DialogMode::YesNoCancel,
                            DialogIcon icon     = DialogIcon::Warning,
                            DialogWidget::Callback cb = nullptr)
    {
        auto* e = findIn(_dialogs, id); if (e) return e->widget;
        _dialogs.push_back({id, DialogWidget(screenW, screenH, title, description, mode, icon, cb)});
        return _dialogs.back().widget;
    }

    DialogWidget* getDialog(const std::string& id) {
        auto* e = findIn(_dialogs, id); return e ? &e->widget : nullptr;
    }

    /// Fire a dialog by id — shows it and dims the background
    void fireDialog(const std::string& id) {
        auto* d = getDialog(id); if (d) d->fire();
    }

    /// quickFireDialog — create, show, block until user responds, then clean up.
    ///
    /// Synchronous blocking call — spins the pump function until user responds.
    /// Returns: DialogResult::Yes(2) / No(1) / Cancel(0)
    ///
    /// titleBarColor:
    ///   Pass GFX_TRANSPARENT (0x00000000) to use the icon's default color.
    ///   Pass any ARGB8888 value to override (e.g. 0xFF7A1A1A for custom red).
    ///
    /// buttonsHidden:
    ///   true  = hide button row + separator (use dismiss() to close programmatically)
    ///   false = normal buttons shown (default)
    ///
    int quickFireDialog(int screenW, int screenH,
                        const std::string& title,
                        const std::string& description,
                        DialogMode mode             = DialogMode::YesNoCancel,
                        DialogIcon icon             = DialogIcon::Warning,
                        uint32_t   titleBarColor    = GFX_TRANSPARENT,  // 0 = use icon default
                        bool       buttonsHidden    = false,
                        const std::string& lBtn2    = "",   // Yes/OK label override
                        const std::string& lBtn1    = "",   // No label override
                        const std::string& lBtn0    = "")   // Cancel label override
    {
        const std::string tmpId = "__qfd_tmp__";

        _dialogs.erase(
            std::remove_if(_dialogs.begin(), _dialogs.end(),
                [&](const DlgEntry& e){ return e.id == tmpId; }),
            _dialogs.end());

        int  result    = DialogResult::Cancel;
        bool responded = false;

        _dialogs.push_back({ tmpId,
            DialogWidget(screenW, screenH, title, description, mode, icon,
                [&](int r){ result = r; responded = true; })
        });
        DlgEntry& entry = _dialogs.back();

        // Title bar color: use icon default unless explicitly overridden
        if (titleBarColor != GFX_TRANSPARENT)
            entry.widget.setTitleBarColor(titleBarColor);

        if (buttonsHidden)
            entry.widget.hideButtons();

        if (!lBtn2.empty() || !lBtn1.empty() || !lBtn0.empty())
            entry.widget.setButtonLabels(lBtn2, lBtn1, lBtn0);

        entry.widget.fire();

        if (_pumpFn) {
            while (!responded) _pumpFn();
        } else {
            fprintf(stderr, "uisys::Manager::quickFireDialog: no pump function set. "
                            "Call setPumpFn() for proper blocking behavior.\n");
            while (!responded) { usleep(10000); }
        }

        _dialogs.erase(
            std::remove_if(_dialogs.begin(), _dialogs.end(),
                [&](const DlgEntry& e){ return e.id == tmpId; }),
            _dialogs.end());

        return result;
    }

    /// True if any dialog is currently showing (blocks all other input)
    bool isAnyDialogOpen() const {
        for (auto& e : _dialogs) if (e.widget.isVisible()) return true;
        return false;
    }

    //  Lookup 

    ButtonWidget*   getButton  (const std::string& id);
    SliderWidget*   getSlider  (const std::string& id);
    DialWidget*     getDial    (const std::string& id);
    ComboBoxWidget* getComboBox(const std::string& id);
    TextEditWidget* getTextEdit(const std::string& id);
    SpinBoxWidget*  getSpinBox (const std::string& id);

    //  Remove 

    void clear();

    //  Event routing 
    // Dialogs get first dibs — they block everything when open.
    // Keyboard gets second dibs — it blocks widgets when visible.

    void handleEvent(const TouchEventData& e);

    //  Draw — templated, must remain in header 

    template<typename GFX>
    void draw(GFX& gfx) {
        for (auto& en : _buttons) en.widget.draw(gfx);
        for (auto& en : _sliders) en.widget.draw(gfx);
        for (auto& en : _dials)   en.widget.draw(gfx);
        for (auto& en : _combos)  en.widget.draw(gfx);
        for (auto& en : _edits)   en.widget.draw(gfx);
        for (auto& en : _spins)   en.widget.draw(gfx);
        _keyboard.draw(gfx);      // on top of widgets
        for (auto& en : _dialogs) en.widget.draw(gfx);  // on top of everything
    }

    //  Show / Hide per widget 

    void setVisible(const std::string& id, bool v);
    void show(const std::string& id);
    void hide(const std::string& id);
    bool isVisible(const std::string& id);

    //  Convenience 

    bool        isToggleOn  (const std::string& id);
    bool        isHoldActive(const std::string& id);
    float       sliderValue (const std::string& id);
    float       dialValue   (const std::string& id);
    int         comboIndex  (const std::string& id);
    std::string editText    (const std::string& id);
    float       spinValue   (const std::string& id);
    int         spinIntValue(const std::string& id);
};

} // namespace uisys