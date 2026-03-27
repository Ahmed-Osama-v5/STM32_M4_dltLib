/**
 * @file    dlt_hal.h
 * @brief   Hardware Abstraction Layer — user must implement these 4 functions.
 *          No other platform dependency exists in the library.
 */

#ifndef DLT_HAL_H
#define DLT_HAL_H

#include <stdint.h>

/**
 * @brief  Send bytes over UART.
 *         MUST be non-blocking (DMA, FIFO, or IT-based).
 *         Called from dlt_process() in the main loop.
 * @param  data  Pointer to byte buffer.
 * @param  len   Number of bytes to send.
 * @return Number of bytes actually accepted by the driver.
 *         Return 0 if the driver is busy — dlt_process() will retry next call.
 */
uint16_t dlt_hal_uart_send(const uint8_t *data, uint16_t len);

/**
 * @brief  Return current timestamp in 0.1ms ticks.
 *         Wraps at UINT32_MAX (~4.97 days of uptime).
 *         Typically driven by SysTick at 1ms → multiply by 10,
 *         or a hardware timer at 100us resolution.
 * @return Timestamp in 0.1ms units.
 */
uint32_t dlt_hal_get_timestamp(void);

/**
 * @brief  Enter critical section.
 *         Disable interrupts to protect shared ring buffer state.
 *         Must be nestable (save/restore PRIMASK on Cortex-M).
 */
void dlt_hal_enter_critical(void);

/**
 * @brief  Exit critical section.
 *         Re-enable interrupts (or restore saved PRIMASK).
 */
void dlt_hal_exit_critical(void);

/**
 * @brief  Abort any in-progress UART TX transfer immediately.
 *         Clears the busy flag so dlt_process() can resume after.
 *         Safe to call from an ISR context.
 */
void dlt_hal_uart_abort_tx(void);

int dlt_hal_uart_busy(void);

#endif /* DLT_HAL_H */
