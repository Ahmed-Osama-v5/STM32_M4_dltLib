/**
 * @file  dlt_control.c
 * @brief DLT control frame parser + response sender.
 *
 * Parses DLT serial frames from DLT Viewer (PC→MCU).
 * Supported service IDs:
 *   0x03  SET_LOG_LEVEL      — per-context
 *   0x11  SET_DEFAULT_LEVEL  — all contexts
 *   0x13  GET_LOG_INFO       — NOT_SUPPORTED
 */

#include "dlt_control.h"
#include "dlt_frame.h"
#include "dlt_context.h"
#include "dlt_config.h"
#include "dlt_hal.h"
#include <string.h>

/* ── Frame layout offsets (after "DLS\x01" magic is in buf[0..3]) ──── */
/*
 * [0..3]   Serial magic  "DLS\x01"
 * [4]      HTYP          (std header)
 * [5]      MCNT
 * [6..7]   LEN           (big-endian, covers std+ext+payload)
 * [8..11]  TMSP
 * [12..15] ECUID
 *          ↑ std header = 12B  (HTYP+MCNT+LEN+TMSP+ECUID)
 * [16]     MSIN          (ext header)
 * [17]     NOAR
 * [18..21] APID
 * [22..25] CTID          ← ext header = 10B
 * [26..]   payload       ← service_id(4B LE) + service args
 */
#define OFF_HTYP        4u
#define OFF_MCNT        5u
#define OFF_LEN_HI      6u
#define OFF_LEN_LO      7u
#define OFF_TMSP        8u
#define OFF_ECUID       12u
#define OFF_MSIN        16u
#define OFF_NOAR        17u
#define OFF_APID        18u
#define OFF_CTID        22u
#define OFF_PAYLOAD     26u

#define SERIAL_HDR_SIZE     4u
#define STD_HDR_SIZE        12u   /* HTYP+MCNT+LEN(2)+TMSP(4)+ECUID(4) */
#define EXT_HDR_SIZE        10u
#define MIN_FRAME_SIZE      (SERIAL_HDR_SIZE + STD_HDR_SIZE + EXT_HDR_SIZE + 4u)

/* DLT MSIN masks */
#define MSIN_MSTP_MASK      0x0Eu
#define MSIN_MSTP_CTRL      0x06u   /* DLT_TYPE_CONTROL */
#define MSIN_CTRL_REQ       0x10u   /* bit4 = request */

/* Service IDs */
#define SVC_SET_LOG_LEVEL   0x01u
#define SVC_SET_DEF_LEVEL   0x11u
#define SVC_GET_LOG_INFO    0x03u

/* Response status codes */
#define CTRL_OK             0x00u
#define CTRL_NOT_SUPPORTED  0x01u
#define CTRL_ERROR          0x02u

/* RX buffer — large enough for any control frame */
#define RX_BUF_SIZE         80u

static const uint8_t k_magic[4] = { 'D', 'L', 'S', 0x01u };

/* ── Parser state ──── */
typedef enum { ST_WAIT_MAGIC, ST_COLLECT } ctrl_state_t;

static ctrl_state_t s_state     = ST_WAIT_MAGIC;
static uint8_t      s_buf[RX_BUF_SIZE];
static uint8_t      s_magic_idx = 0u;
static uint16_t     s_rx_idx    = 0u;
static uint16_t     s_frame_len = 0u;   /* total bytes expected incl. magic */

/* ── Control response context ──── */
static dlt_ctx_t s_ctrl_ctx  = DLT_CTX_INVALID;
static uint8_t   s_ctrl_mcnt = 0u;

static dlt_ctx_t ctrl_ctx_get(void) {
    if (s_ctrl_ctx == DLT_CTX_INVALID) {
        s_ctrl_ctx = dlt_ctx_register("TOOL", "TEST");
        if (s_ctrl_ctx != DLT_CTX_INVALID)
            dlt_ctx_set_level(s_ctrl_ctx, DLT_LEVEL_VERBOSE);
    }
    return s_ctrl_ctx;
}

static void send_response(dlt_ringbuf_t *rb, uint8_t svc, uint8_t status) {
    dlt_ctx_t ctx = ctrl_ctx_get();
    if (ctx == DLT_CTX_INVALID) return;
    dlt_frame_build_ctrl(rb, ctx, svc, status, s_ctrl_mcnt++);
}

