#include "SouthbridgePico.h"

#include <hardware/spi.h>
#include <hardware/dma.h>
#include <hardware/irq.h>
#include <hardware/pwm.h>
#include <hardware/gpio.h>
#include <pico/stdlib.h>
#include <pico/multicore.h>

#include <cstring>
#include <cstdio>

/*  global status byte (sent on MISO during every audio frame)  */
volatile uint8_t g_sb_status = SB_STATUS_OK;

/*  DMA channel indices  */
static int  _dma_rx_chan  = -1;
static int  _dma_tx_chan  = -1;
static SouthbridgePico* _instance = nullptr;

/*  DMA completion ISR (called after full frame received)  */
static void __isr _dma_rx_complete() {
    if (!dma_channel_get_irq0_status(_dma_rx_chan)) return;
    dma_channel_acknowledge_irq0(_dma_rx_chan);

    if (!_instance) return;

    /* notify the defer task (from ISR context) */
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(_instance->_frame_sem, &woken);
    portYIELD_FROM_ISR(woken);

    /* re-arm DMA to the other buffer */
    int next = 1 - _instance->_dma_active_buf;
    dma_channel_set_write_addr(_dma_rx_chan, _instance->_dma_buf[next], true);
    _instance->_dma_active_buf = next;
}

/*  */
SouthbridgePico::SouthbridgePico(
    spi_inst_t* audio_spi, uint audio_cs, uint audio_sck, uint audio_mosi, uint audio_miso,
    spi_inst_t* cmd_spi,   uint cmd_cs,   uint cmd_sck,   uint cmd_mosi,   uint cmd_miso)
    : _audio_spi(audio_spi), _cmd_spi(cmd_spi)
    , _audio_cs(audio_cs), _audio_sck(audio_sck), _audio_mosi(audio_mosi), _audio_miso(audio_miso)
    , _cmd_cs(cmd_cs), _cmd_sck(cmd_sck), _cmd_mosi(cmd_mosi), _cmd_miso(cmd_miso)
{
    _instance = this;
}

void SouthbridgePico::begin(bool use_i2s) {
    _use_i2s = use_i2s;

    /*  audio queue + free pool  */
    _audio_queue = xQueueCreate(SB_PICO_AUDIO_QUEUE_DEPTH, sizeof(sb_audio_frame_t*));
    _free_pool   = xQueueCreate(SB_PICO_AUDIO_QUEUE_DEPTH, sizeof(sb_audio_frame_t*));
    _frame_sem   = xSemaphoreCreateBinary();

    /* populate free pool */
    for (int i = 0; i < SB_PICO_AUDIO_QUEUE_DEPTH; i++) {
        sb_audio_frame_t* p = &_frame_pool[i];
        xQueueSend(_free_pool, &p, 0);
    }

    /*  audio output init  */
    if (_use_i2s)   _i2s_init();
    else            _pwm_audio_init();

    /*  SPI slave + DMA  */
    _setup_spi_slave_dma();

    /*  FreeRTOS tasks  */
    xTaskCreate(_task_spi_defer, "sb_spi",  512, this, SB_TASK_PRIO_SPI_ISR_DEFER, nullptr);
    xTaskCreate(_task_audio_out, "sb_audio",512, this, SB_TASK_PRIO_AUDIO_OUT,     nullptr);
    xTaskCreate(_task_cmd,       "sb_cmd",  256, this, SB_TASK_PRIO_CMD,           nullptr);
    /* vTaskStartScheduler() is called by arduino-pico's main() */
}

void SouthbridgePico::onCommand(SBCommandHandler handler) {
    _cmd_handler = handler;
}

bool SouthbridgePico::sendEvent(const char* json) {
    if (!json) return false;
    size_t len = strlen(json);
    if (len > SB_CMD_FRAME_BYTES - 4) return false;

    sb_cmd_frame_t frame{};
    frame.magic_hi = SB_CMD_MAGIC_HI;
    frame.magic_lo = SB_CMD_MAGIC_LO;
    frame.len      = (uint8_t)len;
    memcpy(frame.payload, json, len);

    /* queue for cmd task to send on next CS1 transaction */
    /* (simplified: direct SPI write; cmd task serialises access) */
    spi_write_blocking(_cmd_spi, (const uint8_t*)&frame, SB_CMD_FRAME_BYTES);
    return true;
}

