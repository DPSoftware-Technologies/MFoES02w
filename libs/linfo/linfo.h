#pragma once

#include <vector>
#include <string>

struct CPUCoreData {
    long idle;
    long total;
};

class Linfo {
private:
    // Average CPU
    float cpuUsage;

    // Per-core CPU usage
    std::vector<float> coreUsages;

    // Previous values for calculation
    CPUCoreData prevCPU;
    std::vector<CPUCoreData> prevCoreData;

    // Other telemetry
    float cpuTemp;
    float cpuClock;

    long ramTotal;
    long ramAvailable;
    long ramUsed;

    // CMA memory
    long cmaTotal;
    long cmaAvailable;
    long cmaUsed;

    // Internal update functions
    void updateCPUUsage();
    void updateCPUTemp();
    void updateCPUClock();
    void updateRAM();
public:
    Linfo();
    ~Linfo();

    // Update all telemetry
    void update();

    // Average CPU usage
    float getCPUUsage() const;

    // Individual core usage
    float getCoreUsage(int core) const;

    // Get all core usages
    std::vector<float> getAllCoreUsages() const;

    // Core count
    int getCoreCount() const;

    // Temp / clock
    float getCPUTemp() const;
    float getCPUClock() const;

    // RAM
    long getRAMTotal() const;
    long getRAMAvailable() const;
    long getRAMUsed() const;

    // CMA
    long getCMATotal() const;
    long getCMAAvailable() const;
    long getCMAUsed() const;
};