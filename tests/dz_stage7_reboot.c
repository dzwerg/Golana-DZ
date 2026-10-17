/* SPDX-License-Identifier: GPL-3.0-only
 * Golana DZ Stage 7: full UI -> idle autosave -> separate-process cold boot -> PLAY.
 * Native host test only. NEVER touches the physical FM-1 or its flash.
 *
 *   cc -O2 -w -Ibuild/gen -Ifirmware/src \
 *     -o build/host/dz_stage7_reboot tests/dz_stage7_reboot.c -lm
 *   build/host/dz_stage7_reboot save   /tmp/dz-stage7-flash.bin
 *   build/host/dz_stage7_reboot reboot /tmp/dz-stage7-flash.bin
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../web/emu/felucca_web.c"

static int failures, checks;
static const uint32_t foreign_offset = 0x97000u + 0x1800u;

static void check(int ok, const char *name)
{
    ++checks;
    printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++failures;
}

static void advance_ms(unsigned ms)
{
    unsigned n = (unsigned)(ms * 44.1 + 0.5);
    while (n) {
        unsigned m = n > 128 ? 128 : n;
        web_render(m);
        n -= m;
    }
}

static void press(unsigned b)
{
    web_buttons(1u << b);
    advance_ms(40);
    web_buttons(0);
    advance_ms(40);
}

static void turn(unsigned role, int detents)
{
    web_enc(role, detents);
    advance_ms(40);
}

static void boot_to_ui(void)
{
    web_boot();
    advance_ms(1600);
    web_keys(1u << 12); /* dismiss Golana splash */
    advance_ms(100);
    web_keys(0);
    advance_ms(300);
    check(gu.ready, "Fresh Golana UI booted");
}

static int write_image(const char *path)
{
    FILE *f = fopen(path, "wb");
    size_t sz = (size_t)web_nor_size();
    if (!f) { perror("fopen write"); return 0; }
    if (fwrite(web_nor(), 1, sz, f) != sz) {
        perror("fwrite NOR"); fclose(f); return 0;
    }
    if (fclose(f)) { perror("fclose NOR"); return 0; }
    return 1;
}

static int read_image(const char *path)
{
    FILE *f = fopen(path, "rb");
    size_t sz = (size_t)web_nor_size();
    int extra;
    if (!f) { perror("fopen read"); return 0; }
    if (fread(web_nor(), 1, sz, f) != sz) {
        fprintf(stderr, "Invalid/truncated NOR image: %s\n", path);
        fclose(f); return 0;
    }
    extra = fgetc(f);
    fclose(f);
    if (extra != EOF) { fprintf(stderr, "NOR image larger than expected\n"); return 0; }
    return 1;
}

