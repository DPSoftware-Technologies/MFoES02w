#pragma once

/* FreeRTOS V11.3.0 configuration for RP2040 (pico-sdk 2.3.0).
 *
 * Port docs: FreeRTOS-Kernel/portable/ThirdParty/GCC/RP2040/README.md
 * Extra RP2040-only knobs live in that port's include/rp2040_config.h.
 */

#ifndef __ASSEMBLER__
#include "pico.h" /* panic() used by configASSERT below */
#endif

/* ---------------- SMP / core setup ---------------- */
/* 1 = single core (core 0 only, core 1 free for your own code).
 * Set to 2 for the SMP scheduler; then also consider configUSE_CORE_AFFINITY.
 */
#define configNUMBER_OF_CORES 1

#if (configNUMBER_OF_CORES > 1)
/* configTICK_CORE: core that runs SysTick.
 * configRUN_MULTIPLE_PRIORITIES: allow different priorities to run at once.
 * configUSE_CORE_AFFINITY: enables vTaskCoreAffinitySet(). */
#define configTICK_CORE 0
#define configRUN_MULTIPLE_PRIORITIES 1
#define configUSE_CORE_AFFINITY 1
#endif

/* ---------------- Scheduler ---------------- */
#define configUSE_PREEMPTION 1
#define configUSE_TIME_SLICING 1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0
#define configUSE_TICKLESS_IDLE 0
/* Informational only; the port reads clk_sys at runtime. */
#define configCPU_CLOCK_HZ 125000000
#define configTICK_RATE_HZ 1000
#define configMAX_PRIORITIES 32
#define configMINIMAL_STACK_SIZE 256 /* words, not bytes */
#define configMAX_TASK_NAME_LEN 16
#define configTICK_TYPE_WIDTH_IN_BITS TICK_TYPE_WIDTH_32_BITS
#define configIDLE_SHOULD_YIELD 1
#define configUSE_TASK_NOTIFICATIONS 1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES 3
#define configUSE_MUTEXES 1
#define configUSE_RECURSIVE_MUTEXES 1
#define configUSE_COUNTING_SEMAPHORES 1
#define configQUEUE_REGISTRY_SIZE 8
#define configUSE_QUEUE_SETS 0
#define configUSE_APPLICATION_TASK_TAG 0
#define configENABLE_BACKWARD_COMPATIBILITY 0
#define configSTACK_DEPTH_TYPE uint32_t
#define configMESSAGE_BUFFER_LENGTH_TYPE size_t

/* ---------------- Memory ---------------- */
#define configSUPPORT_STATIC_ALLOCATION 0
#define configSUPPORT_DYNAMIC_ALLOCATION 1
/* heap_4 pool; RP2040 has 264K SRAM total. */
#define configTOTAL_HEAP_SIZE (64 * 1024)
#define configAPPLICATION_ALLOCATED_HEAP 0

/* ---------------- Hooks / debug ---------------- */
#define configUSE_IDLE_HOOK 0
#define configUSE_TICK_HOOK 0
#define configUSE_MALLOC_FAILED_HOOK 1
#define configUSE_DAEMON_TASK_STARTUP_HOOK 0
#define configCHECK_FOR_STACK_OVERFLOW 2
#define configGENERATE_RUN_TIME_STATS 0
#define configUSE_TRACE_FACILITY 1
#define configUSE_STATS_FORMATTING_FUNCTIONS 0

/* ---------------- Software timers ---------------- */
#define configUSE_TIMERS 1
#define configTIMER_TASK_PRIORITY (configMAX_PRIORITIES - 1)
#define configTIMER_QUEUE_LENGTH 10
#define configTIMER_TASK_STACK_DEPTH 1024

/* ---------------- Interrupt priorities (Cortex-M0+) ---------------- */
/* M0+ has 4 priority levels in the upper 2 bits of an 8-bit field. */
#define configKERNEL_INTERRUPT_PRIORITY (3 << 6) /* lowest */
#define configMAX_SYSCALL_INTERRUPT_PRIORITY (1 << 6)

/* ---------------- pico-sdk interop ---------------- */
/* Make SDK mutex/sem/critical-section and sleep_ms/busy_wait cooperate with
 * the scheduler instead of blocking the whole core. */
#define configSUPPORT_PICO_SYNC_INTEROP 1
#define configSUPPORT_PICO_TIME_INTEROP 1

/* ---------------- Optional API ---------------- */
#define INCLUDE_vTaskPrioritySet 1
#define INCLUDE_uxTaskPriorityGet 1
#define INCLUDE_vTaskDelete 1
#define INCLUDE_vTaskSuspend 1
#define INCLUDE_vTaskDelayUntil 1
#define INCLUDE_vTaskDelay 1
#define INCLUDE_xTaskGetSchedulerState 1
#define INCLUDE_xTaskGetCurrentTaskHandle 1
#define INCLUDE_uxTaskGetStackHighWaterMark 1
#define INCLUDE_xTaskGetIdleTaskHandle 1
#define INCLUDE_eTaskGetState 1
#define INCLUDE_xTimerPendFunctionCall 1
#define INCLUDE_xTaskAbortDelay 1
#define INCLUDE_xQueueGetMutexHolder 1
#define INCLUDE_xSemaphoreGetMutexHolder 1

/* ---------------- Assert ---------------- */
#ifndef NDEBUG
#define configASSERT(x)                                                                                                \
    if ((x) == 0) {                                                                                                    \
        panic("FreeRTOS assert: %s:%d", __FILE__, __LINE__);                                                           \
    }
#else
#define configASSERT(x)
#endif
