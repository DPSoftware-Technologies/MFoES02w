#pragma once
/*
 * southbridge_protocol.h
 * Shared wire protocol between RPi02W (master) and Pico2W (slave).
 *
 * Wire layout (SPI, MSB first):
 *   CS0 – audio stream   (framed packets, MOSI = RPi→Pico, MISO = status)
 *   CS1 – command channel (JSON-lite, bidirectional)
 *
 * Every transfer on CS0 is exactly SOUTHBRIDGE_FRAME_BYTES long so DMA on
 * the Pico side never has to guess transfer size.
 */

#include <stdint.h>

/*  tunables  */
#define SB_AUDIO_SAMPLES_PER_FRAME   128   /* PCM samples per SPI frame    */
#define SB_SAMPLE_BYTES              2     /* 16-bit signed PCM             */
#define SB_CHANNELS                  1     /* mono; set 2 for stereo        */
#define SB_SAMPLE_RATE               16000 /* Hz                            */

/*  derived  */
#define SB_AUDIO_PAYLOAD_BYTES \
    (SB_AUDIO_SAMPLES_PER_FRAME * SB_SAMPLE_BYTES * SB_CHANNELS)

/* header(8) + payload + crc(2) + padding to 4-byte align */
#define SB_FRAME_HEADER_BYTES   8
#define SB_FRAME_CRC_BYTES      2
#define SB_FRAME_RAW \
    (SB_FRAME_HEADER_BYTES + SB_AUDIO_PAYLOAD_BYTES + SB_FRAME_CRC_BYTES)
/* round up to next multiple of 4 for DMA alignment */
#define SB_FRAME_BYTES          (((SB_FRAME_RAW) + 3) & ~3)

/*  frame types  */
#define SB_FTYPE_AUDIO           0x01
#define SB_FTYPE_SILENCE         0x02  /* no payload, tell Pico to play silence */
#define SB_FTYPE_RESET           0xFE
#define SB_FTYPE_NOP             0xFF

/*  status bits returned on MISO  */
#define SB_STATUS_OK             0x00
#define SB_STATUS_OVERFLOW       0x01  /* Pico audio ring is full           */
#define SB_STATUS_UNDERRUN       0x02  /* Pico had nothing to play          */
#define SB_STATUS_CRC_ERR        0x04
#define SB_STATUS_BUSY           0x08  /* Pico not ready, retry             */

/*  magic  */
#define SB_FRAME_MAGIC_HI        0x5B
#define SB_FRAME_MAGIC_LO        0xA4

/*  audio frame layout 
 *
 *  Offset  Size  Field
 *  0       1     magic_hi  (0x5B)
 *  1       1     magic_lo  (0xA4)
 *  2       1     frame_type
 *  3       1     status    (slave→master on MISO; master sets 0x00)
 *  4       4     seq       (little-endian sequence counter)
 *  8       N     pcm_data  (SB_AUDIO_PAYLOAD_BYTES bytes)
 *  8+N     2     crc16     (CRC-16/CCITT of bytes 0..8+N-1)
 *  8+N+2   P     padding   (zeros to reach SB_FRAME_BYTES)
 *  */
#pragma pack(push, 1)
typedef struct {
    uint8_t  magic_hi;
    uint8_t  magic_lo;
    uint8_t  frame_type;
    uint8_t  status;
    uint32_t seq;
    uint8_t  payload[SB_AUDIO_PAYLOAD_BYTES];
    uint16_t crc16;
    /* compiler may not add the alignment bytes here; padded at send time */
} sb_audio_frame_t;
#pragma pack(pop)

/*  command frame (CS1) 
 * Fixed 64-byte frames, JSON-lite text, null-padded.
 * e.g.  {"cmd":"vol","val":80}
 *       {"cmd":"gps","val":1}
 *       {"evt":"overflow","cnt":3}
 *  */
#define SB_CMD_FRAME_BYTES       64

typedef struct {
    uint8_t  magic_hi;   /* 0x5C */
    uint8_t  magic_lo;   /* 0xB5 */
    uint8_t  len;        /* payload length (max 60) */
    uint8_t  reserved;
    uint8_t  payload[SB_CMD_FRAME_BYTES - 4];
} sb_cmd_frame_t;

#define SB_CMD_MAGIC_HI 0x5C
#define SB_CMD_MAGIC_LO 0xB5

/*  CRC helper (header-only, works on both sides)  */
static inline uint16_t sb_crc16(const uint8_t *buf, uint32_t len)
{
    uint16_t crc = 0xFFFF;
    while (len--) {
        crc ^= (uint16_t)(*buf++) << 8;
        for (int i = 0; i < 8; i++)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}
