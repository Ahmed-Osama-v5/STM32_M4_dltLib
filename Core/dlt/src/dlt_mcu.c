#include "dlt_mcu.h"
#include "dlt_hal.h"
#include "dlt_ringbuf.h"
#include "dlt_frame.h"
#include "dlt_control.h"
#include "dlt_context.h"
#include "dlt_config.h"

/* ── Internal State ───────────────────────────────────────────────────────── */

static dlt_ringbuf_t s_tx_rb;
static uint32_t      s_drop_count;
static uint8_t       s_mcnt;           /* rolling message counter */
static uint32_t      s_last_status_ts; /* timestamp of last status frame */

/* ── Init ─────────────────────────────────────────────────────────────────── */

void dlt_init(void) {
    dlt_rb_init(&s_tx_rb);
    dlt_ctx_init();
    s_drop_count    = 0u;
    s_mcnt          = 0u;
    s_last_status_ts = 0u;
}

/* ── Context Registration ─────────────────────────────────────────────────── */

dlt_ctx_t dlt_register_context(const char *app_id, const char *ctx_id) {
    return dlt_ctx_register(app_id, ctx_id);
}

/* ── Level Control ────────────────────────────────────────────────────────── */

void dlt_set_level(dlt_ctx_t ctx, dlt_level_t level) {
    dlt_ctx_set_level(ctx, level);
}

void dlt_set_default_level(dlt_level_t level) {
    dlt_ctx_set_all_levels(level);
}

/* ── Core Log ─────────────────────────────────────────────────────────────── */

dlt_status_t dlt_log(dlt_ctx_t     ctx,
                     dlt_level_t   level,
                     uint32_t      msg_id,
                     const uint8_t *args,
                     uint8_t       args_len)
{
    /* Runtime level filter */
    if (!dlt_ctx_is_enabled(ctx, level)) return DLT_OK;

    if (args_len > DLT_CFG_MAX_PAYLOAD_ARGS) return DLT_ERR_OVERSIZED;

    uint32_t ts = dlt_hal_get_timestamp();

    dlt_hal_enter_critical();
    bool ok = dlt_frame_build(&s_tx_rb, ctx, level, msg_id,
                               args, args_len, s_mcnt, ts);
    if (ok) {
        s_mcnt++;
    } else {
        s_drop_count++;
    }
    dlt_hal_exit_critical();

    return ok ? DLT_OK : DLT_ERR_FULL;
}

dlt_status_t dlt_log_str(dlt_ctx_t ctx, dlt_level_t level, const char *str)
{
    if (!dlt_ctx_is_enabled(ctx, level)) return DLT_OK;

    uint32_t ts = dlt_hal_get_timestamp();

    dlt_hal_enter_critical();
    bool ok = dlt_frame_build_str(&s_tx_rb, ctx, level, str, s_mcnt, ts);
    if (ok) {
        s_mcnt++;
    } else {
        s_drop_count++;
    }
    dlt_hal_exit_critical();

    return ok ? DLT_OK : DLT_ERR_FULL;
}

/* ── Status Frame ─────────────────────────────────────────────────────────── */

#if DLT_CFG_STATUS_INTERVAL_MS > 0

/* Status uses a dedicated context — registered internally */
static dlt_ctx_t s_status_ctx = DLT_CTX_INVALID;

#define DLT_STATUS_APP_ID   "DLT"
#define DLT_STATUS_CTX_ID   "STAT"
#define DLT_STATUS_MSG_ID   0xFFFF0001u

static void send_status_frame(void) {
    if (s_status_ctx == DLT_CTX_INVALID) {
        s_status_ctx = dlt_ctx_register(DLT_STATUS_APP_ID, DLT_STATUS_CTX_ID);
        if (s_status_ctx == DLT_CTX_INVALID) return;
        dlt_ctx_set_level(s_status_ctx, DLT_LEVEL_INFO);
    }

    uint8_t  args[9];
    uint32_t dc  = s_drop_count;
    uint8_t  pct = (uint8_t)((dlt_rb_used(&s_tx_rb) * 100u) / DLT_CFG_TX_BUF_SIZE);
    uint32_t up  = dlt_hal_get_timestamp() / 10000u;

    args[0] = (uint8_t)(dc);
    args[1] = (uint8_t)(dc >>  8);
    args[2] = (uint8_t)(dc >> 16);
    args[3] = (uint8_t)(dc >> 24);
    args[4] = pct;
    args[5] = (uint8_t)(up);
    args[6] = (uint8_t)(up >>  8);
    args[7] = (uint8_t)(up >> 16);
    args[8] = (uint8_t)(up >> 24);

    bool ok = dlt_frame_build(&s_tx_rb, s_status_ctx, DLT_LEVEL_INFO,
                               DLT_STATUS_MSG_ID, args, 9u, s_mcnt++,
                               dlt_hal_get_timestamp());
    if (!ok) {
        s_drop_count++;   /* count the status frame drop itself */
    }
}

#endif /* DLT_CFG_STATUS_INTERVAL_MS */

/* ── Process (main loop) ──────────────────────────────────────────────────── */

void dlt_process(void) {
    uint8_t  chunk[64];

    if (dlt_hal_uart_busy()) return;   // ← add this guard
    
    /* Snapshot read under critical section to avoid race with dlt_log() ISR */
    dlt_hal_enter_critical();
    uint16_t n = dlt_rb_read(&s_tx_rb, chunk, sizeof(chunk));
    dlt_hal_exit_critical();

    if (n > 0) {
        dlt_hal_uart_send(chunk, n);
    }

#if DLT_CFG_STATUS_INTERVAL_MS > 0
    uint32_t now = dlt_hal_get_timestamp();
    if ((now - s_last_status_ts) >= (DLT_CFG_STATUS_INTERVAL_MS * 10u)) {
        s_last_status_ts = now;
        dlt_hal_enter_critical();
        send_status_frame();
        dlt_hal_exit_critical();
    }
#endif
}

/* ── RX Feed ──────────────────────────────────────────────────────────────── */

void dlt_feed_rx(uint8_t byte) {
    dlt_ctrl_feed(byte, &s_tx_rb);
}

/* ── Stats ────────────────────────────────────────────────────────────────── */

dlt_stats_t dlt_get_stats(void) {
    dlt_stats_t s;
    s.drop_count     = s_drop_count;
    s.buf_used_bytes = dlt_rb_used(&s_tx_rb);
    s.uptime_01ms    = dlt_hal_get_timestamp();
    return s;
}
