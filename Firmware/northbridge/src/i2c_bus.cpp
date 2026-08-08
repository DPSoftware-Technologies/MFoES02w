#include "i2c_bus.h"

#include <string.h>

#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "pico/binary_info.h"

#include "semphr.h"

#include "IODef.h"

bi_decl(bi_2pins_with_func(IO_SYS_I2C_SDA, IO_SYS_I2C_SCL, GPIO_FUNC_I2C));

namespace {

    enum class I2cOp : uint8_t { Write, Read, WriteRead };

    /* Queue entry. Passed by value, so tx bytes travel with it. */
    struct I2cCmd {
        uint8_t addr;
        I2cOp op;
        uint8_t tx_len;
        uint8_t rx_len;
        int8_t slot; /* -1 for fire-and-forget */
        uint8_t tx[I2C_MAX_PAYLOAD];
    };

    /* Module-owned rendezvous for one blocking caller. */
    struct I2cTxn {
        SemaphoreHandle_t done;
        int result;
        uint8_t rx[I2C_MAX_PAYLOAD];
        bool completed; /* worker finished; guarded by a critical section */
        bool abandoned; /* caller gave up; worker must free the slot */
    };

    i2c_inst_t *g_i2c = nullptr;
    QueueHandle_t g_cmd_queue = nullptr;
    QueueHandle_t g_free_slots = nullptr; /* holds uint8_t slot indices */
    I2cTxn g_txn[I2C_TXN_SLOTS];
    uint32_t g_dropped = 0;

    int map_sdk_rc(int rc) {
        if (rc >= 0) {
            return rc;
        }
        return (rc == PICO_ERROR_TIMEOUT) ? I2C_ERR_TIMEOUT : I2C_ERR_NACK;
    }

    /* Runs on the worker task only. */
    int execute(const I2cCmd &cmd, uint8_t *rx) {
        int rc = I2C_ERR_ARG;

        switch (cmd.op) {
        case I2cOp::Write:
            rc = i2c_write_timeout_us(g_i2c, cmd.addr, cmd.tx, cmd.tx_len, false, I2C_XFER_TIMEOUT_US);
            break;

        case I2cOp::Read:
            rc = i2c_read_timeout_us(g_i2c, cmd.addr, rx, cmd.rx_len, false, I2C_XFER_TIMEOUT_US);
            break;

        case I2cOp::WriteRead:
            /* nostop=true holds the bus for the repeated start. */
            rc = i2c_write_timeout_us(g_i2c, cmd.addr, cmd.tx, cmd.tx_len, true, I2C_XFER_TIMEOUT_US);
            if (rc < 0) {
                break;
            }
            rc = i2c_read_timeout_us(g_i2c, cmd.addr, rx, cmd.rx_len, false, I2C_XFER_TIMEOUT_US);
            break;
        }

        return map_sdk_rc(rc);
    }

    void release_slot(uint8_t slot) { xQueueSend(g_free_slots, &slot, 0); }

    /* Hands the result back, or frees the slot if the caller already left. */
    void finish(uint8_t slot, int result) {
        I2cTxn &t = g_txn[slot];
        t.result = result;

        taskENTER_CRITICAL();
        const bool abandoned = t.abandoned;
        t.completed = true;
        taskEXIT_CRITICAL();

        if (abandoned) {
            release_slot(slot);
        } else {
            xSemaphoreGive(t.done);
        }
    }

    void i2c_worker(void *) {
        I2cCmd cmd;
        uint8_t scratch[I2C_MAX_PAYLOAD];

        for (;;) {
            if (xQueueReceive(g_cmd_queue, &cmd, portMAX_DELAY) != pdPASS) {
                continue;
            }

            /* The SDK transfer functions busy-wait rather than block, but they hold no
             * critical section, so higher priority tasks still preempt this one. */
            if (cmd.slot >= 0) {
                const uint8_t slot = (uint8_t)cmd.slot;
                finish(slot, execute(cmd, g_txn[slot].rx));
            } else {
                execute(cmd, scratch);
            }
        }
    }

    /* Queues `cmd` and waits for the worker. On return >= 0, `rx_len` bytes have
     * been copied into `rx`. */
    int run_sync(I2cCmd &cmd, uint8_t *rx, size_t rx_len, TickType_t wait) {
        if (g_cmd_queue == nullptr) {
            return I2C_ERR_NOT_INIT;
        }

        uint8_t slot;
        if (xQueueReceive(g_free_slots, &slot, wait) != pdPASS) {
            return I2C_ERR_BUSY;
        }

        I2cTxn &t = g_txn[slot];
        t.completed = false;
        t.abandoned = false;
        t.result = I2C_ERR_TIMEOUT;
        xSemaphoreTake(t.done, 0); /* drop any stale give */

        cmd.slot = (int8_t)slot;
        if (xQueueSend(g_cmd_queue, &cmd, wait) != pdPASS) {
            release_slot(slot);
            ++g_dropped;
            return I2C_ERR_QUEUE_FULL;
        }

        bool have_result = (xSemaphoreTake(t.done, wait) == pdTRUE);

        if (!have_result) {
            /* Race with the worker: whoever wins the critical section owns the slot. */
            taskENTER_CRITICAL();
            have_result = t.completed;
            if (!have_result) {
                t.abandoned = true;
            }
            taskEXIT_CRITICAL();

            if (!have_result) {
                return I2C_ERR_TIMEOUT; /* worker frees the slot when it finishes */
            }
            xSemaphoreTake(t.done, 0);
        }

        const int result = t.result;
        if (result >= 0 && rx != nullptr && rx_len > 0) {
            memcpy(rx, t.rx, rx_len);
        }
        release_slot(slot);
        return result;
    }

