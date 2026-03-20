#ifndef OUTGAUGE_H
#define OUTGAUGE_H

#include <stdint.h>

#pragma pack(push, 1)

struct OutGaugeCarFlags {
    uint16_t raw;
    bool showTurbo() const { return raw & (1 << 0); }
    bool showKM()    const { return raw & (1 << 1); }
    bool showBAR()   const { return raw & (1 << 2); }
};
static_assert(sizeof(OutGaugeCarFlags) == 2, "flags size");

struct OutGaugeCarLights {
    uint16_t raw;
    bool shift_light()   const { return raw & (1 << 0);  }
    bool full_beam()     const { return raw & (1 << 1);  }
    bool handbrake()     const { return raw & (1 << 2);  }
    bool pit_limiter()   const { return raw & (1 << 3);  }
    bool tc()            const { return raw & (1 << 4);  }
    bool left_turn()     const { return raw & (1 << 5);  }
    bool right_turn()    const { return raw & (1 << 6);  }
    bool both_turns()    const { return raw & (1 << 7);  }
    bool oil_warn()      const { return raw & (1 << 8);  }
    bool battery_warn()  const { return raw & (1 << 9);  }
    bool abs()           const { return raw & (1 << 10); }
    bool spare_light()   const { return raw & (1 << 11); }
};
static_assert(sizeof(OutGaugeCarLights) == 2, "lights size");

struct OutGaugeData {
    uint32_t          time;           // 4  → total: 4
    char              carName[4];     // 4  → total: 8
    OutGaugeCarFlags  flags;          // 2  → total: 10
    int8_t            gear;           // 1  → total: 11
    uint8_t           PLID;           // 1  → total: 12
    float             speed;          // 4  → total: 16
    float             rpm;            // 4  → total: 20
    float             turboPressure;  // 4  → total: 24
    float             engTemp;        // 4  → total: 28
    float             fuel;           // 4  → total: 32
    float             oilPressure;    // 4  → total: 36
    float             oilTemp;        // 4  → total: 40
    OutGaugeCarLights lights;         // 2  → total: 42
    float             throttle;       // 4  → total: 46
    float             brake;          // 4  → total: 50
    float             clutch;         // 4  → total: 54
    char              misc1[16];      // 16 → total: 70
    char              misc2[16];      // 16 → total: 86
    double            timestamp;      // 8  → total: 94
};
// total = 94, not 100 — I miscounted earlier!

#pragma pack(pop)

static_assert(sizeof(OutGaugeData) == 94, "OutGaugeData size mismatch!");

#endif // OUTGAUGE_H