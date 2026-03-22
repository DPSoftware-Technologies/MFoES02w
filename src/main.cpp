#include <cstdlib>
#include "app.h"

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    App app;
    app.init();

    int code = app.run();

    // destroy
    delete &app;

    return code;
}
