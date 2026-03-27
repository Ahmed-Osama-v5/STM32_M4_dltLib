#include "dlt_hal.h"
#include "dlt_frame.h"
#include "dlt_context.h"
#include "dlt_config.h"
#include <string.h>

/* Write a big-endian uint16 into buf at offset */
static inline void put_be16(uint8_t *buf, uint16_t v) {
    buf[0] = (uint8_t)(v >> 8);
    buf[1] = (uint8_t)(v);
}

/* Write a big-endian uint32 into buf at offset */
static inline void put_be32(uint8_t *buf, uint32_t v) {
    buf[0] = (uint8_t)(v >> 24);
    buf[1] = (uint8_t)(v >> 16);
    buf[2] = (uint8_t)(v >>  8);
    buf[3] = (uint8_t)(v);
}

/* Write a little-endian uint32 into buf at offset */
static inline void put_le32(uint8_t *buf, uint32_t v) {
    buf[0] = (uint8_t)(v);
    buf[1] = (uint8_t)(v >>  8);
    buf[2] = (uint8_t)(v >> 16);
    buf[3] = (uint8_t)(v >> 24);
}

bool dlt_frame_build(dlt_ringbuf_t       *rb,
                     dlt_ctx_t            ctx,
                     dlt_level_t          level,
                     uint32_t             msg_id,
                     const uint8_t       *args,
                     uint8_t              args_len,
                     uint8_t              mcnt,
                     uint32_t             timestamp)
{
    const dlt_context_entry_t *entry = dlt_ctx_get(ctx);
    if (!entry) return false;

    /* Total frame size */
    uint16_t payload_len = 4u + args_len;  /* MsgID(4) + args */
    uint16_t frame_len   = DLT_SERIAL_HDR_LEN + DLT_STD_HDR_LEN
                         + DLT_EXT_HDR_LEN + payload_len;

    /* Guard against oversized frames */
    if (args_len > DLT_CFG_MAX_PAYLOAD_ARGS) return false;

    /* Assemble into a local stack buffer — max size is bounded */
    uint8_t frame[DLT_FRAME_OVERHEAD + DLT_CFG_MAX_PAYLOAD_ARGS];

    uint8_t *p = frame;

    /* ── Serial Header (4B) ─────────────────────────────────── */
    *p++ = 'D'; *p++ = 'L'; *p++ = 'S'; *p++ = 0x01u;

    /* ── Standard Header (12B) ──────────────────────────────── */
    /* LEN covers from HTYP to end of frame (excludes serial header) */
    uint16_t dlt_len = DLT_STD_HDR_LEN + DLT_EXT_HDR_LEN + payload_len;

    *p++ = DLT_HTYP_STANDARD;          /* HTYP  */
    *p++ = mcnt;                        /* MCNT  */
    put_be16(p, dlt_len); p += 2;      /* LEN   (big-endian) */
    memcpy(p, DLT_CFG_ECU_ID, 4); p += 4; /* ECUID (WEID=1) */
    put_be32(p, timestamp); p += 4;    /* TMSP  (big-endian, 0.1ms) */

    /* ── Extended Header (10B) ──────────────────────────────── */
    /* MSIN: VERB=0, MSTP=000 (LOG), MTIN=level in bits [7:4] */
    *p++ = (uint8_t)((level & 0x0Fu) << 4u); /* MSIN */
    *p++ = 0x00u;                             /* NOAR (non-verbose) */
    memcpy(p, entry->app_id, 4); p += 4;     /* APID */
    memcpy(p, entry->ctx_id, 4); p += 4;     /* CTID */

    /* ── Payload: MsgID (4B LE) + args ─────────────────────── */
    put_le32(p, msg_id); p += 4;
    if (args && args_len > 0) {
        memcpy(p, args, args_len);
        p += args_len;
    }

    /* Sanity check */
    if ((uint16_t)(p - frame) != frame_len) return false;

    return dlt_rb_write(rb, frame, frame_len);
}

