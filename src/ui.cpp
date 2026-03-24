#include "app.h"

void App::render(bool forceRender) {
    // designed for saving energy

    bool needRender = false;

    if (forceRender || RRFDTS || ui.needsRedraw() || show_data_in || show_about || RRFSYSMSG) {
        gfx.fillScreen(0x000000);
    }

    // application zone
    if (RRFDTS || forceRender) {
        if (hasFrame) {
            int ox = (sw - SCREEN_W) / 2;
            int oy = (sh - SCREEN_H) / 2;
            gfx.drawRGB565Bitmap(ox, oy, frameBufB, SCREEN_W, SCREEN_H);
        }

        RRFDTS = false;
        needRender = true;
    }
    
    // user zone
    if (show_data_in) {
        renderDataInInfo();
        needRender = true;
    }

    if (show_about) {
        renderAbout(); 
        needRender = true;
    }

    // interactive UI zone
    if (!hide_ui && (ui.needsRedraw() || forceRender || needRender || RRFSYSMSG)) {
        ui.draw(gfx); 
        needRender = true;
    }

    // system 
    // debug
    if (RRFSYSMSG || forceRender || needRender) {
        gfx.setCursor(10, 10);
        gfx.setTextColor(0xFFFFFFFF);
        gfx.setTextSize(1);
        pthread_mutex_lock(&frameMutex);
        gfx.writeText(statusMsg);
        pthread_mutex_unlock(&frameMutex);

        RRFSYSMSG = false;
        needRender = true;
    }

    // render
    if (forceRender || needRender) { 
        gfx.swapBuffers();
        needRender = false;
    };
}

void App::initSysUI() {
    ui.addButton("quit", 50, 50, 180, 60, "Exit MFD", uisys::ButtonMode::TRIGGER,
        [this](int s){
            defer([this]{
                int r = ui.quickFireDialog(SCREEN_W, SCREEN_H,
                    "Exit the MFD",
                    "Sure to stop this MFD app? ",
                    uisys::DialogMode::YesNo,
                    uisys::DialogIcon::Warning,
                    0, false, "No", "Yes");
                ui.getButton("quit")->setPressed(false);
                ui.requestRedraw();
                if (r == uisys::DialogResult::No) {
                    stop();
                }
            });
        },
        uisys::ButtonTheme::Danger());

    ui.addButton("halt", 250, 50, 180, 60, "Shutdown", uisys::ButtonMode::TRIGGER,
        [this](int s){
            defer([this]{
                int r = ui.quickFireDialog(SCREEN_W, SCREEN_H,
                    "Shutdown",
                    "Sure to shutdown? For power up, short GPIO3 to GND.",
                    uisys::DialogMode::YesNo,
                    uisys::DialogIcon::Warning,
                    0, false, "No", "Yes");
                ui.getButton("halt")->setPressed(false);
                ui.requestRedraw();
                if (r == uisys::DialogResult::No) {
                    auto& dlg = ui.addDialog("__halt__", SCREEN_W, SCREEN_H,
                        "Shutting down...", "The system will power off now.",
                        uisys::DialogMode::Notice,
                        uisys::DialogIcon::Info);
                    dlg.hideButtons();
                    dlg.fire();
                    render(true);
                    system("halt");
                }
            });
        },
        uisys::ButtonTheme::Danger());

    ui.addButton("hideui", 450, 50, 180, 60, "Hide All UI", uisys::ButtonMode::TRIGGER,
        [this](int s){
            defer([this]{
                int r = ui.quickFireDialog(SCREEN_W, SCREEN_H,
                    "Hide UI",
                    "Sure to Hide UI? You can't restore the UI back until restart.",
                    uisys::DialogMode::YesNo,
                    uisys::DialogIcon::Warning,
                    0, false, "No", "Yes");
                ui.getButton("hideui")->setPressed(false);
                ui.requestRedraw();
                if (r == uisys::DialogResult::No) {
                    show_about = false;
                    hide_ui = true;
                }
            });
        },
        uisys::ButtonTheme::Danger());
    
    ui.addButton("restart", 650, 50, 180, 60, "Restart", uisys::ButtonMode::TRIGGER,
        [this](int s){
            defer([this]{
                int r = ui.quickFireDialog(SCREEN_W, SCREEN_H,
                    "Restart",
                    "Sure to restart?",
                    uisys::DialogMode::YesNo,
                    uisys::DialogIcon::Warning,
                    0, false, "No", "Yes");
                ui.getButton("restart")->setPressed(false);
                ui.requestRedraw();
                if (r == uisys::DialogResult::No) {
                    auto& dlg = ui.addDialog("__rebooting__", SCREEN_W, SCREEN_H,
                        "Restarting...", "The system will restart now now.",
                        uisys::DialogMode::Notice,
                        uisys::DialogIcon::Info);
                    dlg.hideButtons();
                    dlg.fire();
                    render(true);
                    system("reboot");
                }
            });
        },
        uisys::ButtonTheme::Danger());

    ui.getButton("quit")->setVisible(false);
    ui.getButton("halt")->setVisible(false);
    ui.getButton("hideui")->setVisible(false);
    ui.getButton("restart")->setVisible(false);
}

