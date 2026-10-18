# Progressive symbols (PSX)

Discover → label → manipulate. Do not leave newly identified functions as raw
hex in host hooks.

## Map

Game-root `symbols.toml` (scaffolded by New Project Layout):

```toml
[[func]]
pc = 0x80031234
name = "BootEntry"
emit = false
status = "guessed"   # guessed | confirmed | hot
note = "SYSTEM.CNF entry"
```

## Sync

```bash
python3 tools/sync_symbols.py          # → psx_symbols.h (PSX_FN_*)
python3 tools/sync_symbols.py --check  # CI / pre-commit drift check
```

Use `PSX_FN_BootEntry` (or the `func_XXXXXXXX` alias) from host code. Keep
`emit = false` until the entry is safe to own for AOT.

## Names in generated code and runtime telemetry

Point the recompiler at the map to carry names past the header:

```toml
[recompiler]
symbols = "symbols.toml"
```

With it, `psxrecomp-game` (main EXE only):

- labels each named body with a `/* Name */` line above
  `void func_XXXXXXXX(CPUState* cpu)`. C symbols stay `func_XXXXXXXX`, so the
  dispatch table, overlay exports and host hooks are unchanged;
- emits a `{lo, hi, name}` table into `<stem>_dispatch.c` and registers it
  before `main()` (`runtime/include/psx_game_symbols.h`). A row spans the named
  entry up to the next compiled entry.

The runtime uses the table in:

- `psx_freeze_heartbeat.json` (every build): `last_fn_entry` /
  `last_fn_entry_name` for the last compiled function entered, and
  `current_func_name` for the dispatch stamp (`null` when unnamed);
- TCP `symbolize {"addr":"0x80017A10"}` (debug-tools builds): replies
  `name` and `offset` of the containing function, or `name: null`.

Entries with an `overlay` key are skipped: overlay images share guest
addresses, so a pc alone cannot name them. Two different names for one pc, or a
name that is not a C identifier, fail generation. Changing names regenerates
code but changes no guest behaviour.

## Status vs SNES

Metal Warriors ships a fuller toolchain (`bank*.cfg` + `MW_FN_*`). PSX names
reach the header macros, generated-code labels and runtime telemetry (above);
the C symbols themselves stay address-named.
