#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>
#include <semphr.h>

#include "protocol.h"

/*  tunables  */
#define SB_PICO_AUDIO_QUEUE_DEPTH   32   /* frames buffered for playback  */
#define SB_PICO_I2S_BCK_PIN         26
#define SB_PICO_I2S_WS_PIN          27
#define SB_PICO_I2S_DATA_PIN        28
#define SB_PICO_PWM_AUDIO_PIN       15   /* fallback if no I2S            */

/*  task priorities  */
#define SB_TASK_PRIO_SPI_ISR_DEFER  (configMAX_PRIORITIES - 1)  /* 7 */
#define SB_TASK_PRIO_AUDIO_OUT      (configMAX_PRIORITIES - 2)  /* 6 */
#define SB_TASK_PRIO_CMD            (configMAX_PRIORITIES - 4)  /* 4 */
#define SB_TASK_PRIO_NETWORK        (configMAX_PRIORITIES - 5)  /* 3 */
#define SB_TASK_PRIO_GPS            (configMAX_PRIORITIES - 5)  /* 3 */
#define SB_TASK_PRIO_SENSOR         (configMAX_PRIORITIES - 6)  /* 2 */

/* status sent back on MISO */
extern volatile uint8_t g_sb_status;

/*  command callback type  */
typedef void (*SBCommandHandler)(const char* json, size_t len);

/*  Southbridge Pico class  */
class SouthbridgePico {
public:
    SouthbridgePico(
        spi_inst_t* audio_spi = spi0,
        uint        audio_cs_pin  = 5,
        uint        audio_sck_pin = 2,
        uint        audio_mosi    = 3,
        uint        audio_miso    = 4,
        spi_inst_t* cmd_spi   = spi1,
        uint        cmd_cs_pin    = 13,
        uint        cmd_sck_pin   = 10,
        uint        cmd_mosi      = 11,
        uint        cmd_miso      = 12
    );

    /* call from setup() – starts all internal tasks */
    void begin(bool use_i2s = true);

    /* register application-level command handler */
    void onCommand(SBCommandHandler handler);

    /* send an event/response back to the RPi */
    bool sendEvent(const char* json);

    /* audio stats */
    uint32_t audioOverflows()  const { return _overflow_count; }
    uint32_t audioUnderruns()  const { return _underrun_count; }
    uint32_t framesReceived()  const { return _frames_received; }

private:
    /* SPI hardware */
    spi_inst_t* _audio_spi;
    spi_inst_t* _cmd_spi;
    uint _audio_cs, _audio_sck, _audio_mosi, _audio_miso;
    uint _cmd_cs,   _cmd_sck,   _cmd_mosi,   _cmd_miso;

    bool _use_i2s;

    /* FreeRTOS objects */
    QueueHandle_t    _audio_queue;   /* sb_audio_frame_t* pointers (pool) */
    SemaphoreHandle_t _frame_sem;    /* signals SPI defer task            */

    /* DMA double-buffer */
    uint8_t _dma_buf[2][SB_FRAME_BYTES];
    volatile int _dma_active_buf = 0;

    /* stats */
    volatile uint32_t _overflow_count  = 0;
    volatile uint32_t _underrun_count  = 0;
    volatile uint32_t _frames_received = 0;

    /* callback */
    SBCommandHandler _cmd_handler = nullptr;

    /* pool of frame buffers to avoid heap fragmentation */
    sb_audio_frame_t _frame_pool[SB_PICO_AUDIO_QUEUE_DEPTH];
    QueueHandle_t    _free_pool;

    /* task statics */
    static void _task_spi_defer(void* arg);
    static void _task_audio_out(void* arg);
    static void _task_cmd(void* arg);

    void _setup_spi_slave_dma();
    void _on_frame_complete(const uint8_t* buf);
    void _play_frame(const sb_audio_frame_t* frame);
    void _i2s_init();
    void _pwm_audio_init();
    void _output_samples(const int16_t* pcm, size_t n);
};
