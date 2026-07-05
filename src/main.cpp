#include "app/Application.h"
#include <cstdio>

int main() {
    Application app;
    if (!app.init()) {
        fprintf(stderr, "Application init failed\n");
        return 1;
    }
    app.run();
    app.shutdown();
    return 0;
}
