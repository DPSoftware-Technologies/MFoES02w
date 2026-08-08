#pragma once

#include <stddef.h>
#include <stdint.h>

#include "dspi.h"

/* Command dispatch for the main-controller link.
 *
 * Register one handler per frame type and a task calls it as frames arrive.
 * Anything the handler writes into the reply goes back to the master tagged
 * DSPI_FLAG_REPLY; write nothing and the command is simply consumed.
 *
 *   static void on_set_leds(const DspiMsg &req, DspiReply &, void *) {
 *       pcf8575_set_leds(req.payload[0]);
 *   }
 *
 *   static void on_get_time(const DspiMsg &, DspiReply &reply, void *) {
 *       Ds3231Time now{};
 *       ds3231_get_time(now);
 *       reply.write(now);
 *   }
 *
 *   dspi_cmd_init(tskIDLE_PRIORITY + 2);
 *   dspi_on(CMD_SET_LEDS, on_set_leds);
 *   dspi_on(CMD_GET_TIME, on_get_time);
 *
 * Handlers run on the dispatcher task, one at a time, so they may block and
 * may use the I2C bus. They must not run longer than the master is willing to
 * wait for its reply. */

#define DSPI_MAX_HANDLERS 16

/* Reply buffer handed to a handler. Untouched means "no reply". */
struct DspiReply {
    uint8_t *data;
    uint8_t capacity;
    uint8_t len;

    /* Appends raw bytes. False if they would not fit, leaving the reply as it
     * was so a partial answer is never sent. */
    bool write(const void *src, size_t n);

    /* Appends one trivially copyable value. */
    template <typename T> bool write(const T &value) { return write(&value, sizeof(T)); }
};

using DspiCommandFn = void (*)(const DspiMsg &req, DspiReply &reply, void *user);

/* Starts the dispatcher task. dspi_init() must have run first. */
bool dspi_cmd_init(UBaseType_t task_priority);

/* Binds a handler to a frame type, replacing any previous one. False if the
 * table is full or the type is DSPI_TYPE_IDLE. */
bool dspi_on(uint8_t type, DspiCommandFn fn, void *user = nullptr);

/* Handler for types with no binding. Without one, unknown frames are dropped. */
void dspi_on_unknown(DspiCommandFn fn, void *user = nullptr);

/* Commands dispatched, and frames dropped for having no handler. */
uint32_t dspi_cmd_handled(void);
uint32_t dspi_cmd_unhandled(void);
