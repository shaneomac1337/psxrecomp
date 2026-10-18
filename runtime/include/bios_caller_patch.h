/* bios_caller_patch.h — a BIOS counter-patch of the calling game code, run as
 * control flow instead of written into guest RAM.
 *
 * Psy-Q's kernel patchers (_patch_card_info, _patch_card2, _patch_pad,
 * _send_pad, _remove_ChgclrPAD, ...) call B0:56 GetC0Table or B0:57
 * GetB0Table and then poke instructions into the retail kernel at fixed
 * offsets. A non-retail BIOS has other code at those offsets. OpenBIOS
 * (patches/patches.c) recognises each patcher by a hash of the 16 words after
 * the call, does the patch's job in its own kernel, and then rewrites the
 * caller: two NOPs, a `b +N` over the kernel pokes with a NOP delay slot, and
 * for one patcher a NOP in a later delay slot.
 *
 * On real hardware the retail BIOS never writes game code. A game that
 * checksums its own code (Spyro: Year of the Dragon's anti-piracy scans read
 * these routines) sees the rewrite as tampering. So the runtime lets the BIOS
 * decide the patch but keeps its stores to the caller's words out of RAM
 * (bios_hle.c), and resumes the guest where the patched words would have
 * taken it. This file is the pure half: given the window as on the disc and
 * as the BIOS wanted it, where does execution go?
 *
 * Supported patched words: NOP, and `beq $zero,$zero,off` (`b`) whose delay
 * slot is a NOP. An unpatched J/JAL whose delay slot was patched to a NOP is
 * taken here too, since resuming at it would run the disc's delay slot, and a
 * word that is a NOP on the disc as well is stepped over.
 * Execution resumes at the first unpatched word whose successor is also
 * unpatched (or outside the window). Anything else is refused.
 */
#ifndef PSXRECOMP_BIOS_CALLER_PATCH_H
#define PSXRECOMP_BIOS_CALLER_PATCH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Words after the call that OpenBIOS hashes and may rewrite. */
#define PSX_CALLER_PATCH_WINDOW_WORDS 16u

typedef struct {
    uint32_t resume_pc;  /* where the guest continues */
    uint32_t link;       /* $ra to set when has_link (a taken JAL) */
    int      has_link;
} PsxCallerPatchResume;

/* Nonzero when any word of the window differs. */
int psx_caller_patch_changed(const uint32_t *orig, const uint32_t *patched,
                             uint32_t n);

/* `base` is the guest address of word 0 (the call's return address). Returns
 * 1 and fills *out when the patched window is supported; 0 otherwise. An
 * unchanged window resumes at `base`. */
int psx_caller_patch_resume(uint32_t base, const uint32_t *orig,
                            const uint32_t *patched, uint32_t n,
                            PsxCallerPatchResume *out);

#ifdef __cplusplus
}
#endif

#endif /* PSXRECOMP_BIOS_CALLER_PATCH_H */
