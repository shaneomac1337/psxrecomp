#pragma once
/* Guest function names for the game EXE.
 *
 * The recompiler reads the game's symbols.toml ([recompiler] symbols in
 * game.toml) and emits a {lo, hi, name} table into the game dispatch file,
 * registered before main(). Runtime telemetry (heartbeat, debug server) uses it
 * to name game code; generated C symbols stay func_XXXXXXXX. See docs/SYMBOLS.md.
 */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t lo;       /* physical start of the named function */
    uint32_t hi;       /* physical end (exclusive): the next compiled entry */
    const char *name;
} PsxGameSymbol;

/* Rows sorted by lo, non-overlapping. The table must outlive the process. */
void psx_game_symbols_register(const PsxGameSymbol *table, uint32_t count);

/* Name of the function containing addr (any segment), or NULL. *offset (if
 * non-NULL) receives addr's byte offset from the function start. */
const char *psx_game_symbol_lookup(uint32_t addr, uint32_t *offset);

uint32_t psx_game_symbol_count(void);

#ifdef __cplusplus
}
#endif
