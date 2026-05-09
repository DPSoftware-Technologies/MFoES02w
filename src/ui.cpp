#include "app.h"
#include <chrono>

void App::render(bool forceRender) {
    // designed for saving energy
    
    forceRender = RRFF;
    RRFF = false; // reset state
    
    bool needRender = false;

    if (forceRender || RRFDTS || ui.needsRedraw() || show_data_in || show_about || RRFSYSMSG) {
        gfx.fillScreen(0x000000);
    }

    // application zone
#ifndef DESKTOP
    if (RRFDTS || forceRender) {
        if (hasFrame) {
            int ox = (sw - SCREEN_W) / 2;
            int oy = (sh - SCREEN_H) / 2;
            gfx.drawRGB565Bitmap(ox, oy, frameBufB, SCREEN_W, SCREEN_H);
        }

        RRFDTS = false;
        needRender = true;
    }
#endif
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
    if (ui.getButton("ssui")->isToggleOn() && (RRFSYSINFO || ui.needsRedraw() || needRender || forceRender)) {
        renderInfo();
        // Only request redraw if system info flag was set (content changed), not every frame
        if (RRFSYSINFO) {
            ui.requestRedraw();
        }

        RRFSYSINFO = false;
        needRender = true;
    }
    
    if (!hide_ui && (RRFSYSMSG || ui.needsRedraw() || needRender || forceRender)) {
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
    ui.addButton("quit", 20, 20, 180, 60, "Exit MFD", uisys::ButtonMode::TRIGGER,
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

    ui.addButton("halt", 210, 20, 180, 60, "Shutdown", uisys::ButtonMode::TRIGGER,
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
                    ostop(false);
                }
            });
        },
        uisys::ButtonTheme::Danger());

    ui.addButton("hideui", 400, 20, 180, 60, "Hide All UI", uisys::ButtonMode::TRIGGER,
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
    
    ui.addButton("restart", 590, 20, 180, 60, "Restart", uisys::ButtonMode::TRIGGER,
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
                    ostop(true);
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
    [this](int s){ 
#ifndef DESKTOP
        buz.set(1); 
        usleep(50000); 
        buz.set(0); 
#endif
    });

    ui.addButton("arm",   250, 600, 180, 60, "ARM",   uisys::ButtonMode::TOGGLE,
        [this](int s){  },
        uisys::ButtonTheme::Military());
    
    ui.addButton("boost", 450, 600, 180, 60, "BOOST", uisys::ButtonMode::HOLD,
        [this](int s){
#ifndef DESKTOP
            buz.set((s == 3) ? 1 : 0); 
#endif
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

void App::setView(std::string view) {
    // 1. Reset everything to a clean slate
    show_data_in = (view == "data");
    show_about = (view == "info");

    ui.getButton("sdata")->setToggleState(view == "data");
    ui.getButton("sinfo")->setToggleState(view == "info");
    ui.getButton("ssui")->setToggleState(view == "system");
    ui.getButton("sdui")->setToggleState(view == "demo");

    // 2. Group visibility
    bool isSys = (view == "system");
    ui.getButton("quit")->setVisible(isSys);
    ui.getButton("halt")->setVisible(isSys);
    ui.getButton("hideui")->setVisible(isSys);
    ui.getButton("restart")->setVisible(isSys);

    bool isDemo = (view == "demo");
    std::vector<std::string> demoElements = {"fire", "boost", "gain", "freq", "ip", "port", "name", "arm", "vol", "testbtn1"};
    for (const auto& id : demoElements) {
        isDemo ? ui.show(id) : ui.hide(id);
    }
    
    ui.getComboBox("mode")->setVisible(isDemo);
    ui.getSpinBox("speed")->setVisible(isDemo);
    ui.getSpinBox("gain2")->setVisible(isDemo);
}

void App::initSidebarBTNs() {
    auto theme = uisys::ButtonTheme::Military();

    ui.addButton("sdata", 1075, 360, 180, 50, "Show Data", uisys::ButtonMode::TOGGLE,
        [this](int s){ if(s) setView("data"); }, theme);

    ui.addButton("sinfo", 1075, 440, 180, 50, "Show Info", uisys::ButtonMode::TOGGLE,
        [this](int s){ if(s) setView("info"); }, theme);

    ui.addButton("ssui", 1075, 520, 180, 50, "Show System UI", uisys::ButtonMode::TOGGLE,
        [this](int s){ if(s) setView("system"); else setView("none"); }, theme);

    ui.addButton("sdui", 1075, 600, 180, 50, "Show Demo UI", uisys::ButtonMode::TOGGLE,
        [this](int s){ if(s) setView("demo"); else setView("none"); }, theme);
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
    gfx.writeText("DPSoftware Technologies (PlatoonLabs)");

    gfx.setCursor(720, sh-135);
    gfx.setTextSize(2);
    gfx.writeText("Adafruit GFX Compatible");
    gfx.drawBitmap(795, sh-100, adaf_logo_bmp, 115, 32, 0xFFFFFFFFu);
}

void App::renderDataInInfo() {
    gfx.setTextColor(0xFFFFFFFF);
    gfx.setTextSize(2);
    
    for (int i = 0; i < 8; i++) {
        gfx.setCursor(20, 50 + (i * 15));
        gfx.writeTextF("CV%d: %u", i + 1, cvdata.v[i]);
    }
}

void App::renderInfo() {
    std::vector<float> coreUsages = linfo.getAllCoreUsages();
    float coreLoadAVG = linfo.getCPUUsage();
    float cpuTemp = linfo.getCPUTemp();
    float cpuClock = linfo.getCPUClock();
    long ramTotal = linfo.getRAMTotal();
    long ramUsed = linfo.getRAMUsed();
    long cmaTotal = linfo.getCMATotal();
    long cmaUsed = linfo.getCMAUsed();

    drawBarContainer(gfx, 20, 100, 100, 20, 0.0f, 100.0f, coreUsages, "PLoads", false, 0xFF00FF00, InfoBarThresholdsColors);
    drawBar(gfx, 180, 100, 100, 20, coreLoadAVG, 0.0f, 100.0f, "PAvg", false, false, 0xFF00FF00, InfoBarThresholdsColors);
    drawBar(gfx, 20, 140, 100, 20, cpuTemp, 0.0f, 85.0f, "PTemp", false, false, 0xFF00FF00, InfoBarThresholdsColors);
    drawBar(gfx, 180, 140, 100, 20, cpuClock, 0.0f, 1300.0f, "PClock", false, false, 0xFF00FF00, InfoBarThresholdsColors);
    drawBar(gfx, 20, 180, 100, 20, (float)ramUsed, 0.0f, (float)ramTotal, "RAM", false, false, 0xFF00FF00, InfoBarThresholdsColors);
    drawBar(gfx, 180, 180, 100, 20, (float)cmaUsed, 0.0f, (float)cmaTotal, "CMA", false, false, 0xFF00FF00, InfoBarThresholdsColors);
}