#include "dspi.h"

#include <string.h>

#include <stdio.h>

#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/spi.h"
#include "pico/binary_info.h"

#include "IODef.h"

/* GPIO 10/11/12/13 are the SPI1 SCK/TX/RX/SS_n group. As a slave, TX drives the
 * bus MISO line and RX listens to the bus MOSI line, so the master's MOSI must
 * land on IO_DSPI_MISO (GPIO 12) and its MISO on IO_DSPI_MOSI (GPIO 11). The
 * IODef names read from the master's point of view. */
#define DSPI_SPI spi1

bi_decl(bi_4pins_with_func(IO_DSPI_SCK, IO_DSPI_MOSI, IO_DSPI_MISO, IO_DSPI_CS, GPIO_FUNC_SPI));
bi_decl(bi_1pin_with_name(IO_DSPI_IRQ, "DSPI IRQ to main controller"));

namespace {

    enum : uint8_t {
        OFF_MAGIC = 0,
        OFF_TYPE = 1,
        OFF_LEN = 2,
        OFF_FLAGS = 3,
        OFF_PAYLOAD = 4,
        OFF_CRC = DSPI_FRAME_SIZE - 2,
    };

#if DSPI_IRQ_ACTIVE_LOW
    constexpr bool IRQ_ASSERTED = false;
#else
    constexpr bool IRQ_ASSERTED = true;
#endif

    QueueHandle_t g_rx_queue = nullptr;
    QueueHandle_t g_tx_queue = nullptr;
    TaskHandle_t g_task = nullptr;

    int g_rx_chan = -1;
    int g_tx_chan = -1;

    /* RX ping-pongs so the ISR can re-arm before the task reads the frame. */
    uint8_t g_rx_buf[2][DSPI_FRAME_SIZE];
    volatile uint8_t g_rx_live = 0;

    uint8_t g_idle_frame[DSPI_FRAME_SIZE];
    uint8_t g_tx_stage[DSPI_FRAME_SIZE];

    /* Handover of the staged frame. The task only writes this when it reads
     * g_idle_frame back, and the ISR only ever writes g_idle_frame, so the
     * staging buffer is never touched while the DMA is reading it. */
    uint8_t *volatile g_next_tx = nullptr;

    volatile uint32_t g_isr_count = 0;
    volatile uint32_t g_cs_edges = 0;
    volatile uint32_t g_last_partial = 0;
    uint32_t g_prev_remaining = DSPI_FRAME_SIZE; /* poll task only */
    volatile uint32_t g_overruns = 0;
    volatile uint32_t g_rx_count = 0;
    volatile uint32_t g_idle_rx = 0;
    volatile uint32_t g_zero_rx = 0;
    volatile uint32_t g_tx_count = 0;
    volatile uint32_t g_crc_errors = 0;
    volatile uint32_t g_rx_dropped = 0;
    uint32_t g_resyncs = 0;

    uint16_t crc16_ccitt(const uint8_t *data, size_t len) {
        uint16_t crc = 0xFFFF;
        for (size_t i = 0; i < len; ++i) {
            crc = (uint16_t)(crc ^ ((uint16_t)data[i] << 8));
            for (uint8_t bit = 0; bit < 8; ++bit) {
                crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
            }
        }
        return crc;
    }

    void build_frame(uint8_t *frame, uint8_t type, const uint8_t *payload, uint8_t len, bool more, uint8_t flags = 0) {
        memset(frame, 0, DSPI_FRAME_SIZE);
        frame[OFF_MAGIC] = DSPI_MAGIC;
        frame[OFF_TYPE] = type;
        frame[OFF_LEN] = len;
        frame[OFF_FLAGS] = (uint8_t)(flags | (more ? DSPI_FLAG_MORE : 0));
        if (payload != nullptr && len > 0) {
            memcpy(&frame[OFF_PAYLOAD], payload, len);
        }
        const uint16_t crc = crc16_ccitt(frame, OFF_CRC);
        frame[OFF_CRC] = (uint8_t)(crc & 0xFF);
        frame[OFF_CRC + 1] = (uint8_t)(crc >> 8);
    }

