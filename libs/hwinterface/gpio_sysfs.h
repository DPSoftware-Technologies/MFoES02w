#pragma once
#include <string>

class GpioPin {
    int absPin;
    std::string base;

    void writeFile(const std::string& path, const std::string& val);
    bool pathExists(const std::string& path);

public:
    GpioPin(int pin, bool output, int chip_offset = 512);

    void set(int val);
    int  get();

    ~GpioPin();
};
