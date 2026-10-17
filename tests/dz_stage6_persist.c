/* SPDX-License-Identifier: GPL-3.0-only
 * DZ Stage 6 native Golana simulator: real st_* NOR A/B code + reboot loading.
 * Run AFTER the Stage-6 installer and ./build.sh.
 */
#include <stdio.h>
#include "../web/emu/felucca_web.c"
static int fails;
static void ck(int ok, const char *name)
{
    printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) fails++;
}
int main(int argc, char **argv)
{
    uint32_t gen, before, after;
    uint8_t old_slot;
    uint8_t *flash;
    web_nor_erase();
    flash = web_nor();
    /* Fill otherwise-unrecognised bytes inside project A, in an untouched
     * area. A safe allocator must never regard it as available. */
    flash[0x97000u + 0x1800u] = 0xA5u;
    if (argc > 1 && argv[1][0] == 'b') {
        /* Only D free for GX; C and B occupied by foreign residue. */
        flash[0x97000u + 2u * 0x2000u + 0x1800u] = 0x3Cu;
        flash[0x97000u + 1u * 0x2000u + 0x1800u] = 0x7Eu;
        web_boot();
        ck(!dz_store_enabled, "Occupied project slots force RAM-only mode");
        ck(flash[0x97000u + 0x1800u] == 0xA5u, "Foreign project bytes untouched");
        printf("Stage6 blocked: %d failures\n", fails);
        return fails ? 1 : 0;
    }
    web_boot();
    ck(dz_store_enabled && !gx_blocked, "Two project pairs reserved, GX available");
    ck(dz_project_slot[0] != dz_project_slot[1] && dz_project_slot[0] != gx_project_slot &&
       dz_project_slot[1] != gx_project_slot, "DZ and GX ownership disjoint");
    before = web_flash_writes_count();
    dz_model.patterns[0].step[0].note = 60;
    dz_model.patterns[0].step[0].flags = 3;
    dz_model.patterns[0].length = 9;
    dz_model.patterns[63].step[63].note = 127;
    dz_model.patterns[63].step[63].flags = 24;
    dz_model.song_count = 2;
    dz_model.song[1].bank = 3;
    dz_model.song[1].pattern = 15;
    dz_model.song[1].play_steps = 5;
    dz_model.song_mode = 1;
    dz_model.loop_mode = 0;
    dz_store_mark_dirty();
    fm1_ms += 1800u;
    dz_store_poll(&dz_model, 1);
    after = web_flash_writes_count();
    ck(dz_store_have_commit && !dz_store_dirty && after > before, "Flash transaction committed");
    ck(flash[0x97000u + 0x1800u] == 0xA5u, "Foreign project A untouched");
    gen = dz_store_generation;
    old_slot = dz_project_slot[0];
    dz_seq_model_init(&dz_model);
    dz_store_boot(&dz_model);
    ck(dz_store_have_commit && dz_store_generation == gen, "Saved generation recovered");
    ck(dz_model.patterns[0].step[0].note == 60 &&
       dz_model.patterns[0].step[0].flags == 3 &&
       dz_model.patterns[0].length == 9, "First pattern round trip");
    ck(dz_model.patterns[63].step[63].note == 127 &&
       dz_model.patterns[63].step[63].flags == 24, "Last pattern round trip");
    ck(dz_model.song_count == 2 && dz_model.song_mode == 1 &&
       dz_model.song[1].bank == 3 && dz_model.song[1].pattern == 15 &&
       dz_model.song[1].play_steps == 5, "Song and mode round trip");
    /* Write ONLY the first page for generation+1, then simulate a reboot. */
    dz_model.patterns[0].step[0].note = 72;
    dz_store_encode(&dz_store_io, &dz_model, 0u, gen + 1u);
    ck(st_save_to(OBJ_DZ_PAGE0, &dz_store_io, sizeof dz_store_io,
                  (int)(1u - dz_store_copy[0])) == 0, "Inject half-written transaction");
    dz_seq_model_init(&dz_model);
    dz_store_boot(&dz_model);
    ck(dz_store_have_commit && dz_store_generation == gen &&
       dz_model.patterns[0].step[0].note == 60, "Reboot rejects incomplete generation");
    ck(dz_project_slot[0] == old_slot, "Owner selection stable after interrupted save");
    dz_model.patterns[0].step[0].note = 75;
    dz_store_mark_dirty();
    fm1_ms += 1800u;
    dz_store_poll(&dz_model, 1);
    dz_seq_model_init(&dz_model);
    dz_store_boot(&dz_model);
    ck(dz_store_have_commit && dz_store_generation == gen + 1u &&
       dz_model.patterns[0].step[0].note == 75, "Retry and reboot recover new complete generation");
    printf("Stage6 persist: %d failures\n", fails);
    return fails ? 1 : 0;
}
