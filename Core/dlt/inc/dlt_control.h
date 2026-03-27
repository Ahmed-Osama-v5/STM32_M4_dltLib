/**
 * @file    dlt_control.h
 * @brief   PC→MCU DLT control message parser and response sender.
 *          Handles: Set Log Level, Get Log Info, Set Default Log Level.
 */

#ifndef DLT_CONTROL_H
#define DLT_CONTROL_H

#include <stdint.h>
#include "dlt_ringbuf.h"

/**
 * Feed one RX byte into the DLT control frame parser.
 * Call from dlt_feed_rx() only.
 */
void dlt_ctrl_feed(uint8_t byte, dlt_ringbuf_t *tx_rb);

#endif /* DLT_CONTROL_H */