static void run_save(const char *path)
{
    uint8_t note;
    uint32_t writes;
    dz_pattern_t *pat;
    step_t before_steps[NSTEP];
    int16_t before_len;
    uint8_t before_dirty;

    web_nor_erase();
    web_nor()[foreign_offset] = 0xA5u; /* project A: nonempty, unknown foreign data */
    boot_to_ui();
    check(dz_store_enabled && !gx_blocked &&
          dz_project_slot[0] != dz_project_slot[1] &&
          dz_project_slot[0] != gx_project_slot &&
          dz_project_slot[1] != gx_project_slot,
          "Persistent DZ owners are distinct from GX");
    check(web_nor()[foreign_offset] == 0xA5u, "Foreign project A preserved at boot");

    memcpy(before_steps, gu_t()->step, sizeof before_steps);
    before_len = gu_t()->p[P_SLEN];
    before_dirty = gu.dirty;
    writes = web_flash_writes_count();

    /* Enter pattern B03 using the real SEQ/ALGO/KNOB/REC panel path. */
    press(B_SEQ);
    turn(EN_ALGO, 1);
    turn(EN_K1, 1); /* A -> B */
    turn(EN_K2, 2); /* 01 -> 03 */
    check(GU_PAGES[gu.page].k[0].id == GV_DZ_BANK &&
          dz_model.bank == 1 && dz_model.pattern == 2,
          "SEQ/ALGO/knobs selected independent pattern B03");

    press(B_REC);
    note = (uint8_t)kb_map(gu_t(), 12);
    web_keys(1u << 12);
    advance_ms(100);
    web_keys(0);
    advance_ms(90);
    press(B_REC); /* REC SETUP LENGTH */
    check(gu.dz_edit_active && GU_PAGES[gu.page].k[0].id == GV_SLEN,
          "REC keyboard entered note; REC SETUP open");
    /* Setting via the real UI setter avoids 60 separate encoder detents. */
    gu_set(&GU_PAGES[gu.page].k[0], 4);
    press(B_SEQ); /* commit selected pattern to RAM, restore synth preset */
    pat = &dz_model.patterns[dz_seq_pat_index(1, 2)];
    check(!gu.dz_edit_active && pat->step[0].note == note && pat->length == 4,
          "REC committed B03 note and length 4");
    check(!memcmp(before_steps, gu_t()->step, sizeof before_steps) &&
          before_len == gu_t()->p[P_SLEN] && before_dirty == gu.dirty,
          "REC restored original synthesizer steps and dirty state");

    /* BANK/PATTERN mode: SONG ON, ONCE. SONG editor: reuse B03 twice,
     * but row 1 plays one step while row 2 plays three. */
    turn(EN_K3, 1);  /* SONG MODE ON */
    turn(EN_K4, -1); /* PLAY MODE ONCE */
    turn(EN_ALGO, 1); /* SONG editor */
    check(GU_PAGES[gu.page].k[0].id == GV_DZ_SONG_STEP,
          "ALGO opened SONG editor");
    turn(EN_K3, 1); /* row 1 BANK B */
    turn(EN_K4, 2); /* row 1 PATTERN 03 */
    gu_set(&GU_PAGES[gu.page].k[1], 1); /* row 1: PLAY STEPS 1 */
    turn(EN_K1, 1); /* browse to row 2: MUST NOT change row 1 */
    check(dz_model.song_count == 1 && dz_model.song_step == 1,
          "Song step browsing does not extend song");
    turn(EN_K3, 1); /* row 2 BANK B */
    turn(EN_K4, 2); /* row 2 PATTERN 03 */
    gu_set(&GU_PAGES[gu.page].k[1], 3); /* row 2: PLAY STEPS 3 */
    check(dz_model.song_mode == 1 && dz_model.loop_mode == 0 &&
          dz_model.song_count == 2 &&
          dz_model.song[0].bank == 1 && dz_model.song[0].pattern == 2 &&
          dz_model.song[0].play_steps == 1 &&
          dz_model.song[1].bank == 1 && dz_model.song[1].pattern == 2 &&
          dz_model.song[1].play_steps == 3,
          "Two SONG rows reuse B03 with separate lengths 1 and 3");

    /* Stage 6 is IDLE AUTOSAVE, not a SAVE button inside REC.
     * No physical flash operations: this is the emulator's NOR array. */
    check(dz_store_dirty && !song.playing, "Pending flash autosave while stopped");
    advance_ms(2300); /* Stage 6 idle write threshold 1500ms */
    check(dz_store_enabled && dz_store_have_commit && !dz_store_dirty &&
          dz_store_generation >= 1u && web_flash_writes_count() > writes,
          "Both A/B pages committed by UI idle autosave");
    check(web_nor()[foreign_offset] == 0xA5u,
          "Foreign project unchanged after UI autosave");

    if (!failures)
        check(write_image(path), "Saved physical-size NOR image to disk");
}

