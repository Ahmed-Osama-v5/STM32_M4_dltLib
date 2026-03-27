/**
 * @file    dlt_frame.h
 * @brief   DLT frame builder — assembles complete DLT serial frames
 *          and writes them into the TX ring buffer.
 */

#ifndef DLT_FRAME_H
#define DLT_FRAME_H

#include <stdint.h>
#include <stdbool.h>
#include "dlt_context.h"
#include "dlt_ringbuf.h"

/* Serial header: 4 bytes */
#define DLT_SERIAL_HDR          { 'D', 'L', 'S', 0x01 }
#define DLT_SERIAL_HDR_LEN      4u

/* Standard header size: HTYP(1)+MCNT(1)+LEN(2)+ECUID(4)+TMSP(4) = 12 bytes */
#define DLT_STD_HDR_LEN         12u

/* Extended header size: MSIN(1)+NOAR(1)+APID(4)+CTID(4) = 10 bytes */
#define DLT_EXT_HDR_LEN         10u

/* Overhead per frame (serial + std + ext + 4B msgid) */
#define DLT_FRAME_OVERHEAD      (DLT_SERIAL_HDR_LEN + DLT_STD_HDR_LEN + DLT_EXT_HDR_LEN + 4u)

/* Overhead for verbose string frame (serial + std + ext + type_info(4) + str_len(2) + null(1)) */
#define DLT_FRAME_STR_OVERHEAD  (DLT_SERIAL_HDR_LEN + DLT_STD_HDR_LEN + DLT_EXT_HDR_LEN + 4u + 2u + 1u)

/*
 * HTYP byte layout (DLT spec 2.2):
 *   bit 0   : UEH  = 1  (use extended header)
 *   bit 1   : MSBF = 0  (little-endian payload — we use LE for args)
 *   bit 2   : WEID = 1  (ECU ID present in std header)
 *   bit 3   : WSID = 0  (no session ID)
 *   bit 4   : WTMS = 1  (timestamp present)
 *   bit 5   : VERS = 1  \
 *   bit 6   : VERS = 0   > version = 0b001
 *   bit 7   : VERS = 0  /
 *
 * Result: 0b0011_0101 = 0x35  (UEH + WEID + WTMS + VERS=1)
 */
#define DLT_HTYP_STANDARD   0x35u

/**
 * @brief Build and enqueue a DLT non-verbose log frame.
 *
 * @param rb        TX ring buffer to write into.
 * @param ctx       Context handle (provides APID, CTID, level check).
 * @param level     Log level (DLT_LEVEL_*).
 * @param msg_id    Non-verbose message ID (matches FIBEX entry).
 * @param args      Packed argument bytes (may be NULL if args_len == 0).
 * @param args_len  Length of args in bytes.
 * @param mcnt      Rolling message counter (caller manages).
 * @param timestamp Timestamp in 0.1ms ticks from dlt_hal_get_timestamp().
 * @return true if frame was enqueued, false if dropped (buffer full).
 */
bool dlt_frame_build(dlt_ringbuf_t       *rb,
                     dlt_ctx_t            ctx,
                     dlt_level_t          level,
                     uint32_t             msg_id,
                     const uint8_t       *args,
                     uint8_t              args_len,
                     uint8_t              mcnt,
                     uint32_t             timestamp);

bool dlt_frame_build_ctrl(dlt_ringbuf_t *rb,
                          dlt_ctx_t      ctx,
                          uint8_t        service_id,
                          uint8_t        status,
                          uint8_t        mcnt);

/**
 * @brief Build and enqueue a DLT verbose log frame with a single ASCII string argument.
 *
 * Sends a verbose message (VERB=1, NOAR=1) — no FIBEX file needed, DLT Viewer
 * decodes and displays the string directly.
 *
 * @param rb        TX ring buffer to write into.
 * @param ctx       Context handle.
 * @param level     Log level.
 * @param str       Null-terminated ASCII string (truncated to DLT_CFG_MAX_PAYLOAD_ARGS chars).
 * @param mcnt      Rolling message counter.
 * @param timestamp Timestamp in 0.1ms ticks.
 * @return true if frame was enqueued, false if dropped.
 */
bool dlt_frame_build_str(dlt_ringbuf_t *rb,
                         dlt_ctx_t      ctx,
                         dlt_level_t    level,
                         const char    *str,
                         uint8_t        mcnt,
                         uint32_t       timestamp);

#endif /* DLT_FRAME_H */
