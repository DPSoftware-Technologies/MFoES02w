#pragma once

#include <stdint.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

/* Incremental rotary encoder with a push switch, on IO_RE_S1 / IO_RE_S2 /
 * IO_RE_BTN.
 *
 * All three pins are inputs with the internal pull-ups on, so the common pin
 * goes to ground and a closed contact reads low. Both quadrature lines
 * interrupt on either edge; the handler decodes the transition with a table,
 * which rejects the illegal double-edge that contact bounce produces instead
 * of counting it. That is the whole debounce: a bouncing detent oscillates
 * between two adjacent states and nets out to zero.
 *
 * The button is debounced in the task instead, by sampling once the line has
 * been quiet for ENCODER_DEBOUNCE_MS.
 *
 * Rotation is reported in detents, not quadrature edges: a common mechanical
 * encoder runs through a full four-state cycle between clicks. */

/* Quadrature edges per detent. 4 suits the usual detented mechanical part;
 * set to 1 for an optical encoder that should report every edge. */
#define ENCODER_STEPS_PER_DETENT 4

/* Quiet time the button line must hold before its level is believed. */
#define ENCODER_DEBOUNCE_MS 5

/* True = clockwise raises the position. Flip if the wiring is mirrored. */
#define ENCODER_CW_POSITIVE 1

struct EncoderMsg {
    int8_t delta;     /* detents since the last message, + = clockwise, 0 on a button event */
    bool pressed;     /* button state at the time of the message */
    bool button_edge; /* true when this message reports a button change */
    int32_t position; /* accumulated detents since boot */
};

/* Configures the pins, their IRQs and the event task. Safe to call before the
 * scheduler starts. Repeat calls are no-ops that return true. */
bool encoder_init(UBaseType_t task_priority, UBaseType_t queue_depth = 16);

/* Blocks until the next rotation or button change. False on timeout or before
 * init. */
bool encoder_wait(EncoderMsg &out, TickType_t wait = portMAX_DELAY);

QueueHandle_t encoder_event_queue(void);

/* Current state, for callers that poll rather than consume events. */
int32_t encoder_position(void);
bool encoder_button(void);

/* Detents accumulated since the last call to this function, cleared as it is
 * read. Independent of the event queue. */
int32_t encoder_take_delta(void);

/* Raw line levels, for the self test. 1 = released / open. */
bool encoder_line_a(void);
bool encoder_line_b(void);

/* Quadrature transitions that skipped a state, i.e. an edge was missed or the
 * contacts are dirty. A few under fast spinning are normal; a count that
 * tracks every click means the two lines are swapped or one is not connected. */
uint32_t encoder_error_count(void);
