#include "dlt_ringbuf.h"
#include <string.h>

_Static_assert((DLT_CFG_TX_BUF_SIZE & (DLT_CFG_TX_BUF_SIZE - 1u)) == 0u,
               "DLT_CFG_TX_BUF_SIZE must be a power of 2");

#define MASK (DLT_CFG_TX_BUF_SIZE - 1u)

void dlt_rb_init(dlt_ringbuf_t *rb) {
    rb->head = 0;
    rb->tail = 0;
}

uint16_t dlt_rb_used(const dlt_ringbuf_t *rb) {
    return (uint16_t)((rb->head - rb->tail) & 0xFFFFu);
}

uint16_t dlt_rb_free(const dlt_ringbuf_t *rb) {
    return (uint16_t)(DLT_CFG_TX_BUF_SIZE - dlt_rb_used(rb));
}

bool dlt_rb_write(dlt_ringbuf_t *rb, const uint8_t *data, uint16_t len) {
    if (len == 0)              return true;
    if (dlt_rb_free(rb) < len) return false;  /* drop */

    for (uint16_t i = 0; i < len; i++) {
        rb->buf[rb->head & MASK] = data[i];
        rb->head++;
    }
    return true;
}

uint16_t dlt_rb_read(dlt_ringbuf_t *rb, uint8_t *dst, uint16_t max_len) {
    uint16_t avail = dlt_rb_used(rb);
    uint16_t n     = (avail < max_len) ? avail : max_len;

    for (uint16_t i = 0; i < n; i++) {
        dst[i] = rb->buf[rb->tail & MASK];
        rb->tail++;
    }
    return n;
}

bool dlt_rb_peek(const dlt_ringbuf_t *rb, uint8_t *out) {
    if (dlt_rb_used(rb) == 0) return false;
    *out = rb->buf[rb->tail & MASK];
    return true;
}
