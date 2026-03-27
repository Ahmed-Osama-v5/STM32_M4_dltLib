/**
 * @file    dlt_config.h
 * @brief   Compile-time configuration for the DLT MCU library.
 *          Edit this file to tune the library for your target.
 */

#ifndef DLT_CONFIG_H
#define DLT_CONFIG_H

/* ── ECU Identity ─────────────────────────────────────────────────────────── */

/** 4-char ECU ID padded with nulls. Shown in DLT Viewer's ECU column. */
#define DLT_CFG_ECU_ID                  "DISC"

/* ── Context Registry ─────────────────────────────────────────────────────── */

/** Maximum number of APP ID / CTX ID pairs that can be registered. */
#define DLT_CFG_MAX_CONTEXTS            8

/* ── TX Ring Buffer ───────────────────────────────────────────────────────── */

/**
 * TX ring buffer size in bytes.
 * Must be a power of 2. Minimum recommended: 256.
 * At 1 Mbaud, 512 bytes drains in ~4ms — safe for most tasks.
 */
#define DLT_CFG_TX_BUF_SIZE             512u

/* ── Log Level Filtering ──────────────────────────────────────────────────── */

/**
 * Compile-time minimum log level.
 * Any DLT_x() call below this level is removed by the preprocessor.
 * 1=FATAL, 2=ERROR, 3=WARN, 4=INFO, 5=DEBUG, 6=VERBOSE
 */
#define DLT_CFG_COMPILE_LEVEL           5   /* DEBUG and above kept */

/**
 * Default runtime log level applied to all contexts at init.
 * Can be changed at runtime via dlt_set_default_level() or PC control message.
 */
#define DLT_CFG_DEFAULT_RUNTIME_LEVEL   4   /* INFO */

/* ── Status Reporting ─────────────────────────────────────────────────────── */

/**
 * Interval in milliseconds between automatic status frames sent to PC.
 * Status frame carries: drop_count, buf_usage_%, uptime_sec.
 * Set to 0 to disable automatic status frames.
 */
#define DLT_CFG_STATUS_INTERVAL_MS      5000u

/* ── Payload ──────────────────────────────────────────────────────────────── */

/**
 * Maximum payload size (args only, excluding 4-byte MsgID).
 * DLT standard allows up to 65507 bytes but we cap for MCU safety.
 * Keep <= 255 to fit in a single UART DMA transfer on most platforms.
 */
#define DLT_CFG_MAX_PAYLOAD_ARGS        64u

/* ── Sanity Checks ────────────────────────────────────────────────────────── */

#if (DLT_CFG_TX_BUF_SIZE & (DLT_CFG_TX_BUF_SIZE - 1u)) != 0u
#  error "DLT_CFG_TX_BUF_SIZE must be a power of 2"
#endif

#if DLT_CFG_MAX_CONTEXTS < 1 || DLT_CFG_MAX_CONTEXTS > 32
#  error "DLT_CFG_MAX_CONTEXTS must be between 1 and 32"
#endif

#if DLT_CFG_COMPILE_LEVEL < 1 || DLT_CFG_COMPILE_LEVEL > 6
#  error "DLT_CFG_COMPILE_LEVEL must be between 1 (FATAL) and 6 (VERBOSE)"
#endif

#endif /* DLT_CONFIG_H */