    void set_irq_line(bool asserted) { gpio_put(IO_DSPI_IRQ, asserted ? IRQ_ASSERTED : !IRQ_ASSERTED); }

    /* Empties both FIFOs. The PL022 clears them when the peripheral is
     * disabled, and toggling SSE is the only way to do it.
     *
     * This matters at every frame boundary: the TX FIFO is 8 deep, so when a
     * frame ends up to 7 of its bytes are still queued unsent. Left there, they
     * shift out ahead of the next frame and offset it -- and every frame after
     * it -- by that many bytes. */
    void flush_fifos(void) {
        hw_clear_bits(&spi_get_hw(DSPI_SPI)->cr1, SPI_SSPCR1_SSE_BITS);
        hw_set_bits(&spi_get_hw(DSPI_SPI)->cr1, SPI_SSPCR1_SSE_BITS);
    }

    /* Points both channels at their buffers and starts them together. */
    void arm_dma(uint8_t *rx, const uint8_t *tx) {
        dma_channel_set_write_addr(g_rx_chan, rx, false);
        dma_channel_set_read_addr(g_tx_chan, tx, false);
        dma_channel_set_trans_count(g_rx_chan, DSPI_FRAME_SIZE, false);
        dma_channel_set_trans_count(g_tx_chan, DSPI_FRAME_SIZE, false);
        dma_start_channel_mask((1u << g_rx_chan) | (1u << g_tx_chan));
    }

    /* Counts CS assertions straight off the pad. The GPIO IRQ block watches the
     * input synchroniser, which keeps working while the pin is muxed to SPI. */
    void dspi_cs_isr(void) {
        if ((gpio_get_irq_event_mask(IO_DSPI_CS) & GPIO_IRQ_EDGE_FALL) == 0) {
            return;
        }
        gpio_acknowledge_irq(IO_DSPI_CS, GPIO_IRQ_EDGE_FALL);
        ++g_cs_edges;
    }

    void dspi_dma_isr(void) {
        if ((dma_hw->ints0 & (1u << g_rx_chan)) == 0) {
            return; /* shared DMA_IRQ_0: not our channel */
        }
        dma_hw->ints0 = 1u << g_rx_chan;
        ++g_isr_count;

        const uint8_t done = g_rx_live;
        g_rx_live ^= 1;

        /* Re-arm first: the master may drop CS and start the next frame at
         * once, and the TX FIFO has to be preloaded before it does.
         *
         * Stop the outgoing channel and empty the FIFOs before re-arming, so
         * the next frame starts on a clean byte boundary instead of trailing
         * whatever the last one left queued. */
        uint8_t *next_tx = g_next_tx;
        const bool sent_real = next_tx != g_idle_frame;
        g_next_tx = g_idle_frame;

        dma_channel_abort(g_tx_chan);
        flush_fifos();
        arm_dma(g_rx_buf[g_rx_live], next_tx);

        if (sent_real) {
            ++g_tx_count;
        }

        /* Validate and hand over the frame that just landed. */
        const uint8_t *frame = g_rx_buf[done];
        BaseType_t higher_woken = pdFALSE;

        if (frame[OFF_MAGIC] == DSPI_MAGIC && frame[OFF_LEN] <= DSPI_PAYLOAD_MAX) {
            const uint16_t want = (uint16_t)(frame[OFF_CRC] | ((uint16_t)frame[OFF_CRC + 1] << 8));
            if (want == crc16_ccitt(frame, OFF_CRC)) {
                if (frame[OFF_TYPE] != DSPI_TYPE_IDLE) {
                    DspiMsg msg;
                    msg.type = frame[OFF_TYPE];
                    msg.len = frame[OFF_LEN];
                    msg.flags = frame[OFF_FLAGS];
                    memcpy(msg.payload, &frame[OFF_PAYLOAD], DSPI_PAYLOAD_MAX);
                    if (xQueueSendFromISR(g_rx_queue, &msg, &higher_woken) != pdPASS) {
                        ++g_rx_dropped;
                    } else {
                        ++g_rx_count;
                    }
                } else {
                    ++g_idle_rx; /* healthy link, nothing to say */
                }
            } else {
                ++g_crc_errors;
            }
        } else if (frame[OFF_MAGIC] != 0x00) {
            ++g_crc_errors;
        } else {
            ++g_zero_rx; /* clocked, but no data arrived */
        }

        vTaskNotifyGiveFromISR(g_task, &higher_woken);
        portYIELD_FROM_ISR(higher_woken);
    }

