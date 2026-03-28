# southbridge

Real-time audio transfer from **RPi Zero 2W** → **Pico 2W** over high-speed SPI,
with full multi-task support on the Pico for network, GPS, sensors, PWM, and GPIO.

---

## Architecture

```
RPi Zero 2W (Alpine Linux)             Pico 2W (arduino-pico / FreeRTOS)
           
ALSA/PCM capture                       SPI0 slave DMA ISR (DMA_IRQ_0)
   │                                       │ (highest priority)
   ▼                                       ▼
Ring buffer (lock-free SPSC)           FreeRTOS defer task (prio 7)
   │                                       │
   ▼                                       ▼
SCHED_FIFO TX thread  SPI ►  Audio queue (32 frames)
  /dev/spidev0.0  CS0                      │
  /dev/spidev0.1  CS1                      ▼
   │                                   Audio-out task (prio 6) → I²S/PWM
   ▼ (MISO)
Status byte per frame                  Cmd task (prio 4) ← CS1
  (overflow/underrun/CRC/busy)         Network task (prio 3) - WiFi/TCP
                                       GPS task (prio 3) - UART NMEA
                                       Sensor/GPIO task (prio 2)
```

## Wiring

| Signal  | RPi GPIO | Pico GPIO |
|---------|----------|-----------|
| SCK     | 11       | 2         |
| MOSI    | 10       | 3         |
| MISO    | 9        | 4         |
| CS0     | 8 (CE0)  | 5         |
| CS1     | 7 (CE1)  | 13        |
| GND     | GND      | GND       |

> **Add 33Ω series resistors on SCK and MOSI to reduce ringing at 25 MHz.**
> Keep wires ≤ 10 cm.

### I²S DAC (recommended: PCM5102A or MAX98357A)

| Signal   | Pico GPIO |
|----------|-----------|
| BCK      | 26        |
| WS/LRCK  | 27        |
| DATA/DIN | 28        |

---

## Protocol

### Audio channel (CS0)

Every SPI transaction is exactly **`SB_FRAME_BYTES`** bytes (≈ 268 bytes for defaults).

```
Offset  Size  Field
0       1     0x5B (magic hi)
1       1     0xA4 (magic lo)
2       1     frame_type  (0x01=audio, 0x02=silence, 0xFF=NOP, 0xFE=reset)
3       1     status (MISO: Pico → RPi: 0=OK, 1=overflow, 2=underrun, 4=CRC err)
4       4     sequence number (LE)
8       256   PCM payload  (128 x 16-bit mono @ 16 kHz)
264     2     CRC-16/CCITT
266+    pad   zeros to 4-byte alignment
```

### Command channel (CS1)

Fixed 64-byte JSON-lite frames:

```
Offset  Size  Field
0       1     0x5C (magic hi)
1       1     0xB5 (magic lo)
2       1     payload length
3       1     reserved
4-63    60    JSON text (null-padded)
```

Example commands (RPi → Pico):
```json
{"cmd":"vol","val":80}
{"cmd":"gps","val":1}
{"cmd":"pwm","pin":22,"duty":128}
```

Example events (Pico → RPi):
```json
{"evt":"overflow","cnt":3}
{"evt":"gps","nmea":"$GPGGA,..."}
{"evt":"sensor","v":3.14}
```

---

## Building (Pico - arduino-pico)

1. Install [arduino-pico](https://github.com/earlephilhower/arduino-pico) in Arduino IDE
2. Open `pico/example/pico_main.ino`
3. Add library files from `pico/southbridge_pico/` to your sketch folder
4. Select **Raspberry Pi Pico 2W** board, enable **FreeRTOS**
5. Upload

## Tuning

| Parameter | Default | Notes |
|-----------|---------|-------|
| `spi_speed_hz` | 25 MHz | Max safe with 10 cm wires; reduce if CRC errors appear |
| `ring_capacity` | 64 frames | ~512 ms buffer; increase for jittery capture |
| `SB_AUDIO_SAMPLES_PER_FRAME` | 128 | Trade latency vs overhead (64=lower latency) |
| `SB_SAMPLE_RATE` | 16000 Hz | Change in protocol.h; affects frame timing |
| `tx_thread_prio` | 85 | SCHED_FIFO; keep below kernel IRQ threads (~90) |
| `overflow_drop_threshold` | 90% | At this ring fill, oldest frames are dropped |
| `SB_PICO_AUDIO_QUEUE_DEPTH` | 32 | Pico-side audio buffer; 32 × 256 bytes = 8 KB |

### Preventing audio loss

- RPi: use `SCHED_FIFO` (requires CAP_SYS_NICE or root)
- RPi: pin TX thread to a non-IRQ core (`tx_thread_cpu = 3`)
- Pico: DMA RX runs independently of FreeRTOS scheduler — no ISR drop
- Pico: audio queue depth 32 frames ≈ 256 ms of buffer at 16 kHz
- If `pico_overflows` counter rises: increase `ring_capacity` on RPi,
  or decrease `SB_AUDIO_SAMPLES_PER_FRAME` for smaller/faster frames

---