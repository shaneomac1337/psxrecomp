/* bios_caller_patch.c — see bios_caller_patch.h for the contract. */

#include "bios_caller_patch.h"

/* A patched window is a handful of NOPs and one forward branch; anything that
 * needs more steps than this is not a counter-patch. */
#define MAX_STEPS 64u

int psx_caller_patch_changed(const uint32_t *orig, const uint32_t *patched,
                             uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        if (orig[i] != patched[i]) return 1;
    return 0;
}

int psx_caller_patch_resume(uint32_t base, const uint32_t *orig,
                            const uint32_t *patched, uint32_t n,
                            PsxCallerPatchResume *out)
{
    uint32_t pc = base;
    out->link = 0;
    out->has_link = 0;
    for (uint32_t step = 0; step < MAX_STEPS; step++) {
        uint32_t off = pc - base;
        if ((off & 3u) != 0 || off / 4u >= n) {   /* left the window */
            out->resume_pc = pc;
            return 1;
        }
        uint32_t i = off / 4u;
        int next_patched = i + 1 < n && patched[i + 1] != orig[i + 1];
        if (patched[i] == orig[i]) {
            if (!next_patched) {
                out->resume_pc = pc;
                return 1;
            }
            if (orig[i] == 0) {                      /* a NOP on the disc too */
                pc += 4u;
                continue;
            }
            /* A live J/JAL whose delay slot became a NOP: take it here. */
            uint32_t w = orig[i], op = w >> 26;
            if ((op == 2u || op == 3u) && patched[i + 1] == 0) {
                if (op == 3u) {
                    out->link = pc + 8u;
                    out->has_link = 1;
                }
                pc = ((pc + 4u) & 0xF0000000u) | ((w & 0x03FFFFFFu) << 2);
                continue;
            }
            return 0;
        }
        uint32_t w = patched[i];
        if (w == 0) {                                /* NOP */
            pc += 4u;
            continue;
        }
        if ((w & 0xFFFF0000u) == 0x10000000u) {      /* beq $zero,$zero,off */
            if (i + 1 >= n || patched[i + 1] != 0) return 0;
            pc = pc + 4u + (uint32_t)((int32_t)(int16_t)(w & 0xFFFFu) * 4);
            continue;
        }
        return 0;
    }
    return 0;
}