    /* A transfer that stopped part way leaves the RX channel mid-frame with CS
     * already released. Start the frame over so the link cannot stay skewed. */
    bool resync_if_stalled(void) {
        /* Sample the overrun flag regardless: it says whether the RX FIFO is
         * being drained, which is independent of how far the frame got. */
        if (spi_get_hw(DSPI_SPI)->ris & SPI_SSPRIS_RORRIS_BITS) {
            ++g_overruns;
            spi_get_hw(DSPI_SPI)->icr = SPI_SSPICR_RORIC_BITS;
        }

        const uint32_t remaining = dma_channel_hw_addr(g_rx_chan)->transfer_count;
        if (remaining == DSPI_FRAME_SIZE || remaining == 0) {
            g_prev_remaining = remaining;
            return false; /* idle at a frame boundary */
        }

        /* Judge by progress, not by the CS level. The master pulses CS between
         * every byte and sends the frame in chunks, so CS is high for most of a
         * perfectly healthy transfer -- aborting on that alone tears up frames
         * that were only mid-flight. A byte count that has not moved since the
         * last check, a whole tick ago, is genuinely stuck. */
        const uint32_t previous = g_prev_remaining;
        g_prev_remaining = remaining;
        if (remaining != previous) {
            return false; /* still advancing */
        }

        g_last_partial = remaining;

#if !DSPI_RESYNC_ENABLE
        return false;
#endif

        /* Mask the completion IRQ across the abort: an aborted channel can
         * still raise one, which would re-enter the ISR and re-arm underneath
         * us. Clear any that latched before re-enabling. */
        dma_channel_set_irq0_enabled(g_rx_chan, false);
        dma_channel_abort(g_rx_chan);
        dma_channel_abort(g_tx_chan);
        dma_hw->ints0 = 1u << g_rx_chan;
        dma_channel_set_irq0_enabled(g_rx_chan, true);

        while (spi_is_readable(DSPI_SPI)) {
            (void)spi_get_hw(DSPI_SPI)->dr;
        }
        spi_get_hw(DSPI_SPI)->icr = SPI_SSPICR_RORIC_BITS;

        g_next_tx = g_idle_frame;
        arm_dma(g_rx_buf[g_rx_live], g_idle_frame);
        g_prev_remaining = DSPI_FRAME_SIZE;
        ++g_resyncs;
        return true;
    }

    void dspi_task(void *) {
        for (;;) {
            /* The timeout doubles as the stall check; frames wake us sooner. */
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(20));

            resync_if_stalled();

            /* Stage the next outbound frame only while the DMA is reading the
             * idle buffer, so the staging buffer is never rewritten in flight. */
            if (g_next_tx == g_idle_frame && uxQueueMessagesWaiting(g_tx_queue) > 0) {
                DspiMsg msg;
                if (xQueueReceive(g_tx_queue, &msg, 0) == pdPASS) {
                    const bool more = uxQueueMessagesWaiting(g_tx_queue) > 0;
                    build_frame(g_tx_stage, msg.type, msg.payload, msg.len, more, msg.flags);
                    g_next_tx = g_tx_stage;
                }
            }

            set_irq_line(dspi_tx_pending());
        }
    }

} // namespace

