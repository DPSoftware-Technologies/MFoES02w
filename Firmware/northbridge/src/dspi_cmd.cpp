#include "dspi_cmd.h"

#include <string.h>

namespace {

    struct Binding {
        DspiCommandFn fn;
        void *user;
        uint8_t type;
        bool used;
    };

    Binding g_table[DSPI_MAX_HANDLERS];
    Binding g_unknown = {};

    TaskHandle_t g_task = nullptr;
    uint32_t g_handled = 0;
    uint32_t g_unhandled = 0;

    Binding *find(uint8_t type) {
        for (uint8_t i = 0; i < DSPI_MAX_HANDLERS; ++i) {
            if (g_table[i].used && g_table[i].type == type) {
                return &g_table[i];
            }
        }
        return nullptr;
    }

    void dispatcher_task(void *) {
        for (;;) {
            DspiMsg req{};
            if (!dspi_receive(req)) {
                continue;
            }

            /* A reply arriving here would mean the master answered something we
             * asked. Nothing in this direction requests, so drop it rather than
             * feed it back into a handler and risk a reply loop. */
            if (req.flags & DSPI_FLAG_REPLY) {
                continue;
            }

            uint8_t buf[DSPI_PAYLOAD_MAX];
            DspiReply reply{buf, DSPI_PAYLOAD_MAX, 0};

            Binding *binding = find(req.type);
            if (binding == nullptr) {
                if (g_unknown.fn == nullptr) {
                    ++g_unhandled;
                    continue;
                }
                binding = &g_unknown;
            }

            binding->fn(req, reply, binding->user);
            ++g_handled;

            if (reply.len > 0) {
                dspi_send(req.type, reply.data, reply.len, 0, DSPI_FLAG_REPLY);
            }
        }
    }

} // namespace

bool DspiReply::write(const void *src, size_t n) {
    if (src == nullptr || len + n > capacity) {
        return false;
    }
    memcpy(data + len, src, n);
    len = (uint8_t)(len + n);
    return true;
}

bool dspi_cmd_init(UBaseType_t task_priority) {
    if (g_task != nullptr) {
        return true;
    }
    return xTaskCreate(dispatcher_task, "dspicmd", configMINIMAL_STACK_SIZE * 2, nullptr, task_priority, &g_task) ==
           pdPASS;
}

bool dspi_on(uint8_t type, DspiCommandFn fn, void *user) {
    if (type == DSPI_TYPE_IDLE || fn == nullptr) {
        return false;
    }

    Binding *slot = find(type);
    if (slot == nullptr) {
        for (uint8_t i = 0; i < DSPI_MAX_HANDLERS; ++i) {
            if (!g_table[i].used) {
                slot = &g_table[i];
                break;
            }
        }
    }
    if (slot == nullptr) {
        return false;
    }

    slot->fn = fn;
    slot->user = user;
    slot->type = type;
    slot->used = true;
    return true;
}

void dspi_on_unknown(DspiCommandFn fn, void *user) {
    g_unknown.fn = fn;
    g_unknown.user = user;
}

uint32_t dspi_cmd_handled(void) { return g_handled; }

uint32_t dspi_cmd_unhandled(void) { return g_unhandled; }
