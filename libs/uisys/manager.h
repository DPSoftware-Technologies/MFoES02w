#pragma once
#include "button_widget.h"
#include "slider_widget.h"
#include "dial_widget.h"
#include "combobox_widget.h"
#include "textedit_widget.h"
#include "spinbox_widget.h"
#include "keyboard_widget.h"
#include "font.h"
#include <vector>
#include <deque>
#include <string>

namespace uisys {

class Manager {
private:
    struct BtnEntry    { std::string id; ButtonWidget   widget; };
    struct SldrEntry   { std::string id; SliderWidget   widget; };
    struct DialEntry   { std::string id; DialWidget     widget; };
    struct ComboEntry  { std::string id; ComboBoxWidget widget; };
    struct EditEntry   { std::string id; TextEditWidget widget; };
    struct SpinEntry   { std::string id; SpinBoxWidget  widget; };

    std::vector<BtnEntry>   _buttons;
    std::vector<SldrEntry>  _sliders;
    std::vector<DialEntry>  _dials;
    std::vector<ComboEntry> _combos;
    std::deque<EditEntry>  _edits;  // deque: no realloc = stable string* pointers
    std::deque<SpinEntry>  _spins;  // deque: no realloc = stable editBuf* pointers

    // One shared keyboard instance — only one visible at a time
    KeyboardWidget _keyboard;

    // Per-type finders — no templates, no deduction issues
    BtnEntry*   findIn(std::vector<BtnEntry>&   v, const std::string& id) { for (auto& e:v) if(e.id==id) return &e; return nullptr; }
    SldrEntry*  findIn(std::vector<SldrEntry>&  v, const std::string& id) { for (auto& e:v) if(e.id==id) return &e; return nullptr; }
    DialEntry*  findIn(std::vector<DialEntry>&  v, const std::string& id) { for (auto& e:v) if(e.id==id) return &e; return nullptr; }
    ComboEntry* findIn(std::vector<ComboEntry>& v, const std::string& id) { for (auto& e:v) if(e.id==id) return &e; return nullptr; }
    EditEntry*  findIn(std::deque<EditEntry>&   v, const std::string& id) { for (auto& e:v) if(e.id==id) return &e; return nullptr; }
    SpinEntry*  findIn(std::deque<SpinEntry>&   v, const std::string& id) { for (auto& e:v) if(e.id==id) return &e; return nullptr; }

public:
    // kb_x, kb_y, kb_w, kb_h — position/size of the shared keyboard
    Manager(int kb_x = 0, int kb_y = 400, int kb_w = 1280, int kb_h = 320,
            Font kb_font = Font::Medium())
        : _keyboard(kb_x, kb_y, kb_w, kb_h, kb_font)
    {}

    KeyboardWidget& keyboard() { return _keyboard; }

    // ── Add ───────────────────────────────────────────────────────────────────

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

    // ── Lookup ────────────────────────────────────────────────────────────────

    ButtonWidget*   getButton  (const std::string& id) { auto* e=findIn(_buttons,id); return e?&e->widget:nullptr; }
    SliderWidget*   getSlider  (const std::string& id) { auto* e=findIn(_sliders,id); return e?&e->widget:nullptr; }
    DialWidget*     getDial    (const std::string& id) { auto* e=findIn(_dials,  id); return e?&e->widget:nullptr; }
    ComboBoxWidget* getComboBox(const std::string& id) { auto* e=findIn(_combos, id); return e?&e->widget:nullptr; }
    TextEditWidget* getTextEdit(const std::string& id) { auto* e=findIn(_edits,  id); return e?&e->widget:nullptr; }
    SpinBoxWidget*  getSpinBox (const std::string& id) { auto* e=findIn(_spins,  id); return e?&e->widget:nullptr; }

    // ── Remove ────────────────────────────────────────────────────────────────

    void clear() { _buttons.clear();_sliders.clear();_dials.clear();_combos.clear();_edits.clear();_spins.clear(); }

    // ── Event routing ─────────────────────────────────────────────────────────
    // Always route to keyboard first — it consumes events when visible

    void handleEvent(const TouchEventData& e) {
        // Keyboard gets first dibs — if it consumes the event, nothing else sees it
        if (_keyboard.handleEvent(e)) return;
        for (auto& en : _buttons) en.widget.handleEvent(e);
        for (auto& en : _sliders) en.widget.handleEvent(e);
        for (auto& en : _dials)   en.widget.handleEvent(e);
        for (auto& en : _combos)  en.widget.handleEvent(e);
        for (auto& en : _edits)   en.widget.handleEvent(e);
        for (auto& en : _spins)   en.widget.handleEvent(e);
    }

    // ── Draw — keyboard drawn last so it renders on top ───────────────────────

    template<typename GFX>
    void draw(GFX& gfx) {
        for (auto& en : _buttons) en.widget.draw(gfx);
        for (auto& en : _sliders) en.widget.draw(gfx);
        for (auto& en : _dials)   en.widget.draw(gfx);
        for (auto& en : _combos)  en.widget.draw(gfx);
        for (auto& en : _edits)   en.widget.draw(gfx);
        for (auto& en : _spins)   en.widget.draw(gfx);
        _keyboard.draw(gfx);   // on top of everything
    }

    // ── Show / Hide per widget ────────────────────────────────────────────────

    void setVisible(const std::string& id, bool v) {
        if (auto* e=getButton(id))   { e->setVisible(v); return; }
        if (auto* e=getSlider(id))   { e->setVisible(v); return; }
        if (auto* e=getDial(id))     { e->setVisible(v); return; }
        if (auto* e=getComboBox(id)) { e->setVisible(v); return; }
        if (auto* e=getTextEdit(id)) { e->setVisible(v); return; }
        if (auto* e=getSpinBox(id))  { e->setVisible(v); return; }
    }

    void show(const std::string& id) { setVisible(id, true);  }
    void hide(const std::string& id) { setVisible(id, false); }

    bool isVisible(const std::string& id) {
        if (auto* e=getButton(id))   return e->isVisible();
        if (auto* e=getSlider(id))   return e->isVisible();
        if (auto* e=getDial(id))     return e->isVisible();
        if (auto* e=getComboBox(id)) return e->isVisible();
        if (auto* e=getTextEdit(id)) return e->isVisible();
        if (auto* e=getSpinBox(id))  return e->isVisible();
        return false;
    }

    // ── Convenience ──────────────────────────────────────────────────────────

    bool        isToggleOn  (const std::string& id) { auto* b=getButton(id);   return b?b->isToggleOn()      :false; }
    bool        isHoldActive(const std::string& id) { auto* b=getButton(id);   return b?b->isHoldActive()    :false; }
    float       sliderValue (const std::string& id) { auto* s=getSlider(id);   return s?s->getValue()        :0.f;   }
    float       dialValue   (const std::string& id) { auto* d=getDial(id);     return d?d->getValue()        :0.f;   }
    int         comboIndex  (const std::string& id) { auto* c=getComboBox(id); return c?c->getSelectedIndex():-1;    }
    std::string editText    (const std::string& id) { auto* e=getTextEdit(id); return e?e->getText()         :"";    }
    float       spinValue   (const std::string& id) { auto* s=getSpinBox(id);  return s?s->getValue()        :0.f;   }
    int         spinIntValue(const std::string& id) { auto* s=getSpinBox(id);  return s?s->getIntValue()     :0;     }
};

} // namespace uisys
