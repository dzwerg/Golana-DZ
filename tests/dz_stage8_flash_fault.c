#define _POSIX_C_SOURCE 200809L
/* SPDX-License-Identifier: GPL-3.0-only
 * Stage 8: native full-device simulator; explicit NOR write fault on the SECOND
 * DZ half. Checks user-visible failure, retry, recoverable old transaction.
 */
#define DZ_TEST_FAULTS 1
#include <stdio.h>
#include <string.h>
#include "../web/emu/felucca_web.c"

static int fails, checks;
static void ck(int ok, const char *name)
{
    ++checks;
    printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++fails;
}
static void advance(unsigned ms)
{
    unsigned n = (unsigned)(ms * 44.1 + 0.5);
    while (n) {
        unsigned m = n > 128 ? 128 : n;
        web_render(m);
        n -= m;
    }
}
int main(void)
{
    uint32_t oldgen, slot, before;
    uint8_t oldnote;
    web_nor_erase();
    web_nor()[0x97000u + 0x1800u] = 0xA5u;  /* foreign project's unknown data */
    web_boot();
    advance(1700);
    ck(gu.ready && dz_store_enabled && !gx_blocked,
       "Full UI boot and noncolliding DZ/GX owners");

    dz_model.patterns[0].step[0].note = 60;
    dz_store_mark_dirty();
    advance(1800);
    ck(dz_store_have_commit && !dz_store_dirty && !dz_store_error,
       "Initial two-page autosave committed");
    oldgen = dz_store_generation;
    oldnote = dz_model.patterns[0].step[0].note;
    ck(oldgen != 0, "Baseline committed generation exists");

    dz_model.patterns[0].step[0].note = 72;
    dz_store_mark_dirty();
    slot = dz_store_have_commit ? 1u - dz_store_copy[1] : 0u;
    dz_test_fail_sector = st_sector(OBJ_DZ_PAGE0 + 1u, slot);
    before = web_flash_writes_count();
    for (unsigned waited = 0;
         waited < 12000 && !dz_store_error;
         waited += 40)
        advance(40);
    ck(web_flash_writes_count() > before,
       "First half wrote before second-half injected failure");
    ck(dz_store_error && dz_store_dirty && dz_store_generation == oldgen,
       "Failed second page leaves old generation and dirty retry pending");
    ck(!strcmp(gu.toast, "SAVE FAILED") && gu_toast_on(),
       "SAVE FAILED visibly shown on Golana display");
    ck(web_nor()[0x97000u + 0x1800u] == 0xA5u,
       "Foreign project data untouched during failed transaction");

    dz_test_fail_sector = 0xFFFFFFFFu;
    advance(5400); /* retry guard 5000ms; the original data remain in RAM */
    ck(!dz_store_error && !dz_store_dirty &&
       dz_store_generation == oldgen + 1u,
       "Automatic retry clears error after BOTH pages commit");
    dz_seq_model_init(&dz_model);
    dz_store_boot(&dz_model);
    ck(dz_store_generation == oldgen + 1u &&
       dz_model.patterns[0].step[0].note == 72,
       "Reload recovers the fully retried generation");
    ck(web_nor()[0x97000u + 0x1800u] == 0xA5u,
       "Foreign project data still untouched after retry");

    /* Separate failed generation: old committed data must survive reboot. */
    dz_model.patterns[0].step[0].note = 83;
    dz_store_mark_dirty();
    slot = 1u - dz_store_copy[1];
    dz_test_fail_sector = st_sector(OBJ_DZ_PAGE0 + 1u, slot);
    for (unsigned waited = 0;
         waited < 12000 && !dz_store_error;
         waited += 40)
        advance(40);
    ck(dz_store_error && dz_store_dirty,
       "Second simulated interrupted generation detected");
    dz_seq_model_init(&dz_model);
    dz_store_boot(&dz_model); /* emulate fresh RAM state with same NOR bytes */
    ck(dz_store_have_commit && dz_store_generation == oldgen + 1u &&
       dz_model.patterns[0].step[0].note == 72 &&
       dz_model.patterns[0].step[0].note != 83,
       "Cold reload rejects incomplete generation, keeps last complete song");
    ck(!dz_store_error, "Error latch resets cleanly on boot");
    ck(oldnote == 60, "Original saved note intact before faults");
    printf("\nDZ Stage 8 flash fault: %d/%d passed; %d failures\n", checks - fails, checks, fails);
    return fails ? 1 : 0;
}
