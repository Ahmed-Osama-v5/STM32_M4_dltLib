#include "dlt_context.h"
#include <string.h>

dlt_context_entry_t g_dlt_ctx_table[DLT_CFG_MAX_CONTEXTS];

void dlt_ctx_init(void) {
    memset(g_dlt_ctx_table, 0, sizeof(g_dlt_ctx_table));
}

dlt_ctx_t dlt_ctx_register(const char app_id[4], const char ctx_id[4]) {
    for (uint8_t i = 0; i < DLT_CFG_MAX_CONTEXTS; i++) {
        if (!g_dlt_ctx_table[i].active) {
            memcpy(g_dlt_ctx_table[i].app_id, app_id, 4);
            memcpy(g_dlt_ctx_table[i].ctx_id, ctx_id, 4);
            g_dlt_ctx_table[i].level  = (dlt_level_t)DLT_CFG_DEFAULT_RUNTIME_LEVEL;
            g_dlt_ctx_table[i].active = true;
            return i;
        }
    }
    return DLT_CTX_INVALID;
}

const dlt_context_entry_t *dlt_ctx_get(dlt_ctx_t ctx) {
    if (ctx >= DLT_CFG_MAX_CONTEXTS || !g_dlt_ctx_table[ctx].active)
        return NULL;
    return &g_dlt_ctx_table[ctx];
}

void dlt_ctx_set_level(dlt_ctx_t ctx, dlt_level_t level) {
    if (ctx < DLT_CFG_MAX_CONTEXTS && g_dlt_ctx_table[ctx].active)
        g_dlt_ctx_table[ctx].level = level;
}

void dlt_ctx_set_all_levels(dlt_level_t level) {
    for (uint8_t i = 0; i < DLT_CFG_MAX_CONTEXTS; i++)
        if (g_dlt_ctx_table[i].active)
            g_dlt_ctx_table[i].level = level;
}

void dlt_ctx_set_level_by_ids(const char app_id[4], const char ctx_id[4],
                               dlt_level_t level)
{
    for (uint8_t i = 0; i < DLT_CFG_MAX_CONTEXTS; i++) {
        if (g_dlt_ctx_table[i].active
            && memcmp(g_dlt_ctx_table[i].app_id, app_id, 4) == 0
            && memcmp(g_dlt_ctx_table[i].ctx_id, ctx_id, 4) == 0)
        {
            g_dlt_ctx_table[i].level = level;
            return;
        }
    }
    /* No match — silently ignore (context may not be registered yet) */
}
