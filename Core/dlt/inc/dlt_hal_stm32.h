/**
 * @file    dlt_hal_stm32.h
 * @brief   STM32 HAL platform abstraction for the DLT MCU library.
 *
 * This header declares the interface that dlt_hal_stm32.c implements.
 * Include this in your application alongside dlt_hal.h.
 *
 * The library core (dlt.c) calls only the functions declared in dlt_hal.h.
 * This file adds STM32-specific extras:
 *   - TX DMA complete callback hook
 *   - Optional debug pin toggle macros
 */

#ifndef DLT_HAL_STM32_H
#define DLT_HAL_STM32_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* ── User config — override in your build system or before this include ─────
 *
 * Example (CMakeLists.txt):
 *   target_compile_definitions(my_app PRIVATE
 *       DLT_HAL_UART_INSTANCE=huart2
 *       DLT_HAL_TIM_INSTANCE=htim5
 *       DLT_HAL_DMA_BUF_SIZE=128
 *   )
 */

#ifndef DLT_HAL_UART_INSTANCE
#  define DLT_HAL_UART_INSTANCE     huart1
#endif

#ifndef DLT_HAL_TIM_INSTANCE
#  define DLT_HAL_TIM_INSTANCE      htim2
#endif

/** DMA TX staging buffer size in bytes. Must be >= max DLT frame size. */
#ifndef DLT_HAL_DMA_BUF_SIZE
#  define DLT_HAL_DMA_BUF_SIZE      64u
#endif

/* ── Core HAL interface (called by dlt.c) ───────────────────────────────────
 *
 * These match the function pointer signatures in dlt_hal.h.
 * If you use the static-dispatch variant of the library (no function pointers),
 * dlt.c will call these directly via weak-symbol resolution.
 */

/**
 * @brief  Send up to @p len bytes over UART (non-blocking, DMA-backed).
 * @return Number of bytes accepted (0 if DMA busy, retry next call).
 */
uint16_t dlt_hal_uart_send(const uint8_t *data, uint16_t len);

/**
 * @brief  Return current timestamp in 0.1 ms ticks (from TIM free-run counter).
 *         Wraps at 2^32 (~4.97 days).
 */
uint32_t dlt_hal_get_timestamp(void);

/**
 * @brief  Enter critical section (disable all interrupts).
 *         Must be paired with dlt_hal_exit_critical().
 */
void dlt_hal_enter_critical(void);

/**
 * @brief  Exit critical section (re-enable interrupts).
 */
void dlt_hal_exit_critical(void);

/**
 * @brief  Check if the UART is busy (DMA in use).
 * 
 * @return true 
 * @return false 
 */
int dlt_hal_uart_busy(void);

/* ── STM32-specific extras ──────────────────────────────────────────────────*/

/**
 * @brief  Must be called from HAL_UART_TxCpltCallback() when the DLT UART
 *         instance fires. Clears the internal DMA-busy flag so the next
 *         dlt_process() call can queue the next chunk.
 *
 * Example wiring in main.c or stm32f4xx_it.c:
 * @code
 *   void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart) {
 *       if (huart->Instance == USART1)
 *           dlt_hal_tx_complete_cb();
 *   }
 * @endcode
 */
void dlt_hal_tx_complete_cb(void);

/**
 * @brief  Feed one received byte into the DLT command parser.
 *         Call this from HAL_UART_RxCpltCallback() for the DLT UART instance.
 *
 * Example wiring:
 * @code
 *   static uint8_t s_rx_byte;
 *
 *   void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
 *       if (huart->Instance == USART1) {
 *           dlt_feed_rx(s_rx_byte);
 *           HAL_UART_Receive_IT(&huart1, &s_rx_byte, 1);
 *       }
 *   }
 * @endcode
 */
void dlt_feed_rx(uint8_t byte);

/* ── Optional debug instrumentation ─────────────────────────────────────────
 *
 * Define DLT_HAL_DEBUG_PIN to toggle a GPIO on TX activity.
 * Useful for oscilloscope timing measurements.
 *
 * Example:
 *   #define DLT_HAL_DEBUG_PIN   HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5)
 */
#ifdef DLT_HAL_DEBUG_PIN
#  define DLT_HAL_DBG_TOGGLE()  DLT_HAL_DEBUG_PIN
#else
#  define DLT_HAL_DBG_TOGGLE()  do {} while(0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* DLT_HAL_STM32_H */
