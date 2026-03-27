/**
 * @file    dlt_context.h
 * @brief   APP ID / CTX ID registry and per-context log level table.
 */

#ifndef DLT_CONTEXT_H
#define DLT_CONTEXT_H

#include <stdint.h>
#include <stdbool.h>
#include "dlt_config.h"

/* Log levels matching DLT spec MTIN values */
typedef enum {
    DLT_LEVEL_OFF     = 0,
    DLT_LEVEL_FATAL   = 1,
    DLT_LEVEL_ERROR   = 2,
    DLT_LEVEL_WARN    = 3,
    DLT_LEVEL_INFO    = 4,
    DLT_LEVEL_DEBUG   = 5,
    DLT_LEVEL_VERBOSE = 6
} dlt_level_t;

/* Opaque context handle — index into context table */
typedef uint8_t dlt_ctx_t;

#define DLT_CTX_INVALID  0xFFu

typedef struct {
    char        app_id[4];
    char        ctx_id[4];
    dlt_level_t level;
    bool        active;
} dlt_context_entry_t;

/** Initialize context registry. */
void dlt_ctx_init(void);

/**
 * Register an APP ID + CTX ID pair.
 * @return Handle (0..N-1) or DLT_CTX_INVALID if table is full.
 */
dlt_ctx_t dlt_ctx_register(const char app_id[4], const char ctx_id[4]);

/** Get pointer to context entry (NULL if invalid handle). */
const dlt_context_entry_t *dlt_ctx_get(dlt_ctx_t ctx);

/** Set log level for a specific context. */
void dlt_ctx_set_level(dlt_ctx_t ctx, dlt_level_t level);

/** Set log level for all registered contexts. */
void dlt_ctx_set_all_levels(dlt_level_t level);

/**
 * Check if a message at @p level should be sent for context @p ctx.
 * Inlined for zero overhead in the hot path.
 */
static inline bool dlt_ctx_is_enabled(dlt_ctx_t ctx, dlt_level_t level) {
    extern dlt_context_entry_t g_dlt_ctx_table[DLT_CFG_MAX_CONTEXTS];
    if (ctx >= DLT_CFG_MAX_CONTEXTS) return false;
    return (g_dlt_ctx_table[ctx].active && level <= g_dlt_ctx_table[ctx].level);
}

void dlt_ctx_set_level_by_ids(const char app_id[4], const char ctx_id[4],
                               dlt_level_t level);

#endif /* DLT_CONTEXT_H */
