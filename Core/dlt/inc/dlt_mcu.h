/**
 * @file    dlt_mcu.h
 * @brief   Public API for the DLT MCU logging library.
 *          Include this file in your application code.
 */

#ifndef DLT_MCU_H
#define DLT_MCU_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "dlt_config.h"
#include "dlt_context.h"


/* ── Status ───────────────────────────────────────────────────────────────── */

typedef enum {
    DLT_OK              = 0,
    DLT_ERR_FULL        = 1,   /* TX buffer full, frame dropped */
    DLT_ERR_CTX_FULL    = 2,   /* Context table full */
    DLT_ERR_INVALID_CTX = 3,
    DLT_ERR_OVERSIZED   = 4,
} dlt_status_t;

typedef struct {
    uint32_t drop_count;        /* frames dropped due to buffer full */
    uint16_t buf_used_bytes;    /* current TX buffer usage */
    uint32_t uptime_01ms;       /* uptime in 0.1ms ticks */
} dlt_stats_t;

/* ── Init & Engine ────────────────────────────────────────────────────────── */

/** Initialize the library. Call once before any logging. */
void dlt_init(void);

/**
 * Call from main loop — drains TX ring buffer via dlt_hal_uart_send().
 * Also sends periodic status frames and handles status interval.
 */
void dlt_process(void);

/**
 * Feed one RX byte from UART into the control message parser.
 * Call from UART RX ISR or polling loop.
 */
void dlt_feed_rx(uint8_t byte);

/* ── Context Registration ─────────────────────────────────────────────────── */

/**
 * Register an APP ID + CTX ID pair.
 * app_id and ctx_id are 4-char strings (no null terminator needed).
 * @return Context handle for use in DLT_x() macros.
 */
dlt_ctx_t dlt_register_context(const char *app_id, const char *ctx_id);

/* ── Runtime Level Control ────────────────────────────────────────────────── */

void dlt_set_level(dlt_ctx_t ctx, dlt_level_t level);
void dlt_set_default_level(dlt_level_t level);

/* ── Core Log Functions ────────────────────────────────────────────────────── */

dlt_status_t dlt_log(dlt_ctx_t     ctx,
                     dlt_level_t   level,
                     uint32_t      msg_id,
                     const uint8_t *args,
                     uint8_t       args_len);

/**
 * Log a printf-formatted string as a DLT verbose message.
 * DLT Viewer decodes and displays it directly — no FIBEX file needed.
 */
dlt_status_t dlt_log_str(dlt_ctx_t ctx, dlt_level_t level, const char *str);

/* ── Stats ────────────────────────────────────────────────────────────────── */

dlt_stats_t dlt_get_stats(void);

/* ── Printf-style Verbose Log Macros ──────────────────────────────────────── */

#include <stdio.h>

#define DLT_LOGF_(ctx, lvl, fmt, ...) do { \
    char _dlt_s[DLT_CFG_MAX_PAYLOAD_ARGS]; \
    snprintf(_dlt_s, sizeof(_dlt_s), fmt, ##__VA_ARGS__); \
    dlt_log_str((ctx), (lvl), _dlt_s); \
} while(0)

#if DLT_CFG_COMPILE_LEVEL >= 1
#  define DLT_LOGF_FATAL(ctx, fmt, ...)   DLT_LOGF_((ctx), DLT_LEVEL_FATAL,   fmt, ##__VA_ARGS__)
#else
#  define DLT_LOGF_FATAL(ctx, fmt, ...)   ((void)0)
#endif

#if DLT_CFG_COMPILE_LEVEL >= 2
#  define DLT_LOGF_ERROR(ctx, fmt, ...)   DLT_LOGF_((ctx), DLT_LEVEL_ERROR,   fmt, ##__VA_ARGS__)
#else
#  define DLT_LOGF_ERROR(ctx, fmt, ...)   ((void)0)
#endif

#if DLT_CFG_COMPILE_LEVEL >= 3
#  define DLT_LOGF_WARN(ctx, fmt, ...)    DLT_LOGF_((ctx), DLT_LEVEL_WARN,    fmt, ##__VA_ARGS__)
#else
#  define DLT_LOGF_WARN(ctx, fmt, ...)    ((void)0)
#endif

#if DLT_CFG_COMPILE_LEVEL >= 4
#  define DLT_LOGF_INFO(ctx, fmt, ...)    DLT_LOGF_((ctx), DLT_LEVEL_INFO,    fmt, ##__VA_ARGS__)
#else
#  define DLT_LOGF_INFO(ctx, fmt, ...)    ((void)0)
#endif

