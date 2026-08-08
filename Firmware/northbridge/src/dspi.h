#pragma once

#include <stddef.h>
#include <stdint.h>

#include "hardware/spi.h"

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "IODef.h"

/* Data link to the main controller over SPI1, with the northbridge as SLAVE.
 *
 * The main controller owns the clock, so the northbridge can never start a
 * transfer. It instead raises IO_DSPI_IRQ and waits to be read. Every transfer
 * is full duplex and exactly DSPI_FRAME_SIZE bytes: one inbound frame and one
 * outbound frame per exchange. When there is nothing to send, the slave
 * presents an idle frame, so the master can poll freely.
 *
 *   inbound  (master -> here):  DMA fills a frame, task validates it, queue
 *   outbound (here -> master):  dspi_send() queues, IRQ asserts, master reads
 *
 * Wire format, little-endian CRC:
 *   [0]      magic  DSPI_MAGIC
 *   [1]      type   application defined; DSPI_TYPE_IDLE means "nothing here"
 *   [2]      len    payload bytes in use, 0..DSPI_PAYLOAD_MAX
 *   [3]      flags  DSPI_FLAG_MORE = another frame is already queued
 *   [4..61]  payload
 *   [62..63] CRC-16/CCITT-FALSE over bytes 0..61
 *
 * The master must clock exactly DSPI_FRAME_SIZE bytes per CS assertion. A short
 * transfer leaves the DMA mid-frame; the task notices CS idle with a partial
 * frame and resynchronises, counted by dspi_resync_count(). */

/* Physical pin roles, independent of the IODef names. On RP2040 SPI1 the mux
 * is fixed: GPIO 11 is TX and GPIO 12 is RX. As a slave, TX drives the bus
 * MISO line and RX listens to the bus MOSI line -- the opposite of what the
 * master-side IODef names suggest. Use these aliases, not the raw names. */
#define DSPI_PIN_SLAVE_OUT IO_DSPI_MOSI /* GPIO 11, SPI1 TX -> master's MISO */
#define DSPI_PIN_SLAVE_IN IO_DSPI_MISO  /* GPIO 12, SPI1 RX <- master's MOSI */

#define DSPI_FRAME_SIZE 64
#define DSPI_PAYLOAD_MAX 58
#define DSPI_MAGIC 0xA5

#define DSPI_TYPE_IDLE 0x00
#define DSPI_FLAG_MORE 0x01
/* Set on a frame sent in answer to a request of the same type. */
#define DSPI_FLAG_REPLY 0x02

/* Nominal slave clock. The master supplies the real one; the PL022 needs
 * clk_peri >= 12x SCK, so keep the master at or below ~10 MHz. */
#define DSPI_BAUD (8 * 1000 * 1000)

/* Clock phase, and it is not a free choice.
 *
 * With CPHA=0 the PL022 frames every byte off the chip select edge: a master
 * that holds CS low across a multi-byte message gets exactly ONE byte out of
 * this slave and then silence, with the TX FIFO stuck full. Two ways out, and
 * the two ends must agree:
 *
 *   SPI_CPHA_1 + master mode 1, CS held low for the whole frame.
 *     Cleanest, but the Pi's auxiliary SPI1 rejects mode 1 outright
 *     ("SPI_IOC_WR_MODE: Invalid argument"), so it only works on SPI0.
 *
 *   SPI_CPHA_0 + master pulsing CS per byte (Config::cs_per_byte on the Linux
 *     side). Works on the aux SPI, which keeps this link off SPI0 and leaves
 *     that controller to the audio stream.
 *
 * Set to CPHA_0 to match the cs-per-byte master. */
#define DSPI_CPHA SPI_CPHA_0
#define DSPI_CPOL SPI_CPOL_0

/* IO_DSPI_IRQ level while outbound data is waiting. */
#define DSPI_IRQ_ACTIVE_LOW 1

/* Set to 0 to stop recovering from partial frames. Only for bringup: with it
 * off a desynchronised link stays broken, but a transfer that is merely slow is
 * allowed to finish instead of being aborted underneath. */
#define DSPI_RESYNC_ENABLE 1

struct DspiMsg {
    uint8_t type;
    uint8_t len;
    uint8_t flags; /* DSPI_FLAG_* as sent on the wire */
    uint8_t payload[DSPI_PAYLOAD_MAX];
};

/* Configures the SPI slave, both DMA channels, the IRQ line and the frame
 * task. Safe to call before the scheduler starts. Repeat calls return true. */
bool dspi_init(UBaseType_t task_priority, UBaseType_t rx_depth = 8, UBaseType_t tx_depth = 8);

/* Queues a frame for the master to collect and asserts IO_DSPI_IRQ. Returns
 * false if the outbound queue is still full after `wait`. */
bool dspi_send(uint8_t type, const void *payload, size_t len, TickType_t wait = 0, uint8_t flags = 0);

/* Takes the next frame sent by the master. */
bool dspi_receive(DspiMsg &out, TickType_t wait = portMAX_DELAY);

QueueHandle_t dspi_rx_queue(void);

/* True while outbound data is waiting, i.e. while the IRQ line is asserted. */
bool dspi_tx_pending(void);

/* DMA completion interrupts, i.e. frames the hardware actually finished.
 * Compare against the master's exchange count: equal means framing is sound,
 * far lower means transfers are not completing, wildly higher means the DMA is
 * free-running instead of waiting on the SPI. */
uint32_t dspi_isr_count(void);

/* Falling edges seen on the CS pad, counted independently of the SPI block.
 * This is the master's transfer count as observed here: if it stays near zero
 * while the master is exchanging, its chip select is not arriving. */
uint32_t dspi_cs_edge_count(void);

/* Current CS pad level. Readable even though the pin is muxed to the SPI
 * peripheral. 1 = idle. */
bool dspi_cs_level(void);

/* Bytes outstanding the last time a partial frame was seen. DSPI_FRAME_SIZE
 * minus this is how many bytes the transfer actually delivered, which says how
 * far into the frame the link gets before it stops. */
uint32_t dspi_last_partial(void);

/* PL022 receive overruns: the RX FIFO filled before the DMA drained it. A
 * climbing count means the DMA is not keeping up or is not wired to the SPI's
 * data request at all. */
uint32_t dspi_overrun_count(void);

/* Prints the PL022 and DMA register state plus the pin mux, for when the
 * counters say the hardware is not doing what the setup asked for. */
void dspi_debug_dump(void);

/* Bytes the RX DMA still expects for the frame in flight. Parked at
 * DSPI_FRAME_SIZE means no transfer has started since the last arm; anything
 * between means a transfer stopped part way. */
uint32_t dspi_rx_remaining(void);

uint32_t dspi_rx_count(void);
uint32_t dspi_tx_count(void);

/* Valid, CRC-checked frames carrying nothing. A climbing count means the link
 * is healthy and simply has no traffic. */
uint32_t dspi_idle_rx_count(void);

/* Frames that arrived all zero, i.e. the master clocked but no data landed. */
uint32_t dspi_zero_rx_count(void);
uint32_t dspi_crc_errors(void);   /* frames arriving with bad magic or CRC */
uint32_t dspi_resync_count(void); /* short or aborted transfers recovered from */
uint32_t dspi_rx_dropped(void);   /* inbound frames lost to a full queue */