static void run_reboot(const char *path)
{
    step_t original_steps[NSTEP];
    int16_t original_len;
    uint8_t note;
    unsigned tries;
    uint32_t writes;

    if (!read_image(path)) { ++failures; return; }
    boot_to_ui();  /* A NEW PROCESS: BSS and RAM model were reset by the OS. */
    note = (uint8_t)kb_map(gu_t(), 12);
    check(dz_store_enabled && dz_store_have_commit &&
          dz_store_generation >= 1u,
          "Cold boot recovered committed flash generation");
    check(web_nor()[foreign_offset] == 0xA5u,
          "Cold boot preserved foreign project A");
    check(dz_model.bank == 1 && dz_model.pattern == 2 &&
          dz_model.patterns[dz_seq_pat_index(1,2)].length == 4 &&
          dz_model.patterns[dz_seq_pat_index(1,2)].step[0].note == note,
          "Cold boot restored B03 notes, length and selection");
    check(dz_model.song_mode == 1 && dz_model.loop_mode == 0 &&
          dz_model.song_count == 2 &&
          dz_model.song[0].bank == 1 && dz_model.song[0].pattern == 2 &&
          dz_model.song[0].play_steps == 1 &&
          dz_model.song[1].bank == 1 && dz_model.song[1].pattern == 2 &&
          dz_model.song[1].play_steps == 3,
          "Cold boot restored two distinct SONG-step lengths");
    press(B_SEQ);
    turn(EN_ALGO, 1);
    check(gu_get(&GU_PAGES[gu.page].k[0]) == 1 &&
          gu_get(&GU_PAGES[gu.page].k[1]) == 3 &&
          gu_get(&GU_PAGES[gu.page].k[2]) == 1 &&
          gu_get(&GU_PAGES[gu.page].k[3]) == 0,
          "BANK/PATTERN display shows saved values after restart");
    turn(EN_ALGO, 1);
    check(gu_get(&GU_PAGES[gu.page].k[0]) == 2 &&
          gu_get(&GU_PAGES[gu.page].k[1]) == 3 &&
          gu_get(&GU_PAGES[gu.page].k[2]) == 1 &&
          gu_get(&GU_PAGES[gu.page].k[3]) == 3,
          "SONG display shows saved row 2");
    turn(EN_K1, -1);
    check(gu_get(&GU_PAGES[gu.page].k[0]) == 1 &&
          gu_get(&GU_PAGES[gu.page].k[1]) == 1,
          "SONG display recalls saved row 1 without changing it");

    memcpy(original_steps, gu_t()->step, sizeof original_steps);
    original_len = gu_t()->p[P_SLEN];
    writes = web_flash_writes_count();
    press(B_PLAY);
    check(song.playing && dz_play.running && dz_play.song_step == 0 &&
          dz_play.bank == 1 && dz_play.pattern == 2 && dz_play.play_steps == 1,
          "PLAY after cold boot starts saved row 1");
    /* Poll in short increments; do not assume a fixed BPM on every build. */
    for (tries = 0; tries < 200 && song.playing && dz_play.song_step == 0; ++tries)
        advance_ms(20);
    check(song.playing && dz_play.song_step == 1 &&
          dz_play.bank == 1 && dz_play.pattern == 2 && dz_play.play_steps == 3,
          "Audio ISR switched to saved row 2, PLAY STEPS 3");
    check(web_flash_writes_count() == writes,
          "Transport playback performed no NOR flash writes");
    for (tries = 0; tries < 500 && song.playing; ++tries)
        advance_ms(20);
    check(!song.playing && !dz_play_has_backup && !dz_play.running,
          "Song ONCE stops at saved song end");
    check(!memcmp(original_steps, gu_t()->step, sizeof original_steps) &&
          gu_t()->p[P_SLEN] == original_len && gu_t()->seq_n == 0,
          "STOP restores synth sequence and releases held notes");
}

int main(int argc, char **argv)
{
    if (argc != 3 || (strcmp(argv[1], "save") && strcmp(argv[1], "reboot"))) {
        fprintf(stderr, "Usage: %s save|reboot /tmp/dz-stage7-flash.bin\n", argv[0]);
        return 2;
    }
    if (!strcmp(argv[1], "save")) run_save(argv[2]);
    else run_reboot(argv[2]);
    printf("\nDZ Stage 7 %s: %d/%d passed; %d failures\n",
           argv[1], checks - failures, checks, failures);
    return failures ? 1 : 0;
}