    bool post_async(I2cCmd &cmd, TickType_t wait) {
        if (g_cmd_queue == nullptr) {
            return false;
        }
        cmd.slot = -1;
        if (xQueueSend(g_cmd_queue, &cmd, wait) != pdPASS) {
            ++g_dropped;
            return false;
        }
        return true;
    }

} // namespace

bool i2c_bus_init(UBaseType_t worker_priority, UBaseType_t queue_depth) {
    if (g_cmd_queue != nullptr) {
        return true;
    }

    g_i2c = (IO_SYS_I2C_INDEX == 0) ? i2c0 : i2c1;
    i2c_init(g_i2c, IO_SYS_I2C_BAUD);
    gpio_set_function(IO_SYS_I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(IO_SYS_I2C_SCL, GPIO_FUNC_I2C);
    /* Internal pull-ups are weak (~50k). Keep the board's external ones. */
    gpio_pull_up(IO_SYS_I2C_SDA);
    gpio_pull_up(IO_SYS_I2C_SCL);

    g_cmd_queue = xQueueCreate(queue_depth, sizeof(I2cCmd));
    g_free_slots = xQueueCreate(I2C_TXN_SLOTS, sizeof(uint8_t));
    if (g_cmd_queue == nullptr || g_free_slots == nullptr) {
        g_cmd_queue = nullptr;
        return false;
    }

    for (uint8_t i = 0; i < I2C_TXN_SLOTS; ++i) {
        g_txn[i].done = xSemaphoreCreateBinary();
        if (g_txn[i].done == nullptr) {
            g_cmd_queue = nullptr;
            return false;
        }
        xQueueSend(g_free_slots, &i, 0);
    }

    if (xTaskCreate(i2c_worker, "i2c", configMINIMAL_STACK_SIZE, nullptr, worker_priority, nullptr) != pdPASS) {
        g_cmd_queue = nullptr;
        return false;
    }
    return true;
}

int i2c_write(uint8_t addr, const uint8_t *src, size_t len, TickType_t wait) {
    if (len == 0 || len > I2C_MAX_PAYLOAD || src == nullptr) {
        return I2C_ERR_ARG;
    }
    I2cCmd cmd{};
    cmd.addr = addr;
    cmd.op = I2cOp::Write;
    cmd.tx_len = (uint8_t)len;
    memcpy(cmd.tx, src, len);
    return run_sync(cmd, nullptr, 0, wait);
}

int i2c_read(uint8_t addr, uint8_t *dst, size_t len, TickType_t wait) {
    if (len == 0 || len > I2C_MAX_PAYLOAD || dst == nullptr) {
        return I2C_ERR_ARG;
    }
    I2cCmd cmd{};
    cmd.addr = addr;
    cmd.op = I2cOp::Read;
    cmd.rx_len = (uint8_t)len;
    return run_sync(cmd, dst, len, wait);
}

int i2c_write_read(uint8_t addr, const uint8_t *tx, size_t tx_len, uint8_t *rx, size_t rx_len, TickType_t wait) {
    if (tx_len == 0 || tx_len > I2C_MAX_PAYLOAD || tx == nullptr) {
        return I2C_ERR_ARG;
    }
    if (rx_len == 0 || rx_len > I2C_MAX_PAYLOAD || rx == nullptr) {
        return I2C_ERR_ARG;
    }
    I2cCmd cmd{};
    cmd.addr = addr;
    cmd.op = I2cOp::WriteRead;
    cmd.tx_len = (uint8_t)tx_len;
    cmd.rx_len = (uint8_t)rx_len;
    memcpy(cmd.tx, tx, tx_len);
    return run_sync(cmd, rx, rx_len, wait);
}

int i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t *dst, size_t len, TickType_t wait) {
    return i2c_write_read(addr, &reg, 1, dst, len, wait);
}

int i2c_write_reg(uint8_t addr, uint8_t reg, const uint8_t *src, size_t len, TickType_t wait) {
    if (src == nullptr || len == 0 || len + 1 > I2C_MAX_PAYLOAD) {
        return I2C_ERR_ARG;
    }
    I2cCmd cmd{};
    cmd.addr = addr;
    cmd.op = I2cOp::Write;
    cmd.tx_len = (uint8_t)(len + 1);
    cmd.tx[0] = reg;
    memcpy(&cmd.tx[1], src, len);
    return run_sync(cmd, nullptr, 0, wait);
}

bool i2c_probe(uint8_t addr, TickType_t wait) {
    uint8_t scratch = 0;
    return i2c_read(addr, &scratch, 1, wait) >= 0;
}

bool i2c_post_write(uint8_t addr, const uint8_t *src, size_t len, TickType_t wait) {
    if (len == 0 || len > I2C_MAX_PAYLOAD || src == nullptr) {
        return false;
    }
    I2cCmd cmd{};
    cmd.addr = addr;
    cmd.op = I2cOp::Write;
    cmd.tx_len = (uint8_t)len;
    memcpy(cmd.tx, src, len);
    return post_async(cmd, wait);
}

bool i2c_post_write_reg(uint8_t addr, uint8_t reg, const uint8_t *src, size_t len, TickType_t wait) {
    if (src == nullptr || len == 0 || len + 1 > I2C_MAX_PAYLOAD) {
        return false;
    }
    I2cCmd cmd{};
    cmd.addr = addr;
    cmd.op = I2cOp::Write;
    cmd.tx_len = (uint8_t)(len + 1);
    cmd.tx[0] = reg;
    memcpy(&cmd.tx[1], src, len);
    return post_async(cmd, wait);
}

uint32_t i2c_dropped_count(void) { return g_dropped; }