bool dlt_frame_build_ctrl(dlt_ringbuf_t *rb,
                          dlt_ctx_t      ctx,
                          uint8_t        service_id,
                          uint8_t        status,
                          uint8_t        mcnt)
{
    const dlt_context_entry_t *entry = dlt_ctx_get(ctx);
    if (!entry) return false;

    /* Control response payload: 4B service_id (LE) + 1B status = 5B */
    uint16_t payload_len = 5u;
    uint16_t dlt_len     = DLT_STD_HDR_LEN + DLT_EXT_HDR_LEN + payload_len;
    uint16_t frame_len   = DLT_SERIAL_HDR_LEN + dlt_len;

    uint8_t frame[DLT_SERIAL_HDR_LEN + DLT_STD_HDR_LEN + DLT_EXT_HDR_LEN + 5u];
    uint8_t *p = frame;

    /* Serial header */
    *p++ = 'D'; *p++ = 'L'; *p++ = 'S'; *p++ = 0x01u;

    /* Standard header */
    *p++ = DLT_HTYP_STANDARD;
    *p++ = mcnt;
    put_be16(p, dlt_len); p += 2;
    memcpy(p, DLT_CFG_ECU_ID, 4); p += 4;
    put_be32(p, dlt_hal_get_timestamp()); p += 4;

    /* Extended header — MSIN=0x26: MSTP=CTRL(0x06), direction=response(0x20) */
    *p++ = 0x26u;
    *p++ = 0x00u;                        /* NOAR=0, non-verbose ctrl */
    memcpy(p, entry->app_id, 4); p += 4;
    memcpy(p, entry->ctx_id, 4); p += 4;

    /* Payload: service_id (4B LE) + status (1B) */
    put_le32(p, (uint32_t)service_id); p += 4;
    *p++ = status;

    if ((uint16_t)(p - frame) != frame_len) return false;
    return dlt_rb_write(rb, frame, frame_len);
}

bool dlt_frame_build_str(dlt_ringbuf_t *rb,
                         dlt_ctx_t      ctx,
                         dlt_level_t    level,
                         const char    *str,
                         uint8_t        mcnt,
                         uint32_t       timestamp)
{
    const dlt_context_entry_t *entry = dlt_ctx_get(ctx);
    if (!entry || !str) return false;

    /* Clamp string to max allowed length */
    uint16_t slen = 0u;
    while (str[slen] && slen < DLT_CFG_MAX_PAYLOAD_ARGS) { slen++; }
    uint16_t str_payload = 4u + 2u + slen + 1u; /* type_info + len field + chars + null */

    uint16_t dlt_len   = DLT_STD_HDR_LEN + DLT_EXT_HDR_LEN + str_payload;
    uint16_t frame_len = DLT_SERIAL_HDR_LEN + dlt_len;

    uint8_t frame[DLT_FRAME_STR_OVERHEAD + DLT_CFG_MAX_PAYLOAD_ARGS];
    uint8_t *p = frame;

    /* Serial header */
    *p++ = 'D'; *p++ = 'L'; *p++ = 'S'; *p++ = 0x01u;

    /* Standard header */
    *p++ = DLT_HTYP_STANDARD;
    *p++ = mcnt;
    put_be16(p, dlt_len); p += 2;
    memcpy(p, DLT_CFG_ECU_ID, 4); p += 4;
    put_be32(p, timestamp); p += 4;

    /* Extended header — VERB=1 (bit 0), MSTP=LOG (000), MTIN=level in [7:4] */
    *p++ = (uint8_t)(((level & 0x0Fu) << 4u) | 0x01u); /* MSIN: verbose */
    *p++ = 0x01u;                                        /* NOAR = 1 */
    memcpy(p, entry->app_id, 4); p += 4;
    memcpy(p, entry->ctx_id, 4); p += 4;

    /* Verbose payload: type_info (4B LE) — STRG=bit9=0x200, SCOD=ASCII=0 */
    put_le32(p, 0x00000200u); p += 4;
    /* String length field (2B LE): includes null terminator */
    p[0] = (uint8_t)(slen + 1u);
    p[1] = (uint8_t)((slen + 1u) >> 8);
    p += 2;
    /* String data + null */
    memcpy(p, str, slen); p += slen;
    *p++ = 0x00u;

    if ((uint16_t)(p - frame) != frame_len) return false;
    return dlt_rb_write(rb, frame, frame_len);
}
