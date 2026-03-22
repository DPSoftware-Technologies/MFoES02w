#include "gpio_sysfs.h"
#include <fstream>
#include <stdexcept>
#include <unistd.h>
#include <sys/stat.h>

void GpioPin::writeFile(const std::string& path, const std::string& val) {
    std::ofstream f(path);
    if (!f) throw std::runtime_error("Cannot write: " + path);
    f << val;
    f.flush();
}

bool GpioPin::pathExists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

GpioPin::GpioPin(int pin, bool output, int chip_offset)
    : absPin(chip_offset + pin),
      base("/sys/class/gpio/gpio" + std::to_string(chip_offset + pin))
{
    if (!pathExists(base)) {
        std::ofstream exp("/sys/class/gpio/export");
        if (!exp) throw std::runtime_error("Cannot open gpio export");
        exp << absPin;
        exp.flush();

        for (int i = 0; i < 100; i++) {
            if (pathExists(base + "/direction")) break;
            usleep(10000);
            if (i == 99) throw std::runtime_error("Timeout gpio" + std::to_string(absPin));
        }
    }

    writeFile(base + "/direction", output ? "out" : "in");
}

void GpioPin::set(int val) {
    writeFile(base + "/value", std::to_string(val));
}

int GpioPin::get() {
    std::ifstream f(base + "/value");
    if (!f) throw std::runtime_error("Cannot read: " + base + "/value");
    int v; f >> v;
    return v;
}

GpioPin::~GpioPin() {
    std::ofstream unexp("/sys/class/gpio/unexport");
    unexp << absPin;
}
