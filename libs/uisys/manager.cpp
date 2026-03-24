#include "manager.h"

namespace uisys {

//  Constructor 

Manager::Manager(int kb_x, int kb_y, int kb_w, int kb_h, Font kb_font)
    : _keyboard(kb_x, kb_y, kb_w, kb_h, kb_font)
{}

KeyboardWidget& Manager::keyboard() { return _keyboard; }

//  Per-type finders 

Manager::BtnEntry*   Manager::findIn(std::vector<BtnEntry>&   v, const std::string& id) { for (auto& e:v) if(e.id==id) return &e; return nullptr; }
Manager::SldrEntry*  Manager::findIn(std::vector<SldrEntry>&  v, const std::string& id) { for (auto& e:v) if(e.id==id) return &e; return nullptr; }
Manager::DialEntry*  Manager::findIn(std::vector<DialEntry>&  v, const std::string& id) { for (auto& e:v) if(e.id==id) return &e; return nullptr; }
Manager::ComboEntry* Manager::findIn(std::vector<ComboEntry>& v, const std::string& id) { for (auto& e:v) if(e.id==id) return &e; return nullptr; }
Manager::EditEntry*  Manager::findIn(std::deque<EditEntry>&   v, const std::string& id) { for (auto& e:v) if(e.id==id) return &e; return nullptr; }
Manager::SpinEntry*  Manager::findIn(std::deque<SpinEntry>&   v, const std::string& id) { for (auto& e:v) if(e.id==id) return &e; return nullptr; }
Manager::DlgEntry*   Manager::findIn(std::deque<DlgEntry>&    v, const std::string& id) { for (auto& e:v) if(e.id==id) return &e; return nullptr; }

//  Lookup 

ButtonWidget*   Manager::getButton  (const std::string& id) { auto* e=findIn(_buttons,id); return e?&e->widget:nullptr; }
SliderWidget*   Manager::getSlider  (const std::string& id) { auto* e=findIn(_sliders,id); return e?&e->widget:nullptr; }
DialWidget*     Manager::getDial    (const std::string& id) { auto* e=findIn(_dials,  id); return e?&e->widget:nullptr; }
ComboBoxWidget* Manager::getComboBox(const std::string& id) { auto* e=findIn(_combos, id); return e?&e->widget:nullptr; }
TextEditWidget* Manager::getTextEdit(const std::string& id) { auto* e=findIn(_edits,  id); return e?&e->widget:nullptr; }
SpinBoxWidget*  Manager::getSpinBox (const std::string& id) { auto* e=findIn(_spins,  id); return e?&e->widget:nullptr; }

//  Remove 

void Manager::clear() {
    _buttons.clear();
    _sliders.clear();
    _dials.clear();
    _combos.clear();
    _edits.clear();
    _spins.clear();
    _dialogs.clear();
    _needsRedraw = true;
}

//  Event routing 

void Manager::handleEvent(const TouchEventData& e) {
    // Dialogs get absolute priority — block everything when open
    for (auto& en : _dialogs) {
        if (en.widget.isVisible()) {
            en.widget.handleEvent(e);
            _needsRedraw = true;
            return;  // nothing else sees the event
        }
    }
    // Keyboard gets second priority
    if (_keyboard.handleEvent(e)) {
        _needsRedraw = true;
        return;
    }
    // Normal widgets — collect whether any of them consumed the event
    bool consumed = false;
    for (auto& en : _buttons) { auto pre = en.widget.isPressed(); en.widget.handleEvent(e); if (en.widget.isPressed() != pre || e.event == TouchEvent::RELEASE) consumed = true; }
    for (auto& en : _sliders) { en.widget.handleEvent(e); consumed = true; }
    for (auto& en : _dials)   { en.widget.handleEvent(e); consumed = true; }
    for (auto& en : _combos)  { en.widget.handleEvent(e); consumed = true; }
    for (auto& en : _edits)   { en.widget.handleEvent(e); consumed = true; }
    for (auto& en : _spins)   { en.widget.handleEvent(e); consumed = true; }
    if (consumed) _needsRedraw = true;
}

//  Show / Hide 

void Manager::setVisible(const std::string& id, bool v) {
    if (auto* e=getButton(id))   { e->setVisible(v); _needsRedraw = true; return; }
    if (auto* e=getSlider(id))   { e->setVisible(v); _needsRedraw = true; return; }
    if (auto* e=getDial(id))     { e->setVisible(v); _needsRedraw = true; return; }
    if (auto* e=getComboBox(id)) { e->setVisible(v); _needsRedraw = true; return; }
    if (auto* e=getTextEdit(id)) { e->setVisible(v); _needsRedraw = true; return; }
    if (auto* e=getSpinBox(id))  { e->setVisible(v); _needsRedraw = true; return; }
}

void Manager::show(const std::string& id) { setVisible(id, true);  }
void Manager::hide(const std::string& id) { setVisible(id, false); }

bool Manager::isVisible(const std::string& id) {
    if (auto* e=getButton(id))   return e->isVisible();
    if (auto* e=getSlider(id))   return e->isVisible();
    if (auto* e=getDial(id))     return e->isVisible();
    if (auto* e=getComboBox(id)) return e->isVisible();
    if (auto* e=getTextEdit(id)) return e->isVisible();
    if (auto* e=getSpinBox(id))  return e->isVisible();
    return false;
}

//  Convenience getters 

bool        Manager::isToggleOn  (const std::string& id) { auto* b=getButton(id);   return b?b->isToggleOn()      :false; }
bool        Manager::isHoldActive(const std::string& id) { auto* b=getButton(id);   return b?b->isHoldActive()    :false; }
float       Manager::sliderValue (const std::string& id) { auto* s=getSlider(id);   return s?s->getValue()        :0.f;   }
float       Manager::dialValue   (const std::string& id) { auto* d=getDial(id);     return d?d->getValue()        :0.f;   }
int         Manager::comboIndex  (const std::string& id) { auto* c=getComboBox(id); return c?c->getSelectedIndex():-1;    }
std::string Manager::editText    (const std::string& id) { auto* e=getTextEdit(id); return e?e->getText()         :"";    }
float       Manager::spinValue   (const std::string& id) { auto* s=getSpinBox(id);  return s?s->getValue()        :0.f;   }
int         Manager::spinIntValue(const std::string& id) { auto* s=getSpinBox(id);  return s?s->getIntValue()     :0;     }

} // namespace uisys