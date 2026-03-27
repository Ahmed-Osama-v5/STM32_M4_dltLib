/**
 * @file    dlt_ringbuf.h
 * @brief   ISR-safe power-of-2 ring buffer for DLT TX frames.
 */

#ifndef DLT_RINGBUF_H
#define DLT_RINGBUF_H

#include <stdint.h>
#include <stdbool.h>
#include "dlt_config.h"

typedef struct {
    uint8_t  buf[DLT_CFG_TX_BUF_SIZE];
    uint16_t head;   /* write index */
    uint16_t tail;   /* read index  */
} dlt_ringbuf_t;

/** Initialize (zero) the ring buffer. */
void     dlt_rb_init(dlt_ringbuf_t *rb);

/** Number of bytes currently stored. */
uint16_t dlt_rb_used(const dlt_ringbuf_t *rb);

/** Number of free bytes remaining. */
uint16_t dlt_rb_free(const dlt_ringbuf_t *rb);

/**
 * Write @p len bytes atomically.
 * Caller must hold critical section OR guarantee single-writer.
 * @return true on success, false if insufficient space (frame dropped).
 */
bool     dlt_rb_write(dlt_ringbuf_t *rb, const uint8_t *data, uint16_t len);

/**
 * Read up to @p max_len bytes into @p dst.
 * Called from dlt_process() — never from ISR.
 * @return Number of bytes actually read.
 */
uint16_t dlt_rb_read(dlt_ringbuf_t *rb, uint8_t *dst, uint16_t max_len);

/** Peek at the next byte without consuming it. Returns false if empty. */
bool     dlt_rb_peek(const dlt_ringbuf_t *rb, uint8_t *out);

#endif /* DLT_RINGBUF_H */
