#include "ds3231.h"

#include "build_time.h"
#include "i2c_bus.h"

namespace {

    enum Reg : uint8_t {
        REG_SECONDS = 0x00,
        REG_MINUTES = 0x01,
        REG_HOURS = 0x02,
        REG_WEEKDAY = 0x03,
        REG_DAY = 0x04,
        REG_MONTH = 0x05, /* bit 7 = century */
        REG_YEAR = 0x06,
        REG_CONTROL = 0x0E, /* bit 7 = EOSC (1 = oscillator off on battery) */
        REG_STATUS = 0x0F,  /* bit 7 = OSF */
        REG_TEMP_MSB = 0x11,
    };

    constexpr uint8_t HOUR_12_MODE = 0x40; /* hours register bit 6 */
    constexpr uint8_t HOUR_PM = 0x20;      /* hours register bit 5, 12h mode only */
    constexpr uint8_t MONTH_CENTURY = 0x80;
    constexpr uint8_t CONTROL_EOSC = 0x80;
    constexpr uint8_t STATUS_OSF = 0x80;

    uint8_t bcd_to_bin(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }

    uint8_t bin_to_bcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

    bool is_leap(uint16_t y) { return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0); }

    uint8_t days_in_month(uint16_t y, uint8_t m) {
        static const uint8_t kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        if (m < 1 || m > 12) {
            return 0;
        }
        if (m == 2 && is_leap(y)) {
            return 29;
        }
        return kDays[m - 1];
    }

    bool time_valid(const Ds3231Time &t) {
        if (t.year < 2000 || t.year > 2199 || t.month < 1 || t.month > 12) {
            return false;
        }
        if (t.day < 1 || t.day > days_in_month(t.year, t.month)) {
            return false;
        }
        return t.hour < 24 && t.minute < 60 && t.second < 60;
    }

} // namespace