/*  SPI slave DMA setup  */
void SouthbridgePico::_setup_spi_slave_dma() {
    /* SPI slave mode */
    spi_init(_audio_spi, 0);   /* clock provided by master */
    spi_set_slave(_audio_spi, true);
    spi_set_format(_audio_spi, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    gpio_set_function(_audio_sck,  GPIO_FUNC_SPI);
    gpio_set_function(_audio_mosi, GPIO_FUNC_SPI);
    gpio_set_function(_audio_miso, GPIO_FUNC_SPI);
    gpio_set_function(_audio_cs,   GPIO_FUNC_SPI);

    /* claim DMA channels */
    _dma_rx_chan = dma_claim_unused_channel(true);
    _dma_tx_chan = dma_claim_unused_channel(true);

    /* RX DMA: SPI DR → _dma_buf[0] */
    dma_channel_config rx_cfg = dma_channel_get_default_config(_dma_rx_chan);
    channel_config_set_transfer_data_size(&rx_cfg, DMA_SIZE_8);
    channel_config_set_dreq(&rx_cfg, spi_get_dreq(_audio_spi, false));
    channel_config_set_read_increment(&rx_cfg, false);
    channel_config_set_write_increment(&rx_cfg, true);
    dma_channel_configure(_dma_rx_chan, &rx_cfg,
        _dma_buf[0],
        &spi_get_hw(_audio_spi)->dr,
        SB_FRAME_BYTES, false);

    /* TX DMA: MISO – send status byte repeated (the status is in rx_buf[3]
     * on the RPi side; we fill the entire MISO stream with g_sb_status
     * at byte 3 and 0xFF elsewhere – simplest approach for fixed frames) */
    /* For now fill MISO with a static status pattern */
    static uint8_t miso_buf[SB_FRAME_BYTES];
    memset(miso_buf, 0xFF, SB_FRAME_BYTES);
    dma_channel_config tx_cfg = dma_channel_get_default_config(_dma_tx_chan);
    channel_config_set_transfer_data_size(&tx_cfg, DMA_SIZE_8);
    channel_config_set_dreq(&tx_cfg, spi_get_dreq(_audio_spi, true));
    channel_config_set_read_increment(&tx_cfg, true);
    channel_config_set_write_increment(&tx_cfg, false);
    dma_channel_configure(_dma_tx_chan, &tx_cfg,
        &spi_get_hw(_audio_spi)->dr,
        miso_buf,
        SB_FRAME_BYTES, false);

    /* IRQ on RX complete */
    dma_channel_set_irq0_enabled(_dma_rx_chan, true);
    irq_set_exclusive_handler(DMA_IRQ_0, _dma_rx_complete);
    irq_set_priority(DMA_IRQ_0, 0);  /* highest HW priority */
    irq_set_enabled(DMA_IRQ_0, true);

    /* start */
    dma_channel_start(_dma_rx_chan);
    dma_channel_start(_dma_tx_chan);
}

/*  SPI defer task (highest FreeRTOS priority)  */
void SouthbridgePico::_task_spi_defer(void* arg) {
    SouthbridgePico* self = static_cast<SouthbridgePico*>(arg);

    for (;;) {
        /* wait for DMA ISR to signal a completed frame */
        xSemaphoreTake(self->_frame_sem, portMAX_DELAY);

        /* the completed buffer is (active_buf ^ 1) */
        int done = 1 - self->_dma_active_buf;
        const uint8_t* buf = self->_dma_buf[done];

        /* validate magic */
        if (buf[0] != SB_FRAME_MAGIC_HI || buf[1] != SB_FRAME_MAGIC_LO) {
            g_sb_status = SB_STATUS_CRC_ERR;
            continue;
        }

        uint8_t ftype = buf[2];

        if (ftype == SB_FTYPE_NOP || ftype == SB_FTYPE_SILENCE) {
            g_sb_status = SB_STATUS_OK;
            continue;
        }

        if (ftype == SB_FTYPE_RESET) {
            /* drain audio queue */
            sb_audio_frame_t* p;
            while (xQueueReceive(self->_audio_queue, &p, 0) == pdTRUE)
                xQueueSend(self->_free_pool, &p, 0);
            g_sb_status = SB_STATUS_OK;
            continue;
        }

        if (ftype == SB_FTYPE_AUDIO) {
            /* validate CRC */
            uint16_t crc_rcv;
            memcpy(&crc_rcv, buf + SB_FRAME_HEADER_BYTES + SB_AUDIO_PAYLOAD_BYTES, 2);
            uint16_t crc_calc = sb_crc16(buf, SB_FRAME_HEADER_BYTES + SB_AUDIO_PAYLOAD_BYTES);
            if (crc_rcv != crc_calc) {
                g_sb_status = SB_STATUS_CRC_ERR;
                continue;
            }

            /* get a free frame from pool */
            sb_audio_frame_t* frame = nullptr;
            if (xQueueReceive(self->_free_pool, &frame, 0) != pdTRUE) {
                /* pool exhausted = overflow */
                self->_overflow_count++;
                g_sb_status = SB_STATUS_OVERFLOW;
                continue;
            }

            memcpy(frame, buf, sizeof(sb_audio_frame_t));
            self->_frames_received++;

            /* push to audio queue */
            if (xQueueSend(self->_audio_queue, &frame,0) != pdTRUE) {
                xQueueSend(self->_free_pool, &frame, 0);
                self->_overflow_count++;
                g_sb_status = SB_STATUS_OVERFLOW;
            } else {
                g_sb_status = SB_STATUS_OK;
            }
        }
    }
}

/*  audio output task  */
void SouthbridgePico::_task_audio_out(void* arg) {
    SouthbridgePico* self = static_cast<SouthbridgePico*>(arg);

    for (;;) {
        sb_audio_frame_t* frame = nullptr;
        if (xQueueReceive(self->_audio_queue, &frame, pdMS_TO_TICKS(5)) == pdTRUE) {
            self->_output_samples(
                reinterpret_cast<const int16_t*>(frame->payload),
                SB_AUDIO_SAMPLES_PER_FRAME * SB_CHANNELS);
            xQueueSend(self->_free_pool, &frame, 0);
            if (g_sb_status == SB_STATUS_UNDERRUN)
                g_sb_status = SB_STATUS_OK;
        } else {
            /* underrun – output silence */
            self->_underrun_count++;
            if (g_sb_status == SB_STATUS_OK)
                g_sb_status = SB_STATUS_UNDERRUN;
            static int16_t silence[SB_AUDIO_SAMPLES_PER_FRAME * SB_CHANNELS] = {};
            self->_output_samples(silence, SB_AUDIO_SAMPLES_PER_FRAME * SB_CHANNELS);
        }
    }
}

/*  command task  */
void SouthbridgePico::_task_cmd(void* arg) {
    SouthbridgePico* self = static_cast<SouthbridgePico*>(arg);

    spi_init(self->_cmd_spi, 0);
    spi_set_slave(self->_cmd_spi, true);
    spi_set_format(self->_cmd_spi, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(self->_cmd_sck,  GPIO_FUNC_SPI);
    gpio_set_function(self->_cmd_mosi, GPIO_FUNC_SPI);
    gpio_set_function(self->_cmd_miso, GPIO_FUNC_SPI);
    gpio_set_function(self->_cmd_cs,   GPIO_FUNC_SPI);

    uint8_t rx[SB_CMD_FRAME_BYTES];

    for (;;) {
        int n = spi_read_blocking(self->_cmd_spi, 0xFF, rx, SB_CMD_FRAME_BYTES);
        if (n == SB_CMD_FRAME_BYTES &&
            rx[0] == SB_CMD_MAGIC_HI && rx[1] == SB_CMD_MAGIC_LO && rx[2] > 0)
        {
            uint8_t len = rx[2];
            if (len <= SB_CMD_FRAME_BYTES - 4 && self->_cmd_handler) {
                /* null-terminate and dispatch */
                char json[SB_CMD_FRAME_BYTES];
                memcpy(json, &rx[4], len);
                json[len] = '\0';
                self->_cmd_handler(json, len);
            }
        }
        taskYIELD();
    }
}

/*  I²S init (using PIO-based I²S from arduino-pico)  */
void SouthbridgePico::_i2s_init() {
    /* arduino-pico provides I2S library; configure it here */
    /* Example uses the pico-extras/pico_audio_i2s library approach */
    /* Actual I2S setup depends on your DAC chip – hook in your setup here */
    /* This placeholder sets up the PIO I2S program */
    gpio_set_function(SB_PICO_I2S_BCK_PIN,  GPIO_FUNC_PIO0);
    gpio_set_function(SB_PICO_I2S_WS_PIN,   GPIO_FUNC_PIO0);
    gpio_set_function(SB_PICO_I2S_DATA_PIN, GPIO_FUNC_PIO0);
    /* Further I2S PIO config would go here (see pico-extras audio examples) */
}

/*  PWM audio fallback  */
void SouthbridgePico::_pwm_audio_init() {
    gpio_set_function(SB_PICO_PWM_AUDIO_PIN, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(SB_PICO_PWM_AUDIO_PIN);
    pwm_config cfg = pwm_get_default_config();
    /* 125 MHz / 256 wrap = ~488 kHz carrier, well above audio */
    pwm_config_set_clkdiv(&cfg, 1.0f);
    pwm_config_set_wrap(&cfg, 255);
    pwm_init(slice, &cfg, true);
}

/*  output samples to DAC  */
void SouthbridgePico::_output_samples(const int16_t* pcm, size_t n) {
    if (_use_i2s) {
        /* Write to I2S FIFO / PIO TX FIFO */
        /* Implementation depends on chosen I2S library (arduino-pico I2S or pico-extras) */
        /* Example: i2s_write_blocking(pcm, n); */
        (void)pcm; (void)n;  /* replace with real I2S write */
    } else {
        /* PWM: convert 16-bit signed → 8-bit unsigned duty cycle */
        uint slice = pwm_gpio_to_slice_num(SB_PICO_PWM_AUDIO_PIN);
        uint chan  = pwm_gpio_to_channel(SB_PICO_PWM_AUDIO_PIN);
        for (size_t i = 0; i < n; i++) {
            uint8_t duty = (uint8_t)((pcm[i] + 32768) >> 8);
            pwm_set_chan_level(slice, chan, duty);
            /* pace: at 16 kHz each sample = 62.5 µs */
            sleep_us(1000000 / SB_SAMPLE_RATE);
        }
    }
}
