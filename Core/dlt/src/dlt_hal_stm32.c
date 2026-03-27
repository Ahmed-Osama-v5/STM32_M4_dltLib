/**
 * @file    dlt_hal_stm32.c
 * @brief   STM32 HAL implementation for dlt_hal.h
 *          Tested on STM32F4xx / STM32G4xx with STM32 HAL drivers.
 *
 * Wiring assumptions:
 *   - UART instance : USART1 (change DLT_HAL_UART below)
 *   - TX via DMA    : huart1 DMA TX stream must be configured in CubeMX
 *   - RX via IT     : UART RX interrupt enabled, calls dlt_feed_rx()
 *   - SysTick       : 1ms tick (HAL default) — we derive 0.1ms via TIM2
 *
 * CubeMX checklist:
 *   [ ] USART1: Async, 1000000 baud, 8N1
 *   [ ] USART1 DMA TX: Normal mode, byte width
 *   [ ] USART1 global interrupt: enabled
 *   [ ] TIM2: prescaler=(SystemCoreClock/100000)-1, period=0xFFFFFFFF (free-run)
 */

#include "dlt_hal.h"
#include "stm32f4xx_hal.h"   /* adjust for your series */
#include "stm32f4xx_hal_tim.h"
#include <string.h>

/* ── Config — change to match your CubeMX setup ──────────────────────────── */
#define DLT_HAL_UART        huart1
#define DLT_HAL_TIM         htim2   /* 100 kHz free-running timer → 0.1ms res */

extern UART_HandleTypeDef   DLT_HAL_UART;
extern TIM_HandleTypeDef    DLT_HAL_TIM;

/* ── TX DMA state ─────────────────────────────────────────────────────────── */

/*
 * DMA TX double-buffer:
 * dlt_process() reads from ring buffer into one of two 64-byte chunks,
 * then fires DMA. On DMA complete callback, the flag is cleared.
 * This prevents overwriting an in-flight DMA buffer.
 */
#define DLT_HAL_DMA_BUF_SIZE    64u

static uint8_t  s_dma_buf[DLT_HAL_DMA_BUF_SIZE];
static volatile uint8_t s_dma_busy = 0u;

/**
 * Called from HAL_UART_TxCpltCallback — clears busy flag.
 * Wire this into your stm32f4xx_it.c or main.c callback.
 */
void dlt_hal_tx_complete_cb(void) {
    s_dma_busy = 0u;
}

void dlt_hal_uart_abort_tx(void) {
    HAL_UART_AbortTransmit(&DLT_HAL_UART);
    s_dma_busy = 0u;
}

int dlt_hal_uart_busy(void) {
    return s_dma_busy != 0u;
}

/* ── HAL Implementation ───────────────────────────────────────────────────── */

uint16_t dlt_hal_uart_send(const uint8_t *data, uint16_t len) {
	if (s_dma_busy)  return 0u;
	if (len == 0)    return 0u;

	uint16_t n = (len > DLT_HAL_DMA_BUF_SIZE) ? DLT_HAL_DMA_BUF_SIZE : len;
	memcpy(s_dma_buf, data, n);

	s_dma_busy = 1u;
	if (HAL_UART_Transmit_IT(&DLT_HAL_UART, s_dma_buf, n) != HAL_OK) {
		s_dma_busy = 0u;
		return 0u;
	}
	return n;
}

uint32_t dlt_hal_get_timestamp(void) {
    /* TIM2 counts at 100 kHz → each tick = 0.01ms = 10µs
     * We want 0.1ms ticks → divide by 10                  */
    return (uint32_t)(__HAL_TIM_GET_COUNTER(&DLT_HAL_TIM) / 10u);
}

void dlt_hal_enter_critical(void) {
    __disable_irq();
}

void dlt_hal_exit_critical(void) {
    __enable_irq();
}

/* ── UART RX ISR wiring ───────────────────────────────────────────────────── */

/*
 * In stm32f4xx_it.c, inside USART1_IRQHandler():
 *
 *   void USART1_IRQHandler(void) {
 *       HAL_UART_IRQHandler(&huart1);
 *   }
 *
 * In main.c or a callback file:
 *
 *   static uint8_t s_rx_byte;
 *
 *   void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
 *       if (huart->Instance == USART1) {
 *           dlt_feed_rx(s_rx_byte);
 *           HAL_UART_Receive_IT(&huart1, &s_rx_byte, 1);
 *       }
 *   }
 *
 *   // In app_init(), after dlt_init():
 *   HAL_UART_Receive_IT(&huart1, &s_rx_byte, 1);
 *
 *   // In HAL_UART_TxCpltCallback():
 *   void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart) {
 *       if (huart->Instance == USART1)
 *           dlt_hal_tx_complete_cb();
 *   }
 */