#if DLT_CFG_COMPILE_LEVEL >= 5
#  define DLT_LOGF_DEBUG(ctx, fmt, ...)   DLT_LOGF_((ctx), DLT_LEVEL_DEBUG,   fmt, ##__VA_ARGS__)
#else
#  define DLT_LOGF_DEBUG(ctx, fmt, ...)   ((void)0)
#endif

#if DLT_CFG_COMPILE_LEVEL >= 6
#  define DLT_LOGF_VERBOSE(ctx, fmt, ...) DLT_LOGF_((ctx), DLT_LEVEL_VERBOSE, fmt, ##__VA_ARGS__)
#else
#  define DLT_LOGF_VERBOSE(ctx, fmt, ...) ((void)0)
#endif

/* ── Payload Pack Macros ──────────────────────────────────────────────────── */

/**
 * Usage:
 *   DLT_PACK_BEGIN(buf);
 *   DLT_PACK_U32(buf, speed_rpm);
 *   DLT_PACK_F32(buf, temperature);
 *   DLT_INFO(ctx, 0x00000001, buf);
 */

#define DLT_PACK_BEGIN(name) \
    uint8_t name##_buf[DLT_CFG_MAX_PAYLOAD_ARGS]; \
    uint8_t name##_len = 0; \
    uint8_t *name##_p  = name##_buf

#define DLT_PACK_U8(name, v)  do { name##_p[name##_len++] = (uint8_t)(v); } while(0)

#define DLT_PACK_U16(name, v) do { \
    uint16_t _v = (uint16_t)(v); \
    memcpy(name##_p + name##_len, &_v, 2); name##_len += 2; } while(0)

#define DLT_PACK_U32(name, v) do { \
    uint32_t _v = (uint32_t)(v); \
    memcpy(name##_p + name##_len, &_v, 4); name##_len += 4; } while(0)

#define DLT_PACK_I8(name, v)  DLT_PACK_U8(name, v)
#define DLT_PACK_I16(name, v) DLT_PACK_U16(name, v)
#define DLT_PACK_I32(name, v) DLT_PACK_U32(name, v)

#define DLT_PACK_F32(name, v) do { \
    float _v = (float)(v); \
    memcpy(name##_p + name##_len, &_v, 4); name##_len += 4; } while(0)

#define DLT_PACK_END(name)  /* nothing — name##_buf and name##_len are ready */

/* ── Convenience Log Macros (compile-time level stripping) ───────────────── */

#if DLT_CFG_COMPILE_LEVEL >= 1
#  define DLT_FATAL(ctx, msg_id, name) \
     dlt_log((ctx), DLT_LEVEL_FATAL, (msg_id), name##_buf, name##_len)
#else
#  define DLT_FATAL(ctx, msg_id, name) ((void)0)
#endif

#if DLT_CFG_COMPILE_LEVEL >= 2
#  define DLT_ERROR(ctx, msg_id, name) \
     dlt_log((ctx), DLT_LEVEL_ERROR, (msg_id), name##_buf, name##_len)
#else
#  define DLT_ERROR(ctx, msg_id, name) ((void)0)
#endif

#if DLT_CFG_COMPILE_LEVEL >= 3
#  define DLT_WARN(ctx, msg_id, name) \
     dlt_log((ctx), DLT_LEVEL_WARN, (msg_id), name##_buf, name##_len)
#else
#  define DLT_WARN(ctx, msg_id, name) ((void)0)
#endif

#if DLT_CFG_COMPILE_LEVEL >= 4
#  define DLT_INFO(ctx, msg_id, name) \
     dlt_log((ctx), DLT_LEVEL_INFO, (msg_id), name##_buf, name##_len)
#else
#  define DLT_INFO(ctx, msg_id, name) ((void)0)
#endif

#if DLT_CFG_COMPILE_LEVEL >= 5
#  define DLT_DEBUG(ctx, msg_id, name) \
     dlt_log((ctx), DLT_LEVEL_DEBUG, (msg_id), name##_buf, name##_len)
#else
#  define DLT_DEBUG(ctx, msg_id, name) ((void)0)
#endif

#if DLT_CFG_COMPILE_LEVEL >= 6
#  define DLT_VERBOSE(ctx, msg_id, name) \
     dlt_log((ctx), DLT_LEVEL_VERBOSE, (msg_id), name##_buf, name##_len)
#else
#  define DLT_VERBOSE(ctx, msg_id, name) ((void)0)
#endif

/* No-args shorthand */
#define DLT_LOG_NOARGS(ctx, level, msg_id) \
    dlt_log((ctx), (level), (msg_id), NULL, 0)

#endif /* DLT_MCU_H */
