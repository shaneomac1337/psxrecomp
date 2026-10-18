/* test_bios_caller_patch.c — a BIOS counter-patch of the calling game code
 * runs as control flow, and anything else is refused.
 *
 * OpenBIOS answers the Psy-Q kernel patchers' GetB0Table/GetC0Table calls by
 * rewriting the words after the call (patches/patches.c: two NOPs, then a
 * `b +N` with a NOP delay slot, sometimes one more NOP). The runtime keeps the
 * game's bytes as on the disc and instead resumes the guest where the patched
 * words would have taken it. Expected resume points below are worked by hand
 * from the MIPS encodings, not computed the way the code does.
 */
#include "bios_caller_patch.h"

#include <stdio.h>
#include <string.h>

static int s_fails = 0;

static void expect(int cond, const char *what) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", what);
        s_fails++;
    }
}

#define N 16

/* Spyro: Year of the Dragon (SCUS-94467), title overlay func_title_8007EA6C,
 * the 16 words after its GetB0Table call at 0x8007EA7C (OpenBIOS hash
 * 0x5123f82a, "_patch_card_info#1"). */
static const uint32_t card_info_base = 0x8007EA84u;
static const uint32_t card_info_disc[N] = {
    0x240A0009u, /* addiu t2, zero, 9 */
    0x8C42016Cu, /* lw    v0, 0x16C(v0) */
    0x00000000u, /* nop */
    0x20431988u, /* addi  v1, v0, 0x1988 */
    0x0C01926Fu, /* jal   0x800649BC (FlushCache) */
    0xAC600000u, /* sw    zero, 0(v1)  <- the kernel write */
    0x3C1F8008u, 0x8FFF12C4u, 0x00000000u, 0x03E00008u, 0x00000000u,
    0x946F000Au, 0x3C080000u, 0x01E2C025u, 0x37190012u, 0xA479000Au,
};

/* func_title_8007EBDC after its GetB0Table call at 0x8007EBF4 (OpenBIOS hash
 * 0xc29df18f, "_patch_card2#2"). */
static const uint32_t card2_base = 0x8007EBFCu;
static const uint32_t card2_disc[N] = {
    0x8C42016Cu, 0x00000000u, 0x8C4309C8u, 0x3C0A8008u, 0x254AEB34u,
    0x3C098008u, 0x2529EB48u, 0x8D480000u, 0x00000000u, 0xAC4809C8u,
    0x254A0004u, 0x1549FFFBu,
    0x24420004u, /* 0x8007EC2C: addiu v0, v0, 4 (the loop's delay slot) */
    0x0C01926Fu, /* 0x8007EC30: jal FlushCache */
    0x00000000u, 0x3C1F8008u,
};

static uint32_t patched[N];

static int resume(uint32_t base, const uint32_t *disc, PsxCallerPatchResume *out) {
    memset(out, 0xEE, sizeof *out);
    return psx_caller_patch_resume(base, disc, patched, N, out);
}

int main(void) {
    PsxCallerPatchResume r;

    /* ---- 1. _patch_card_info#1: nop, nop, b +0, nop, ., ., nop --------- *
     * b +0 at 0x8007EA8C lands on 0x8007EA90 (now a NOP); the unpatched jal
     * at 0x8007EA94 has a patched NOP delay slot, so the call itself is taken
     * with $ra = 0x8007EA9C. The kernel write at 0x8007EA98 never runs. */
    memcpy(patched, card_info_disc, sizeof patched);
    patched[0] = 0; patched[1] = 0; patched[2] = 0x10000000u; patched[3] = 0; patched[5] = 0;
    expect(psx_caller_patch_changed(card_info_disc, patched, N), "card_info: window changed");
    expect(resume(card_info_base, card_info_disc, &r) == 1, "card_info: supported");
    expect(r.resume_pc == 0x800649BCu, "card_info: resumes in FlushCache");
    expect(r.has_link == 1 && r.link == 0x8007EA9Cu, "card_info: $ra after the jal");

    /* ---- 2. _patch_card2#2: nop, nop, b +9, nop ------------------------ *
     * b +9 at 0x8007EC04 lands on 0x8007EC08 + 9*4 = 0x8007EC2C. */
    memcpy(patched, card2_disc, sizeof patched);
    patched[0] = 0; patched[1] = 0; patched[2] = 0x10000009u; patched[3] = 0;
    expect(resume(card2_base, card2_disc, &r) == 1, "card2: supported");
    expect(r.resume_pc == 0x8007EC2Cu, "card2: resumes after the copy loop");
    expect(r.has_link == 0, "card2: $ra untouched");

    /* ---- 3. Nothing written: resume right where the call returns ------- */
    memcpy(patched, card2_disc, sizeof patched);
    expect(!psx_caller_patch_changed(card2_disc, patched, N), "unchanged: not changed");
    expect(resume(card2_base, card2_disc, &r) == 1, "unchanged: supported");
    expect(r.resume_pc == card2_base && r.has_link == 0, "unchanged: resumes at the return address");

    /* ---- 4. A patched word that is neither NOP nor `b`: refused --------- */
    memcpy(patched, card2_disc, sizeof patched);
    patched[0] = 0x24020001u; /* addiu v0, zero, 1 */
    expect(resume(card2_base, card2_disc, &r) == 0, "arbitrary patched instruction refused");

    /* ---- 5. `b` whose delay slot is an unpatched real instruction ------- */
    memcpy(patched, card2_disc, sizeof patched);
    patched[0] = 0; patched[1] = 0x10000009u; /* delay slot = disc word 2, a load */
    expect(resume(card2_base, card2_disc, &r) == 0, "branch over a live delay slot refused");

    /* ---- 6. Landing on a live instruction followed by a patched one ----- *
     * Resuming there would run the disc word after it, not the patched one. */
    memcpy(patched, card2_disc, sizeof patched);
    patched[0] = 0; patched[1] = 0; patched[2] = 0x10000000u; patched[3] = 0; /* lands on [3] */
    patched[3] = card2_disc[3];  /* [3] stays live ... */
    patched[4] = 0;              /* ... and [4] is patched */
    expect(resume(card2_base, card2_disc, &r) == 0, "live word before a patched one refused");

    /* ---- 7. A patched loop never leaves the window: refused ------------- */
    memcpy(patched, card2_disc, sizeof patched);
    patched[0] = 0x1000FFFFu; patched[1] = 0; /* b . */
    expect(resume(card2_base, card2_disc, &r) == 0, "patched self-loop refused");

    /* ---- 8. Branch out of the window: resume at the target ------------- */
    memcpy(patched, card2_disc, sizeof patched);
    patched[0] = 0; patched[1] = 0; patched[2] = 0x10000020u; patched[3] = 0;
    expect(resume(card2_base, card2_disc, &r) == 1, "branch past the window supported");
    expect(r.resume_pc == card2_base + 0x0Cu + 0x80u, "branch past the window: target");

    if (s_fails) {
        fprintf(stderr, "%d failure(s)\n", s_fails);
        return 1;
    }
    printf("bios_caller_patch_test: all passed\n");
    return 0;
}
