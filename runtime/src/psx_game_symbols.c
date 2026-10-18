/* Guest function name table (see psx_game_symbols.h). Empty unless the game's
 * generated dispatch file registers one. */
#include "psx_game_symbols.h"

#include <stddef.h>

static const PsxGameSymbol *s_table = NULL;
static uint32_t s_count = 0;

void psx_game_symbols_register(const PsxGameSymbol *table, uint32_t count) {
    s_table = table;
    s_count = table ? count : 0;
}

uint32_t psx_game_symbol_count(void) { return s_count; }

const char *psx_game_symbol_lookup(uint32_t addr, uint32_t *offset) {
    const uint32_t phys = addr & 0x1FFFFFFFu;
    uint32_t lo = 0, hi = s_count;
    while (lo < hi) {               /* first row with row.lo > phys */
        const uint32_t mid = lo + (hi - lo) / 2u;
        if (s_table[mid].lo <= phys) lo = mid + 1u; else hi = mid;
    }
    if (lo == 0) return NULL;
    const PsxGameSymbol *row = &s_table[lo - 1u];
    if (phys >= row->hi) return NULL;
    if (offset) *offset = phys - row->lo;
    return row->name;
}
