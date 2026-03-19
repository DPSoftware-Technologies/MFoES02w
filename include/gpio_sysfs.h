#pragma once
#include <fstream>
#include <string>
#include <stdexcept>
#include <unistd.h>
#include <sys/stat.h>

class GpioPin {
    int absPin;
    std::string base;

    void writeFile(const std::string& path, const std::string& val) {
        std::ofstream f(path);
        if (!f) throw std::runtime_error("Cannot write: " + path);
        f << val;
        f.flush();
    }

    bool pathExists(const std::string& path) {
        struct stat st;
        return stat(path.c_str(), &st) == 0;
    }

public:
    GpioPin(int pin, bool output, int chip_offset = 512)
        : absPin(chip_offset + pin),
          base("/sys/class/gpio/gpio" + std::to_string(chip_offset + pin))
    {
        // Only export if not already exported
        if (!pathExists(base)) {
            std::ofstream exp("/sys/class/gpio/export");
            if (!exp) throw std::runtime_error("Cannot open gpio export");
            exp << absPin;
            exp.flush();

            // Wait for sysfs entry
            for (int i = 0; i < 100; i++) {
                if (pathExists(base + "/direction")) break;
                usleep(10000);
                if (i == 99) throw std::runtime_error("Timeout gpio" + std::to_string(absPin));
            }
        }

        writeFile(base + "/direction", output ? "out" : "in");
    }

    void set(int val) { writeFile(base + "/value", std::to_string(val)); }

    int get() {
        std::ifstream f(base + "/value");
        if (!f) throw std::runtime_error("Cannot read: " + base + "/value");
        int v; f >> v;
        return v;
    }

    ~GpioPin() {
        std::ofstream unexp("/sys/class/gpio/unexport");
        unexp << absPin;
    }
};