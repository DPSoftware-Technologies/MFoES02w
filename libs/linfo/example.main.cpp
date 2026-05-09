#include <iostream>
#include <thread>
#include <chrono>

#include "linfo.h"

int main() {
    Linfo sys;

    while (true) {
        sys.update();

        std::cout
            << "AVG CPU: "
            << sys.getCPUUsage()
            << "%\n";

        for (int i = 0; i < sys.getCoreCount(); i++) {
            std::cout
                << "Core "
                << i
                << ": "
                << sys.getCoreUsage(i)
                << "%\n";
        }

        std::cout
            << "Temp: "
            << sys.getCPUTemp()
            << " C\n";

        std::cout
            << "Clock: "
            << sys.getCPUClock()
            << " MHz\n";

        std::cout
            << "RAM Used: "
            << sys.getRAMUsed() / 1024
            << " MB\n";

        std::cout
            << "------------------\n";

        std::this_thread::sleep_for(
            std::chrono::seconds(1)
        );
    }

    return 0;
}