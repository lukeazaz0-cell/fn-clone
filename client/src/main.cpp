// Storm Island client entry point.
#include "app.h"

int main() {
    si::netInit();
    client::App app;
    app.run();
    return 0;
}
