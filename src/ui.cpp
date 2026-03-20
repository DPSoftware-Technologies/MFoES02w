#include "app.h"

void App::initSysUI() {
    ui.addButton("quit", 50, 50, 180, 60, "QUIT", uisys::ButtonMode::TRIGGER,
        [this](bool s){
            pthread_join(usb_thread, nullptr);
            exit(0);
        },
        uisys::ButtonTheme::Danger());

    ui.addButton("halt", 250, 50, 180, 60, "Halt", uisys::ButtonMode::TRIGGER,
        [this](bool s){
            system("halt");
        },
        uisys::ButtonTheme::Danger());

    ui.addButton("hideui", 450, 50, 180, 60, "Hide All UI", uisys::ButtonMode::TRIGGER,
        [this](bool s){
            show_about = false;
            hide_ui = true;
        },
        uisys::ButtonTheme::Danger());
    
    ui.addButton("restart", 650, 50, 180, 60, "Restart", uisys::ButtonMode::TRIGGER,
        [this](bool s){
            system("reboot");
        },
        uisys::ButtonTheme::Danger());

    ui.getButton("quit")->setVisible(false);
    ui.getButton("halt")->setVisible(false);
    ui.getButton("hideui")->setVisible(false);
    ui.getButton("restart")->setVisible(false);
}

void App::initDemoUI() {
    ui.addButton("fire",  50,  600, 180, 60, "FIRE",  uisys::ButtonMode::TRIGGER,
    [this](bool s){ buz.set(1); usleep(50000); buz.set(0); });

    ui.addButton("arm",   250, 600, 180, 60, "ARM",   uisys::ButtonMode::TOGGLE,
        [this](bool s){ printf("ARM = %s\n", s ? "ON" : "OFF"); },
        uisys::ButtonTheme::Military());

    ui.addButton("boost", 450, 600, 180, 60, "BOOST", uisys::ButtonMode::HOLD,
        [this](bool s){ buz.set(s ? 1 : 0); },
        uisys::ButtonTheme::HUD());

    ui.addSlider("vol",  50, 500, 300, 30, uisys::SliderOrientation::HORIZONTAL,
        "Volume", 0.0f, 100.0f,
        [this](float v){ printf("vol=%.1f\n", v); 
    });

    ui.addSlider("gain", 700, 100, 30, 250, uisys::SliderOrientation::VERTICAL,
        "Gain", 0.0f, 10.0f,
        [this](float v){ printf("gain=%.1f\n", v); 
    });

    // Dial
    ui.addDial("freq", 500, 450, 60, "FREQ", 20.0f, 20000.0f,
        [this](float v){ printf("freq=%.0f Hz\n", v); 
    });

    // ComboBox
    ui.addComboBox("mode", 50, 400, 200, 36, 
    {"Normal", "Combat", "Stealth", "Override"}, 0,
    [this](int idx, const std::string& val){
        printf("mode=%s\n", val.c_str());
    });

    // TextEdit — tap to focus, keyboard auto-appears
    ui.addTextEdit("name", 50, 100, 400, 50, "Enter name...");
    ui.addTextEdit("ip",   50, 170, 400, 50, "192.168.x.x", false, uisys::Font::Large());

    // Numeric-only TextEdit
    ui.addTextEdit("port", 50, 240, 200, 50, "8080", true);

    // SpinBox INT with +/- buttons
    ui.addSpinBox("speed", 50, 320, 250, 60,
        uisys::SpinBoxType::INT, 0, 200, 5, 60, true);

    // SpinBox FLOAT, no buttons (keyboard only)
    ui.addSpinBox("gain2", 350, 320, 200, 60,
        uisys::SpinBoxType::FLOAT, 0.0f, 10.0f, 0.1f, 1.0f, false);

    ui.hide("fire");
    ui.hide("boost");
    ui.hide("gain");;
    ui.hide("freq");
    ui.hide("ip");
    ui.hide("port");
    ui.hide("name");
    ui.hide("arm");
    ui.hide("vol");
    ui.getComboBox("mode")->setVisible(false);
    ui.getSpinBox("speed")->setVisible(false);
    ui.getSpinBox("gain2")->setVisible(false);
}
void App::initSidebarBTNs() {
    ui.addButton("sinfo", 1075, 440, 180, 50, "Show Info",   uisys::ButtonMode::TOGGLE,
        [this](bool s){
            show_about = s;
        },
        uisys::ButtonTheme::Military());

    ui.addButton("ssui", 1075, 520, 180, 50, "Show System UI",   uisys::ButtonMode::TOGGLE,
        [this](bool s){
            ui.getButton("quit")->setVisible(s);
            ui.getButton("halt")->setVisible(s);
            ui.getButton("hideui")->setVisible(s);
            ui.getButton("restart")->setVisible(s);
        },
        uisys::ButtonTheme::Military());

    ui.addButton("sdui", 1075, 600, 180, 50, "Show Demo UI",   uisys::ButtonMode::TOGGLE,
        [this](bool s){
            ui.getComboBox("mode")->setVisible(s);
            ui.getSpinBox("speed")->setVisible(s);
            ui.getSpinBox("gain2")->setVisible(s);
            if (!s) {
                ui.hide("fire");
                ui.hide("boost");
                ui.hide("gain");
                ui.hide("freq");
                ui.hide("ip");
                ui.hide("port");
                ui.hide("name");
                ui.hide("arm");
                ui.hide("vol");
            } else {
                ui.show("fire");
                ui.show("boost");
                ui.show("gain");
                ui.show("freq");
                ui.show("ip");
                ui.show("port");
                ui.show("name");
                ui.show("arm");
                ui.show("vol");
            }
        },
        uisys::ButtonTheme::Military());
}

void App::renderAbout(int sw, int sh) {
    gfx.setTextColor(gfx.color565(0xFF, 0xFF, 0xFF));
    gfx.setTextSize(4);
    gfx.setCursor(20, sh-150);
    gfx.writeText("MFoES for RPI0w2");
    gfx.setCursor(20, sh-100);
    gfx.setTextSize(3);
    gfx.writeText("a Mounted Family of Embedded System");
    gfx.setCursor(20, sh-50);
    gfx.setTextSize(2);
    gfx.writeText("by DPSoftware Technologies (PlatoonLabs)");

    gfx.setCursor(720, sh-135);
    gfx.setTextSize(2);
    gfx.writeText("Adafruit GFX Compatible");
    gfx.drawBitmap(795, sh-100, adaf_logo_bmp, 115, 32, gfx.color565(0xFF,0xFF,0xFF));
}