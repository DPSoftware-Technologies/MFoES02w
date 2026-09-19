#include "app.h"
#include <chrono>

// =============================================================================
// Frame planning
// =============================================================================
//
// The redraw flags are level-triggered and several of them are consumed by
// reading them. The frame is therefore planned once, up front, and the flags
// are cleared once in render() — never inside drawFrame(), which runs twice
// when a remote renderer is attached.
//
App::RenderPlan App::planFrame(bool forceRender) {
    RenderPlan p;

    const bool redraw = ui.needsRedraw();

    p.clear = forceRender || RRFDTS || redraw || show_data_in || show_about || RRFSYSMSG;

    bool nr = false;

    p.dts = (RRFDTS || forceRender);
    if (p.dts) nr = true;

    p.dataIn = show_data_in;
    if (p.dataIn) nr = true;

    p.about = show_about;
    if (p.about) nr = true;

    p.sysInfo = ui.getButton("ssui")->isToggleOn()
             && (RRFSYSINFO || redraw || nr || forceRender);
    if (p.sysInfo) nr = true;

    // Only ask for another redraw when the system info actually changed,
    // rather than every frame.
    p.askRedraw = p.sysInfo && RRFSYSINFO;

    p.widgets = !hide_ui && (RRFSYSMSG || redraw || nr || forceRender);
    if (p.widgets) nr = true;

    p.status = RRFSYSMSG || forceRender || nr;
    if (p.status) nr = true;

    p.any = forceRender || nr;
    return p;
}

// =============================================================================
// The frame itself
// =============================================================================

template<typename G>
void App::drawFrame(G& g, const RenderPlan& plan) {
    if (plan.clear) {
        g.fillScreen(0x000000);
    }

    // application zone
#ifndef DESKTOP
    if (plan.dts && hasFrame) {
        int ox = (sw - SCREEN_W) / 2;
        int oy = (sh - SCREEN_H) / 2;
        // Drawn on the recorder too, but the record mask drops RGB565 bitmaps,
        // so this costs the remote stream nothing.
        g.drawRGB565Bitmap(ox, oy, frameBufB, SCREEN_W, SCREEN_H);
    }
#endif

    // user zone
    if (plan.dataIn) renderDataInInfo(g);
    if (plan.about)  renderAbout(g);

    // interactive UI zone
    if (plan.sysInfo) renderInfo(g);
    if (plan.widgets) ui.draw(g);

    // system / debug
    if (plan.status) {
        g.setCursor(10, 10);
        g.setTextColor(0xFFFFFFFF);
        g.setTextSize(1);
        pthread_mutex_lock(&frameMutex);
        g.writeText(statusMsg);
        pthread_mutex_unlock(&frameMutex);
    }
}

void App::render(bool forceRender) {
    // designed for saving energy

    forceRender = RRFF;
    RRFF = false; // reset state

    const RenderPlan plan = planFrame(forceRender);

    // Local pass — unchanged behaviour, straight at the panel.
    drawFrame(gfx, plan);

    // Remote pass — same plan, recorded as commands instead of pixels.
    // Skipped entirely when nobody is attached.
    if (plan.any && drRemoteAttached()) {
        dr.DRFlush();

        // The panel's draw buffer is wiped by swapBuffers()'s autoclear, so
        // every local frame starts black and the plan only has to paint what
        // is visible. A remote renderer has no such buffer behind it, so the
        // clear has to be stated in the stream — otherwise it paints each
        // frame on top of the last and the views pile up.
        RenderPlan rplan = plan;
        rplan.clear = true;

        drawFrame(dr, rplan);

        drBlob.clear();
        dr.DRExport(drBlob);
        drPublish(drBlob);
    }

    // Latched flags are cleared once, after both passes have run.
    if (plan.dts)     RRFDTS = false;
    if (plan.sysInfo) RRFSYSINFO = false;
    if (plan.status)  RRFSYSMSG = false;
    if (plan.askRedraw) ui.requestRedraw();

    // render
    if (plan.any) {
        gfx.swapBuffers();
    }
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

template<typename G>
void App::renderAbout(G& g) {
    g.setTextColor(0xFFFFFFFF);
    g.setTextSize(4);
    g.setCursor(20, sh-150);
    g.writeText("MFoES for RPI0w2");
    g.setCursor(20, sh-100);
    g.setTextSize(3);
    g.writeText("a Mounted Family of Embedded System");
    g.setCursor(20, sh-50);
    g.setTextSize(2);
    g.writeText("DPSoftware Technologies (PlatoonLabs)");

    g.setCursor(720, sh-135);
    g.setTextSize(2);
    g.writeText("Adafruit GFX Compatible");
    g.drawBitmap(795, sh-100, adaf_logo_bmp, 115, 32, 0xFFFFFFFFu);
}

template<typename G>
void App::renderDataInInfo(G& g) {
    g.setTextColor(0xFFFFFFFF);
    g.setTextSize(2);

    for (int i = 0; i < 8; i++) {
        g.setCursor(20, 50 + (i * 15));
        g.writeTextF("CV%d: %u", i + 1, cvdata.v[i]);
    }
}

template<typename G>
void App::renderInfo(G& g) {
    std::vector<float> coreUsages = linfo.getAllCoreUsages();
    float coreLoadAVG = linfo.getCPUUsage();
    float cpuTemp = linfo.getCPUTemp();
    float cpuClock = linfo.getCPUClock();
    long ramTotal = linfo.getRAMTotal();
    long ramUsed = linfo.getRAMUsed();
    long cmaTotal = linfo.getCMATotal();
    long cmaUsed = linfo.getCMAUsed();

    drawBarContainer(g, 20, 100, 100, 20, 0.0f, 100.0f, coreUsages, "PLoads", false, 0xFF00FF00, InfoBarThresholdsColors);
    drawBar(g, 180, 100, 100, 20, coreLoadAVG, 0.0f, 100.0f, "PAvg", false, false, 0xFF00FF00, InfoBarThresholdsColors);
    drawBar(g, 20, 140, 100, 20, cpuTemp, 0.0f, 85.0f, "PTemp", false, false, 0xFF00FF00, InfoBarThresholdsColors);
    drawBar(g, 180, 140, 100, 20, cpuClock, 0.0f, 1300.0f, "PClock", false, false, 0xFF00FF00, InfoBarThresholdsColors);
    drawBar(g, 20, 180, 100, 20, (float)ramUsed, 0.0f, (float)ramTotal, "RAM", false, false, 0xFF00FF00, InfoBarThresholdsColors);
    drawBar(g, 180, 180, 100, 20, (float)cmaUsed, 0.0f, (float)cmaTotal, "CMA", false, false, 0xFF00FF00, InfoBarThresholdsColors);
}
