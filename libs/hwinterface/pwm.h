#pragma once

#include <string>
#include <stdexcept>
#include <cstdint>

/**
 * PWM - sysfs-based PWM controller for Linux (BCM2835/RPi)
 *
 * Usage:
 *   PWM pwm(0, 0);                    // chip 0, channel 0
 *
 *   // Period-based
 *   pwm.setPeriodNs(20000000);        // 20ms = 50Hz
 *   pwm.setDutyCycleNs(1500000);
 *
 *   // Frequency-based
 *   pwm.setFrequencyHz(50.0f);        // 50Hz
 *   pwm.setDutyCyclePercent(75.0f);   // 75% duty
 *
 *   // Frequency + duty in one call
 *   pwm.setFrequencyHz(1000.0f, 50.0f);
 *
 *   pwm.enable();
 *   pwm.disable();
 */
class PWM {
public:
    /**
     * @param chip    pwmchip index (usually 0)
     * @param channel PWM channel (0 or 1)
     */
    PWM(int chip, int channel);
    ~PWM();

    // --- Period (raw nanoseconds) ---
    void     setPeriodNs(uint32_t ns);
    uint32_t getPeriodNs() const;

    // --- Frequency (Hz) ---
    /** Set frequency in Hz. Preserves current duty cycle ratio if period was set. */
    void  setFrequencyHz(float hz);

    /** Set frequency and duty cycle together atomically (avoids glitch). */
    void  setFrequencyHz(float hz, float dutyCyclePercent);

    float getFrequencyHz() const;

    // --- Duty cycle ---
    void     setDutyCycleNs(uint32_t ns);
    uint32_t getDutyCycleNs() const;

    /** Set duty cycle as percentage 0.0 - 100.0 of current period. */
    void setDutyCyclePercent(float percent);
    float getDutyCyclePercent() const;

    /** Set pulse width in microseconds (servo-friendly: 1000-2000us). */
    void setPulseUs(uint32_t us);

    // --- Polarity ---
    enum class Polarity { NORMAL, INVERSED };
    void setPolarity(Polarity p);

    // --- Enable / Disable ---
    void enable();
    void disable();
    bool isEnabled() const;

    void set(bool state) { state ? enable() : disable(); }

    // --- Unexport on cleanup ---
    void unexport();

private:
    int    m_chip;
    int    m_channel;
    bool   m_exported;

    std::string m_chipPath;    // /sys/class/pwm/pwmchipN
    std::string m_chanPath;    // /sys/class/pwm/pwmchipN/pwmM

    void doExport();
    void writeAttr(const std::string& attr, const std::string& value) const;
    std::string readAttr(const std::string& attr) const;
};