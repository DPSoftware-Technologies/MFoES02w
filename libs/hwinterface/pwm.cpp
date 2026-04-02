#include "pwm.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <cmath>
#include <unistd.h>

// 
// Helpers
// 

static std::string buildChipPath(int chip) {
    return "/sys/class/pwm/pwmchip" + std::to_string(chip);
}

static std::string buildChanPath(int chip, int channel) {
    return buildChipPath(chip) + "/pwm" + std::to_string(channel);
}

// 
// Constructor / Destructor
// 

PWM::PWM(int chip, int channel)
    : m_chip(chip), m_channel(channel), m_exported(false),
      m_chipPath(buildChipPath(chip)),
      m_chanPath(buildChanPath(chip, channel))
{
    doExport();
}

PWM::~PWM() {
    // best-effort cleanup
    try { disable();  } catch (...) {}
    try { unexport(); } catch (...) {}
}

// 
// Private helpers
// 

void PWM::doExport() {
    // Check if already exported
    std::ifstream test(m_chanPath + "/period");
    if (test.good()) {
        m_exported = true;
        return;
    }

    std::ofstream exp(m_chipPath + "/export");
    if (!exp.is_open())
        throw std::runtime_error("PWM: cannot open export for pwmchip" +
                                 std::to_string(m_chip) +
                                 " — is pwm-bcm2835 loaded?");
    exp << m_channel;
    exp.close();

    // Give sysfs a moment to create the channel directory
    usleep(100000); // 100ms

    m_exported = true;
}

void PWM::writeAttr(const std::string& attr, const std::string& value) const {
    std::ofstream f(m_chanPath + "/" + attr);
    if (!f.is_open())
        throw std::runtime_error("PWM: cannot write attr '" + attr + "'");
    f << value;
}

std::string PWM::readAttr(const std::string& attr) const {
    std::ifstream f(m_chanPath + "/" + attr);
    if (!f.is_open())
        throw std::runtime_error("PWM: cannot read attr '" + attr + "'");
    std::string val;
    std::getline(f, val);
    return val;
}

// 
// Period
// 

void PWM::setPeriodNs(uint32_t ns) {
    writeAttr("period", std::to_string(ns));
}

uint32_t PWM::getPeriodNs() const {
    return static_cast<uint32_t>(std::stoul(readAttr("period")));
}

// 
// Frequency
// 

void PWM::setFrequencyHz(float hz) {
    if (hz <= 0.0f)
        throw std::invalid_argument("PWM: frequency must be > 0 Hz");

    // Preserve current duty cycle ratio before changing period
    float dutyRatio = 0.0f;
    uint32_t oldPeriod = getPeriodNs();
    if (oldPeriod > 0)
        dutyRatio = static_cast<float>(getDutyCycleNs()) / static_cast<float>(oldPeriod);

    uint32_t newPeriodNs = static_cast<uint32_t>(std::round(1.0e9f / hz));

    // sysfs constraint: duty_cycle must always be <= period.
    // When increasing period, set period first.
    // When decreasing period, set duty first.
    uint32_t newDutyNs = static_cast<uint32_t>(std::round(newPeriodNs * dutyRatio));

    if (newPeriodNs >= oldPeriod) {
        writeAttr("period",     std::to_string(newPeriodNs));
        writeAttr("duty_cycle", std::to_string(newDutyNs));
    } else {
        writeAttr("duty_cycle", std::to_string(newDutyNs));
        writeAttr("period",     std::to_string(newPeriodNs));
    }
}

void PWM::setFrequencyHz(float hz, float dutyCyclePercent) {
    if (hz <= 0.0f)
        throw std::invalid_argument("PWM: frequency must be > 0 Hz");
    if (dutyCyclePercent < 0.0f || dutyCyclePercent > 100.0f)
        throw std::invalid_argument("PWM: dutyCyclePercent must be 0.0 - 100.0");

    uint32_t newPeriodNs = static_cast<uint32_t>(std::round(1.0e9f / hz));
    uint32_t newDutyNs   = static_cast<uint32_t>(std::round(newPeriodNs * (dutyCyclePercent / 100.0f)));
    uint32_t oldPeriod   = getPeriodNs();

    if (newPeriodNs >= oldPeriod) {
        writeAttr("period",     std::to_string(newPeriodNs));
        writeAttr("duty_cycle", std::to_string(newDutyNs));
    } else {
        writeAttr("duty_cycle", std::to_string(newDutyNs));
        writeAttr("period",     std::to_string(newPeriodNs));
    }
}

float PWM::getFrequencyHz() const {
    uint32_t period = getPeriodNs();
    if (period == 0) return 0.0f;
    return 1.0e9f / static_cast<float>(period);
}

// 
// Duty Cycle
// 

void PWM::setDutyCycleNs(uint32_t ns) {
    writeAttr("duty_cycle", std::to_string(ns));
}

uint32_t PWM::getDutyCycleNs() const {
    return static_cast<uint32_t>(std::stoul(readAttr("duty_cycle")));
}

void PWM::setDutyCyclePercent(float percent) {
    if (percent < 0.0f || percent > 100.0f)
        throw std::invalid_argument("PWM: percent must be 0.0 - 100.0");
    uint32_t period = getPeriodNs();
    uint32_t duty   = static_cast<uint32_t>(period * (percent / 100.0f));
    setDutyCycleNs(duty);
}

float PWM::getDutyCyclePercent() const {
    uint32_t period = getPeriodNs();
    if (period == 0) return 0.0f;
    return (static_cast<float>(getDutyCycleNs()) / static_cast<float>(period)) * 100.0f;
}

void PWM::setPulseUs(uint32_t us) {
    setDutyCycleNs(us * 1000u);
}

// 
// Polarity
// 

void PWM::setPolarity(Polarity p) {
    writeAttr("polarity", p == Polarity::INVERSED ? "inversed" : "normal");
}

// 
// Enable / Disable
// 

void PWM::enable() {
    writeAttr("enable", "1");
}

void PWM::disable() {
    writeAttr("enable", "0");
}

bool PWM::isEnabled() const {
    return readAttr("enable") == "1";
}

// 
// Unexport
// 

void PWM::unexport() {
    if (!m_exported) return;
    std::ofstream f(m_chipPath + "/unexport");
    if (!f.is_open()) return; // already gone, don't throw
    f << m_channel;
    m_exported = false;
}