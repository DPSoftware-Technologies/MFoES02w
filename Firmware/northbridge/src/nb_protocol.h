#pragma once

#include <stdint.h>

/*  Wire contract between the northbridge and the main controller.
 *
 *  Included by BOTH sides -- the firmware and libs/hwinterface on the Pi -- so
 *  a layout can never drift between them. Keep it free of SDK, FreeRTOS and
 *  Linux headers: stdint only.
 *
 *  Structs are packed and use fixed-width fields, so the aarch64 host and the
 *  Cortex-M0+ agree byte for byte without relying on either one's alignment
 *  rules. Add fields at the END of a struct and bump NB_PROTO_VERSION.        */

#define NB_PROTO_VERSION 2

#define NB_FW_VERSION_MAJOR 0
#define NB_FW_VERSION_MINOR 3
#define NB_FW_VERSION_PATCH 0

/*  Frame types.
 *  0x0x = link level, 0x1x = panel, 0x2x = clock, 0x3x = device.             */
#define NB_CMD_PING 0x01      /* echo payload back                            */
#define NB_CMD_PANEL_EVT 0x10 /* unsolicited: an input changed                */
#define NB_CMD_SET_LEDS 0x11  /* set the panel LEDs, no reply                 */
#define NB_CMD_GET_PANEL 0x12 /* -> NbPanelWire                               */
#define NB_CMD_GET_TIME 0x13  /* -> NbTimeWire                                */
#define NB_CMD_SET_TIME 0x14  /* NbTimeWire -> NbStatusWire                   */
#define NB_CMD_GET_INFO 0x15  /* -> NbInfoWire                                */
#define NB_CMD_ENC_EVT 0x16   /* unsolicited: the encoder moved or was pressed*/
#define NB_CMD_GET_ENC 0x17   /* -> NbEncoderWire                             */

/*  NbInfoWire.features                                                       */
#define NB_FEAT_RTC 0x01     /* DS3231 answered at boot        */
#define NB_FEAT_PANEL 0x02   /* PCF8575 expanders came up      */
#define NB_FEAT_ENCODER 0x04 /* rotary encoder is being read   */
#define NB_FEAT_POST_OK 0x08 /* power-on self test passed      */

/*  NbEncoderWire.flags                                                       */
#define NB_ENC_MOVED 0x01  /* delta is a real rotation report */
#define NB_ENC_BUTTON 0x02 /* pressed changed in this event   */

/*  NbStatusWire.code                                                         */
#define NB_OK 0
#define NB_ERR_UNSUPPORTED 1 /* hardware absent on this build */
#define NB_ERR_ARG 2         /* payload was malformed         */
#define NB_ERR_HARDWARE 3    /* device present but refused    */

#pragma pack(push, 1)

/* Reply to anything that only reports success or failure. */
typedef struct {
    uint8_t code;
} NbStatusWire;

/* Clock, and the RTC's own die temperature since it costs nothing to add. */
typedef struct {
    uint16_t year; /* full year, e.g. 2026 */
    uint8_t month; /* 1-12 */
    uint8_t day;   /* 1-31 */
    uint8_t hour;  /* 0-23 */
    uint8_t minute;
    uint8_t second;
    uint8_t weekday;    /* 1-7, 1 = Sunday */
    int16_t temp_centi; /* hundredths of a degree C, signed */
} NbTimeWire;

/* Debounced input state. Buttons are chip 0's 16 pins in bits 0-15, then the
 * button pins of the second chip above them. */
typedef struct {
    uint32_t buttons;
    uint8_t toggles;
    uint8_t leds; /* current LED state, bit 0 = first LED */
} NbPanelWire;

/* Rotary encoder. Sent on every change, and on request. Rotation is counted in
 * detents (clicks), not quadrature edges. */
typedef struct {
    int32_t position; /* detents since boot, signed, free running */
    int8_t delta;     /* detents in this event, + = clockwise */
    uint8_t pressed;  /* 1 = button down */
    uint8_t flags;    /* NB_ENC_* */
} NbEncoderWire;

/* Who am I, and what am I running. */
typedef struct {
    uint8_t proto_version; /* NB_PROTO_VERSION the firmware speaks */
    uint8_t fw_major;
    uint8_t fw_minor;
    uint8_t fw_patch;

    uint16_t build_year; /* when the firmware was compiled */
    uint8_t build_month;
    uint8_t build_day;
    uint8_t build_hour;
    uint8_t build_minute;
    uint8_t build_second;

    uint32_t uptime_s;
    uint64_t board_id; /* RP2040 unique flash id */
    uint8_t features;  /* NB_FEAT_* */
} NbInfoWire;

#pragma pack(pop)

/* Catch a layout change on either side at compile time rather than as garbage
 * on the wire. C++ on both sides today; the _Static_assert arm keeps it usable
 * from C if this ever gets included by a C tool. */
#ifdef __cplusplus
static_assert(sizeof(NbTimeWire) == 10, "NbTimeWire layout changed");
static_assert(sizeof(NbPanelWire) == 6, "NbPanelWire layout changed");
static_assert(sizeof(NbEncoderWire) == 7, "NbEncoderWire layout changed");
static_assert(sizeof(NbInfoWire) == 24, "NbInfoWire layout changed");
#else
_Static_assert(sizeof(NbTimeWire) == 10, "NbTimeWire layout changed");
_Static_assert(sizeof(NbPanelWire) == 6, "NbPanelWire layout changed");
_Static_assert(sizeof(NbEncoderWire) == 7, "NbEncoderWire layout changed");
_Static_assert(sizeof(NbInfoWire) == 24, "NbInfoWire layout changed");
#endif