bool dspi_init(UBaseType_t task_priority, UBaseType_t rx_depth, UBaseType_t tx_depth) {
    if (g_rx_queue != nullptr) {
        return true;
    }

    g_rx_queue = xQueueCreate(rx_depth, sizeof(DspiMsg));
    g_tx_queue = xQueueCreate(tx_depth, sizeof(DspiMsg));
    if (g_rx_queue == nullptr || g_tx_queue == nullptr) {
        g_rx_queue = nullptr;
        return false;
    }

    build_frame(g_idle_frame, DSPI_TYPE_IDLE, nullptr, 0, false);
    g_next_tx = g_idle_frame;

    /* IRQ line first, so it is already deasserted when the master boots. */
    gpio_init(IO_DSPI_IRQ);
    gpio_set_dir(IO_DSPI_IRQ, GPIO_OUT);
    set_irq_line(false);

    spi_init(DSPI_SPI, DSPI_BAUD);
    spi_set_format(DSPI_SPI, 8, DSPI_CPOL, DSPI_CPHA, SPI_MSB_FIRST);
    /* Slave last: it cycles SSE around the MS change, so the format above is
     * applied while the peripheral is disabled, as the PL022 docs want. */
    spi_set_slave(DSPI_SPI, true);

    gpio_set_function(IO_DSPI_SCK, GPIO_FUNC_SPI);
    gpio_set_function(DSPI_PIN_SLAVE_OUT, GPIO_FUNC_SPI);
    gpio_set_function(DSPI_PIN_SLAVE_IN, GPIO_FUNC_SPI);
    gpio_set_function(IO_DSPI_CS, GPIO_FUNC_SPI);

    g_rx_chan = dma_claim_unused_channel(false);
    g_tx_chan = dma_claim_unused_channel(false);
    if (g_rx_chan < 0 || g_tx_chan < 0) {
        g_rx_queue = nullptr;
        return false;
    }

    dma_channel_config rx_cfg = dma_channel_get_default_config(g_rx_chan);
    channel_config_set_transfer_data_size(&rx_cfg, DMA_SIZE_8);
    channel_config_set_read_increment(&rx_cfg, false);
    channel_config_set_write_increment(&rx_cfg, true);
    channel_config_set_dreq(&rx_cfg, spi_get_dreq(DSPI_SPI, false));
    dma_channel_configure(g_rx_chan, &rx_cfg, g_rx_buf[0], &spi_get_hw(DSPI_SPI)->dr, DSPI_FRAME_SIZE, false);

    dma_channel_config tx_cfg = dma_channel_get_default_config(g_tx_chan);
    channel_config_set_transfer_data_size(&tx_cfg, DMA_SIZE_8);
    channel_config_set_read_increment(&tx_cfg, true);
    channel_config_set_write_increment(&tx_cfg, false);
    channel_config_set_dreq(&tx_cfg, spi_get_dreq(DSPI_SPI, true));
    dma_channel_configure(g_tx_chan, &tx_cfg, &spi_get_hw(DSPI_SPI)->dr, g_idle_frame, DSPI_FRAME_SIZE, false);

    if (xTaskCreate(dspi_task, "dspi", configMINIMAL_STACK_SIZE, nullptr, task_priority, &g_task) != pdPASS) {
        g_rx_queue = nullptr;
        return false;
    }

    /* Shared handler: DMA_IRQ_0 may already carry other channels. */
    irq_add_shared_handler(DMA_IRQ_0, dspi_dma_isr, PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
    dma_channel_set_irq0_enabled(g_rx_chan, true);
    irq_set_enabled(DMA_IRQ_0, true);

    /* Watch CS independently of the SPI block, purely as instrumentation. */
    gpio_add_raw_irq_handler(IO_DSPI_CS, dspi_cs_isr);
    gpio_set_irq_enabled(IO_DSPI_CS, GPIO_IRQ_EDGE_FALL, true);
    irq_set_enabled(IO_IRQ_BANK0, true);

    arm_dma(g_rx_buf[0], g_idle_frame);
    return true;
}

bool dspi_send(uint8_t type, const void *payload, size_t len, TickType_t wait, uint8_t flags) {
    if (g_tx_queue == nullptr || len > DSPI_PAYLOAD_MAX) {
        return false;
    }

    DspiMsg msg;
    msg.type = type;
    msg.len = (uint8_t)len;
    msg.flags = flags;
    memset(msg.payload, 0, sizeof(msg.payload));
    if (payload != nullptr && len > 0) {
        memcpy(msg.payload, payload, len);
    }

    if (xQueueSend(g_tx_queue, &msg, wait) != pdPASS) {
        return false;
    }

    /* Assert straight away rather than waiting for the task to run: the master
     * may be faster than our scheduler. */
    set_irq_line(true);
    xTaskNotifyGive(g_task);
    return true;
}

bool dspi_receive(DspiMsg &out, TickType_t wait) {
    if (g_rx_queue == nullptr) {
        return false;
    }
    return xQueueReceive(g_rx_queue, &out, wait) == pdPASS;
}

QueueHandle_t dspi_rx_queue(void) { return g_rx_queue; }

bool dspi_tx_pending(void) {
    if (g_tx_queue == nullptr) {
        return false;
    }
    return g_next_tx != g_idle_frame || uxQueueMessagesWaiting(g_tx_queue) > 0;
}

uint32_t dspi_isr_count(void) { return g_isr_count; }

uint32_t dspi_cs_edge_count(void) { return g_cs_edges; }

bool dspi_cs_level(void) { return gpio_get(IO_DSPI_CS); }

uint32_t dspi_last_partial(void) { return g_last_partial; }

uint32_t dspi_overrun_count(void) { return g_overruns; }

void dspi_debug_dump(void) {
    spi_hw_t *hw = spi_get_hw(DSPI_SPI);

    /* cr1 bit1 = SSE (enabled), bit2 = MS (1 = slave).
     * dmacr bit0 = RXDMAE, bit1 = TXDMAE: without these the PL022 never raises
     * a DMA request and the channels sit idle no matter how they are set up.
     * sr bit0 = TFE (tx empty), bit2 = RNE (rx not empty), bit4 = BSY. */
    printf("spi: cr0=%08lx cr1=%08lx cpsr=%08lx dmacr=%08lx sr=%08lx\n", (unsigned long)hw->cr0, (unsigned long)hw->cr1,
           (unsigned long)hw->cpsr, (unsigned long)hw->dmacr, (unsigned long)hw->sr);

    printf("spi: slave=%d enabled=%d dmacr_rx=%d dmacr_tx=%d\n", (hw->cr1 & SPI_SSPCR1_MS_BITS) ? 1 : 0,
           (hw->cr1 & SPI_SSPCR1_SSE_BITS) ? 1 : 0, (hw->dmacr & SPI_SSPDMACR_RXDMAE_BITS) ? 1 : 0,
           (hw->dmacr & SPI_SSPDMACR_TXDMAE_BITS) ? 1 : 0);

    if (g_rx_chan >= 0 && g_tx_chan >= 0) {
        printf("dma: rx ctrl=%08lx left=%lu busy=%d | tx ctrl=%08lx left=%lu busy=%d\n",
               (unsigned long)dma_channel_hw_addr(g_rx_chan)->ctrl_trig,
               (unsigned long)dma_channel_hw_addr(g_rx_chan)->transfer_count, dma_channel_is_busy(g_rx_chan) ? 1 : 0,
               (unsigned long)dma_channel_hw_addr(g_tx_chan)->ctrl_trig,
               (unsigned long)dma_channel_hw_addr(g_tx_chan)->transfer_count, dma_channel_is_busy(g_tx_chan) ? 1 : 0);
    }

    /* GPIO_FUNC_SPI is 1. Anything else means the pin is not on the bus. */
    printf("mux: sck(%d)=%d out(%d)=%d in(%d)=%d cs(%d)=%d\n", IO_DSPI_SCK, gpio_get_function(IO_DSPI_SCK),
           DSPI_PIN_SLAVE_OUT, gpio_get_function(DSPI_PIN_SLAVE_OUT), DSPI_PIN_SLAVE_IN,
           gpio_get_function(DSPI_PIN_SLAVE_IN), IO_DSPI_CS, gpio_get_function(IO_DSPI_CS));
}

uint32_t dspi_rx_remaining(void) {
    if (g_rx_chan < 0) {
        return 0;
    }
    return dma_channel_hw_addr(g_rx_chan)->transfer_count;
}

uint32_t dspi_rx_count(void) { return g_rx_count; }

uint32_t dspi_idle_rx_count(void) { return g_idle_rx; }

uint32_t dspi_zero_rx_count(void) { return g_zero_rx; }

uint32_t dspi_tx_count(void) { return g_tx_count; }

uint32_t dspi_crc_errors(void) { return g_crc_errors; }

uint32_t dspi_resync_count(void) { return g_resyncs; }

uint32_t dspi_rx_dropped(void) { return g_rx_dropped; }