void App::initDemoUI() {
    ui.addButton("fire",  50,  600, 180, 60, "FIRE",  uisys::ButtonMode::TRIGGER,
    [this](int s){ buz.set(1); usleep(50000); buz.set(0); });

    ui.addButton("arm",   250, 600, 180, 60, "ARM",   uisys::ButtonMode::TOGGLE,
        [this](int s){  },
        uisys::ButtonTheme::Military());
    
    ui.addButton("boost", 450, 600, 180, 60, "BOOST", uisys::ButtonMode::HOLD,
        [this](int s){
            buz.set((s == 3) ? 1 : 0); 
        },
        uisys::ButtonTheme::HUD());

    ui.addButton("testbtn1", 650, 600, 180, 60, "Test",   uisys::ButtonMode::HOLD_SWIPE,
        [this](int s){  },
        uisys::ButtonTheme::Military());

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
    ui.hide("testbtn1");
    ui.getComboBox("mode")->setVisible(false);
    ui.getSpinBox("speed")->setVisible(false);
    ui.getSpinBox("gain2")->setVisible(false);
}
void App::initSidebarBTNs() {
    ui.addButton("sdata", 1075, 360, 180, 50, "Show Data", uisys::ButtonMode::TOGGLE,
        [this](int s){
            show_data_in = (s == 1) ? true : false;
        },
        uisys::ButtonTheme::Military());

    ui.addButton("sinfo", 1075, 440, 180, 50, "Show Info", uisys::ButtonMode::TOGGLE,
        [this](int s){
            show_about = (s == 1) ? true : false;
        },
        uisys::ButtonTheme::Military());

    ui.addButton("ssui", 1075, 520, 180, 50, "Show System UI", uisys::ButtonMode::TOGGLE,
        [this](int s){
            bool state = (s == 1) ? true : false;

            ui.getButton("quit")->setVisible(state);
            ui.getButton("halt")->setVisible(state);
            ui.getButton("hideui")->setVisible(state);
            ui.getButton("restart")->setVisible(state);
        },
        uisys::ButtonTheme::Military());

    ui.addButton("sdui", 1075, 600, 180, 50, "Show Demo UI", uisys::ButtonMode::TOGGLE,
        [this](int s){
            bool state = (s == 1) ? true : false;
            ui.getComboBox("mode")->setVisible(state);
            ui.getSpinBox("speed")->setVisible(state);
            ui.getSpinBox("gain2")->setVisible(state);
            if (!state) {
                ui.hide("fire");
                ui.hide("boost");
                ui.hide("gain");
                ui.hide("freq");
                ui.hide("ip");
                ui.hide("port");
                ui.hide("name");
                ui.hide("arm");
                ui.hide("vol");
                ui.hide("testbtn1");
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
                ui.show("testbtn1");
            }
        },
        uisys::ButtonTheme::Military());
}

void App::renderAbout() {
    gfx.setTextColor(0xFFFFFFFF);
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
    gfx.drawBitmap(795, sh-100, adaf_logo_bmp, 115, 32, 0xFFFFFFFFu);
}

void App::drawGauge(int x, int y, int r, float value, float minVal, float maxVal) {
    float angleDegrees = (value - minVal) * (180.0 - 0.0) / (maxVal - minVal);
    
    angleDegrees = 180.0 - angleDegrees; 

    float angleRad = angleDegrees * (3.14159265 / 180.0);

    // Use drawCircle instead of the protected drawCircleHelper
    gfx.drawCircle(x, y, r, GFX_WHITE);

    int xEnd = x + cos(angleRad) * (r - 5);
    int yEnd = y - sin(angleRad) * (r - 5);

    gfx.drawLine(x, y, xEnd, yEnd, GFX_WHITE);
}


void App::renderDataInInfo() {
    gfx.setTextColor(0xFFFFFFFF);
    gfx.setTextSize(2);
    
    uint16_t values[8];

    values[0] = cvdata.v1;
    values[1] = cvdata.v2;
    values[2] = cvdata.v3;
    values[3] = cvdata.v4;
    values[4] = cvdata.v5;
    values[5] = cvdata.v6;
    values[6] = cvdata.v7;
    values[7] = cvdata.v8;

    for (int i = 0; i < 8; i++) {
        gfx.setCursor(20, 50 + (i * 15));
        gfx.writeTextF("CV%d: %u", i + 1, values[i]);
    }

    drawGauge(50, 200, 10, cvdata.v1, 0, 100);
}
