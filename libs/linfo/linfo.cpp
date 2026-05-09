#include "linfo.h"

#include <fstream>
#include <sstream>

Linfo::Linfo() {
    cpuUsage = 0.0f;
    cpuTemp = 0.0f;
    cpuClock = 0.0f;

    ramTotal = 0;
    ramAvailable = 0;
    ramUsed = 0;

    cmaTotal = 0;
    cmaAvailable = 0;
    cmaUsed = 0;

    update();
}

Linfo::~Linfo() {}

void Linfo::update() {
    updateCPUUsage();
    updateCPUTemp();
    updateCPUClock();
    updateRAM();
}

void Linfo::updateCPUUsage() {
    std::ifstream file("/proc/stat");

    std::string line;

    int coreIndex = -1;

    while (std::getline(file, line)) {
        if (line.rfind("cpu", 0) != 0)
            break;

        std::stringstream ss(line);

        std::string cpuLabel;

        long user, nice, system, idle;
        long iowait, irq, softirq, steal;

        ss >> cpuLabel
           >> user
           >> nice
           >> system
           >> idle
           >> iowait
           >> irq
           >> softirq
           >> steal;

        long idleTime = idle + iowait;

        long totalTime =
            user +
            nice +
            system +
            idle +
            iowait +
            irq +
            softirq +
            steal;

        // Average CPU
        if (cpuLabel == "cpu") {
            long totalDiff =
                totalTime - prevCPU.total;

            long idleDiff =
                idleTime - prevCPU.idle;

            if (totalDiff > 0) {
                cpuUsage =
                    100.0f *
                    (1.0f -
                    ((float)idleDiff / totalDiff));
            }

            prevCPU.total = totalTime;
            prevCPU.idle = idleTime;
        }
        else {
            coreIndex++;

            if ((int)prevCoreData.size() <= coreIndex) {
                prevCoreData.push_back(
                    {idleTime, totalTime}
                );

                coreUsages.push_back(0.0f);

                continue;
            }

            long totalDiff =
                totalTime -
                prevCoreData[coreIndex].total;

            long idleDiff =
                idleTime -
                prevCoreData[coreIndex].idle;

            if (totalDiff > 0) {
                coreUsages[coreIndex] =
                    100.0f *
                    (1.0f -
                    ((float)idleDiff / totalDiff));
            }

            prevCoreData[coreIndex].total =
                totalTime;

            prevCoreData[coreIndex].idle =
                idleTime;
        }
    }
}

void Linfo::updateCPUTemp() {
    std::ifstream file("/sys/class/thermal/thermal_zone0/temp");

    int temp = 0;

    if (file.is_open()) {
        file >> temp;
        cpuTemp = temp / 1000.0f;
    }
}

void Linfo::updateCPUClock() {
    std::ifstream file("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq");

    int freq = 0;

    if (file.is_open()) {
        file >> freq;
        cpuClock = freq / 1000.0f;
    }
}

void Linfo::updateRAM() {
    std::ifstream file("/proc/meminfo");

    std::string key;
    long value;
    std::string unit;

    while (file >> key >> value >> unit) {
        if (key == "MemTotal:") {
            ramTotal = value;
        }
        if (key == "MemAvailable:") {
            ramAvailable = value;
        }
        if (key == "CmaTotal:") {
            cmaTotal = value;
        }
        if (key == "CmaFree:") {
            cmaAvailable = value;
        }
            
    }

    ramUsed = ramTotal - ramAvailable;
    cmaUsed = cmaTotal - cmaAvailable;
}

float Linfo::getCPUUsage() const {
    return cpuUsage;
}

float Linfo::getCoreUsage(int core) const {
    if (core < 0 || core >= (int)coreUsages.size())
        return 0.0f;

    return coreUsages[core];
}

std::vector<float> Linfo::getAllCoreUsages() const {
    return coreUsages;
}

int Linfo::getCoreCount() const {
    return coreUsages.size();
}

float Linfo::getCPUTemp() const {
    return cpuTemp;
}

float Linfo::getCPUClock() const {
    return cpuClock;
}

long Linfo::getRAMTotal() const {
    return ramTotal;
}

long Linfo::getRAMAvailable() const {
    return ramAvailable;
}

long Linfo::getRAMUsed() const {
    return ramUsed;
}

long Linfo::getCMATotal() const {
    return cmaTotal;
}

long Linfo::getCMAAvailable() const {
    return cmaAvailable;
}

long Linfo::getCMAUsed() const {
    return cmaUsed;
}