uint8_t ds3231_weekday(uint16_t year, uint8_t month, uint8_t day) {
    /* Sakamoto's method. Returns 1 = Sunday to match the DS3231 convention. */
    static const uint8_t kShift[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    uint16_t y = year;
    if (month < 3) {
        --y;
    }
    const uint16_t dow = (uint16_t)((y + y / 4 - y / 100 + y / 400 + kShift[month - 1] + day) % 7);
    return (uint8_t)(dow + 1);
}

bool ds3231_init(TickType_t wait) {
    uint8_t control = 0;
    int rc = i2c_read_reg(DS3231_I2C_ADDR, REG_CONTROL, &control, 1, wait);
    if (rc < 0) {
        return false;
    }

    /* EOSC is inverted: clearing it keeps the oscillator running from VBAT. */
    if (control & CONTROL_EOSC) {
        control &= (uint8_t)~CONTROL_EOSC;
        if (i2c_write_reg(DS3231_I2C_ADDR, REG_CONTROL, &control, 1, wait) < 0) {
            return false;
        }
    }
    return true;
}

int ds3231_get_time(Ds3231Time &out, TickType_t wait) {
    uint8_t raw[7];
    const int rc = i2c_read_reg(DS3231_I2C_ADDR, REG_SECONDS, raw, sizeof(raw), wait);
    if (rc < 0) {
        return rc;
    }

    out.second = bcd_to_bin(raw[0] & 0x7F);
    out.minute = bcd_to_bin(raw[1] & 0x7F);

    if (raw[2] & HOUR_12_MODE) {
        const uint8_t hour12 = bcd_to_bin(raw[2] & 0x1F);
        out.hour = (uint8_t)((hour12 % 12) + ((raw[2] & HOUR_PM) ? 12 : 0));
    } else {
        out.hour = bcd_to_bin(raw[2] & 0x3F);
    }

    out.day = bcd_to_bin(raw[4] & 0x3F);
    out.month = bcd_to_bin(raw[5] & 0x1F);
    out.year = (uint16_t)(2000 + bcd_to_bin(raw[6]) + ((raw[5] & MONTH_CENTURY) ? 100 : 0));

    if (!time_valid(out)) {
        return DS3231_ERR_BAD_TIME;
    }
    out.weekday = ds3231_weekday(out.year, out.month, out.day);
    return 0;
}

int ds3231_set_time(const Ds3231Time &t, TickType_t wait) {
    if (!time_valid(t)) {
        return DS3231_ERR_ARG;
    }

    const uint16_t year_in_century = (uint16_t)(t.year - (t.year >= 2100 ? 2100 : 2000));

    uint8_t raw[7];
    raw[0] = bin_to_bcd(t.second);
    raw[1] = bin_to_bcd(t.minute);
    raw[2] = bin_to_bcd(t.hour); /* bit 6 clear = 24-hour mode */
    raw[3] = ds3231_weekday(t.year, t.month, t.day);
    raw[4] = bin_to_bcd(t.day);
    raw[5] = (uint8_t)(bin_to_bcd(t.month) | (t.year >= 2100 ? MONTH_CENTURY : 0));
    raw[6] = bin_to_bcd((uint8_t)year_in_century);

    return i2c_write_reg(DS3231_I2C_ADDR, REG_SECONDS, raw, sizeof(raw), wait);
}

int ds3231_lost_power(bool &lost, TickType_t wait) {
    uint8_t status = 0;
    const int rc = i2c_read_reg(DS3231_I2C_ADDR, REG_STATUS, &status, 1, wait);
    if (rc < 0) {
        return rc;
    }
    lost = (status & STATUS_OSF) != 0;
    return 0;
}

int ds3231_clear_lost_power(TickType_t wait) {
    uint8_t status = 0;
    int rc = i2c_read_reg(DS3231_I2C_ADDR, REG_STATUS, &status, 1, wait);
    if (rc < 0) {
        return rc;
    }
    status &= (uint8_t)~STATUS_OSF;
    return i2c_write_reg(DS3231_I2C_ADDR, REG_STATUS, &status, 1, wait);
}

int ds3231_temperature_centi(int16_t &centi, TickType_t wait) {
    uint8_t raw[2];
    const int rc = i2c_read_reg(DS3231_I2C_ADDR, REG_TEMP_MSB, raw, sizeof(raw), wait);
    if (rc < 0) {
        return rc;
    }
    /* 10-bit two's complement: signed integer part, then a positive quarter.
     * -0.25 C arrives as 0xFF 0xC0 -> -100 + 75. */
    centi = (int16_t)((int16_t)(int8_t)raw[0] * 100 + (int16_t)((raw[1] >> 6) * 25));
    return 0;
}

Ds3231Time ds3231_build_time(void) {
    const BuildStamp stamp = build_stamp();

    Ds3231Time t{};
    t.year = stamp.year;
    t.month = stamp.month;
    t.day = stamp.day;
    t.hour = stamp.hour;
    t.minute = stamp.minute;
    t.second = stamp.second;
    t.weekday = ds3231_weekday(t.year, t.month, t.day);
    return t;
}

int ds3231_compare(const Ds3231Time &a, const Ds3231Time &b) {
    const uint32_t days_a = (uint32_t)a.year * 10000u + (uint32_t)a.month * 100u + a.day;
    const uint32_t days_b = (uint32_t)b.year * 10000u + (uint32_t)b.month * 100u + b.day;
    if (days_a != days_b) {
        return days_a < days_b ? -1 : 1;
    }

    const uint32_t secs_a = (uint32_t)a.hour * 3600u + (uint32_t)a.minute * 60u + a.second;
    const uint32_t secs_b = (uint32_t)b.hour * 3600u + (uint32_t)b.minute * 60u + b.second;
    if (secs_a != secs_b) {
        return secs_a < secs_b ? -1 : 1;
    }
    return 0;
}

int ds3231_sync_to_build_time(bool force, TickType_t wait) {
    const Ds3231Time reference = ds3231_build_time();

    if (force) {
        const int rc = ds3231_set_time(reference, wait);
        if (rc < 0) {
            return rc;
        }
        ds3231_clear_lost_power(wait);
        return 1;
    }

    bool lost = false;
    int rc = ds3231_lost_power(lost, wait);
    if (rc < 0) {
        return rc; /* bus is down; do not touch the chip */
    }

    Ds3231Time now{};
    const int read_rc = ds3231_get_time(now, wait);
    if (read_rc < 0 && read_rc > DS3231_ERR_BAD_TIME) {
        return read_rc; /* I2C failure, as opposed to garbage registers */
    }

    const bool stale = lost || read_rc == DS3231_ERR_BAD_TIME || ds3231_compare(now, reference) < 0;
    if (!stale) {
        return 0;
    }

    rc = ds3231_set_time(reference, wait);
    if (rc < 0) {
        return rc;
    }
    ds3231_clear_lost_power(wait);
    return 1;
}
