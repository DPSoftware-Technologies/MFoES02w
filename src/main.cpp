#include <cstdlib>
#include "app.h"

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    App app;
    app.init();

    return app.run();
}
