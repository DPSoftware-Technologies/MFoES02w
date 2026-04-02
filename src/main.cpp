#include <cstdlib>
#include <signal.h>
#include "app.h"

App* g_app_ptr = nullptr;

void signalHandler(int signal) {
    if (g_app_ptr) {
        g_app_ptr->stop();
    }
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    App app;
    g_app_ptr = &app;

    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    app.init();
    int code = app.run();
    
    // destroy
    delete &app;

    return code;
}