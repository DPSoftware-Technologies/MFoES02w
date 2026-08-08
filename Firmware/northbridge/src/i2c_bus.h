#pragma once

#include <stddef.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

/* Serialised I2C access for FreeRTOS tasks.
 *
 * One worker task owns the bus; every caller goes through a command queue, so
 * two tasks can never interleave a register write and a read on the same
 * device. Nothing here allocates after i2c_bus_init().
 *
 * Blocking calls copy payloads into a module-owned transaction slot, so a
 * caller that gives up on its timeout cannot leave the worker writing into a
 * dead stack frame.
 */

/* Largest tx or rx payload of a single command. Bump if a device needs more;
 * it sizes both the queue entries and the transaction slots. */
#define I2C_MAX_PAYLOAD 32

/* Concurrent blocking callers. Extra callers wait for a free slot. */
#define I2C_TXN_SLOTS 4

/* Per-transfer bus timeout. 32 bytes at 100 kHz is roughly 3 ms. */
#define I2C_XFER_TIMEOUT_US 10000

/* Negative returns from the blocking calls. Non-negative = bytes transferred. */
enum I2cError : int {
    I2C_ERR_NOT_INIT = -10, /* i2c_bus_init() not called (or it failed) */
    I2C_ERR_ARG = -11,      /* length exceeds I2C_MAX_PAYLOAD, or zero-length */
    I2C_ERR_BUSY = -12,     /* no free transaction slot within the wait */
    I2C_ERR_QUEUE_FULL = -13,
    I2C_ERR_TIMEOUT = -14, /* bus timeout, or caller wait expired */
    I2C_ERR_NACK = -15,    /* address or data byte not acknowledged */
};

/* Brings up the pins (IO_SYS_I2C_SDA / IO_SYS_I2C_SCL from IODef.h) plus the queue and
 * worker task. Call once, before or after vTaskStartScheduler(). Repeat calls
 * are no-ops that return true. */
bool i2c_bus_init(UBaseType_t worker_priority, UBaseType_t queue_depth = 8);

/* ---------------- Blocking calls (queued, then wait) ---------------- */
/* `wait` bounds each of: slot acquire, queue send, worker completion — so the
 * worst case is roughly 3x `wait` plus one bus timeout. */

int i2c_write(uint8_t addr, const uint8_t *src, size_t len, TickType_t wait = pdMS_TO_TICKS(100));

int i2c_read(uint8_t addr, uint8_t *dst, size_t len, TickType_t wait = pdMS_TO_TICKS(100));

/* Write then read without releasing the bus (repeated start). */
int i2c_write_read(uint8_t addr, const uint8_t *tx, size_t tx_len, uint8_t *rx, size_t rx_len,
                   TickType_t wait = pdMS_TO_TICKS(100));

int i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t *dst, size_t len, TickType_t wait = pdMS_TO_TICKS(100));

int i2c_write_reg(uint8_t addr, uint8_t reg, const uint8_t *src, size_t len, TickType_t wait = pdMS_TO_TICKS(100));

/* True if `addr` acknowledges a 1-byte read. */
bool i2c_probe(uint8_t addr, TickType_t wait = pdMS_TO_TICKS(50));

/* ---------------- Fire-and-forget (queue, do not wait) ---------------- */
/* Payload is copied into the queue entry. Result is dropped; use these for
 * things like display refreshes where a retry is pointless. */

bool i2c_post_write(uint8_t addr, const uint8_t *src, size_t len, TickType_t wait = 0);

bool i2c_post_write_reg(uint8_t addr, uint8_t reg, const uint8_t *src, size_t len, TickType_t wait = 0);

/* Commands dropped because the queue was full since boot. */
uint32_t i2c_dropped_count(void);