/* ── Reset parser to initial state ──── */
static void parser_reset(void) {
    s_state     = ST_WAIT_MAGIC;
    s_magic_idx = 0u;
    s_rx_idx    = 0u;
    s_frame_len = 0u;
}

/* ── Dispatch a fully-collected frame ──── */
static void dispatch(dlt_ringbuf_t *rb) {
    if (s_rx_idx < MIN_FRAME_SIZE) return;

    uint8_t msin = s_buf[OFF_MSIN];

    /* Must be a control request — identified by MTIN direction bit */
    if (!(msin & MSIN_CTRL_REQ)) return;

    uint8_t svc = s_buf[OFF_PAYLOAD];   /* service_id byte 0 (LE, so this is the ID) */

    switch (svc) {

    /*
     * 0x01 SET_LOG_LEVEL
     * payload layout (after 4B svc_id):
     *   app_id[4]  ctx_id[4]  log_level[1]
     */
    case SVC_SET_LOG_LEVEL: {
        if (s_rx_idx < (OFF_PAYLOAD + 4u + 9u)) {
            send_response(rb, svc, CTRL_ERROR);
            break;
        }
        const char *app = (const char *)&s_buf[OFF_PAYLOAD + 4u];
        const char *ctx = (const char *)&s_buf[OFF_PAYLOAD + 8u];
        uint8_t     lvl =               s_buf[OFF_PAYLOAD + 12u];
        dlt_ctx_set_level_by_ids(app, ctx, (dlt_level_t)lvl);
        send_response(rb, svc, CTRL_OK);
        break;
    }

    /*
     * 0x11 SET_DEFAULT_LOG_LEVEL
     * payload layout (after 4B svc_id):
     *   log_level[1]
     */
    case SVC_SET_DEF_LEVEL: {
        if (s_rx_idx < (OFF_PAYLOAD + 4u + 1u)) {
            send_response(rb, svc, CTRL_ERROR);
            break;
        }
        uint8_t lvl = s_buf[OFF_PAYLOAD + 4u];
        dlt_ctx_set_all_levels((dlt_level_t)lvl);
        if (lvl == 0u) {
            dlt_hal_uart_abort_tx();
            dlt_rb_init(rb);
        }
        send_response(rb, svc, CTRL_OK);
        break;
    }

    /* 0x13 GET_LOG_INFO — too heavy for MCU */
    case SVC_GET_LOG_INFO:
    default:
        send_response(rb, svc, CTRL_NOT_SUPPORTED);
        break;
    }
}

/* ── Public API ──── */

void dlt_ctrl_feed(uint8_t byte, dlt_ringbuf_t *rb) {
    switch (s_state) {

    case ST_WAIT_MAGIC:
        if (byte == k_magic[s_magic_idx]) {
            s_buf[s_magic_idx] = byte;
            s_magic_idx++;
            if (s_magic_idx == 4u) {
                s_rx_idx    = 4u;
                s_frame_len = 0u;
                s_state     = ST_COLLECT;
                s_magic_idx = 0u;
            }
        } else {
            s_magic_idx = 0u;
            if (byte == k_magic[0]) {
                s_buf[0]    = byte;
                s_magic_idx = 1u;
            }
        }
        break;

    case ST_COLLECT:
        if (s_rx_idx >= RX_BUF_SIZE) { parser_reset(); break; }
        s_buf[s_rx_idx++] = byte;

        /* Once we have serial(4) + std_hdr(12) = 16 bytes, extract LEN */
        if (s_frame_len == 0u && s_rx_idx >= (SERIAL_HDR_SIZE + STD_HDR_SIZE)) {
            uint16_t dlt_len = ((uint16_t)s_buf[OFF_LEN_HI] << 8u)
                             |  (uint16_t)s_buf[OFF_LEN_LO];
            /* dlt_len covers std_hdr + ext_hdr + payload */
            s_frame_len = SERIAL_HDR_SIZE + dlt_len;
            if (s_frame_len > RX_BUF_SIZE || s_frame_len < MIN_FRAME_SIZE) {
                parser_reset();
                break;
            }
        }

        if (s_frame_len > 0u && s_rx_idx >= s_frame_len) {
            dispatch(rb);
            parser_reset();
        }
        break;
    }
}
