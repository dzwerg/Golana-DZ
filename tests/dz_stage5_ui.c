/* SPDX-License-Identifier: GPL-3.0-only
 * Golana DZ Stage 5: native host integration test for UI, REC and song boundaries.
 * Run ONLY with the current feature/dz-seq-song-banks branch (Stages 2-4 applied).
 * This test does not write the physical FM-1's flash.
 */
#include <stdio.h>
#include <string.h>
#include "../web/emu/felucca_web.c"

static int fails, total;
static float peak;

static void advance(unsigned ms)
{
    unsigned n = (unsigned)(ms * 44.1 + 0.5);
    while (n) {
        unsigned m = n > 128 ? 128 : n;
        web_render(m);
        for (unsigned i = 0; i < m; ++i) {
            float a = out_l[i] < 0 ? -out_l[i] : out_l[i];
            if (a > peak) peak = a;
        }
        n -= m;
    }
}

static void press(unsigned b)
{
    web_buttons(1u << b);
    advance(40);
    web_buttons(0);
    advance(40);
}

static void turn(unsigned role, int steps)
{
    web_enc(role, steps);
    advance(40);
}

static void check(int success, const char *label)
{
    ++total;
    printf("%s %s\n", success ? "PASS" : "FAIL", label);
    if (!success) ++fails;
}

static int32_t knob_value(unsigned k)
{
    return gu_get(&GU_PAGES[gu.page].k[k]);
}

int main(void)
{
    web_nor_erase();
    web_boot();
    advance(1600);
    web_keys(1u << 12); /* skip Golana splash */
    advance(100);
    web_keys(0);
    advance(300);
    check(gu.ready, "Golana simulator booted");
    uint32_t writes_initial = web_flash_writes_count();

    /* ALGO from ANY page enters MicroSTEP's BANK/PATTERN, never cutoff. */
    gu_set(&GU_PAGES[0].k[0], 64);
    int cutoff_initial = gu_t()->p[GL_CUTOFF];
    turn(EN_ALGO, -1);
    check(gu_t()->p[GL_CUTOFF] == cutoff_initial &&
          gu.dz_seq_mode == 1 && GU_PAGES[gu.page].k[0].id == GV_DZ_BANK,
          "ALGO from HOME opens BANK/PATTERN without touching CUTOFF");
    press(B_HOME);
    press(B_SEQ);
    check(gu.dz_seq_mode == 1 && GU_PAGES[gu.page].k[0].id == GV_DZ_BANK,
          "SEQ returns to the selected BANK/PATTERN page");
    turn(EN_ALGO, 1);
    check(gu.dz_seq_mode == 2 && GU_PAGES[gu.page].k[0].id == GV_DZ_SONG_STEP,
          "ALGO toggles to SONG editor");
    turn(EN_ALGO, -1);
    check(gu.dz_seq_mode == 1 && GU_PAGES[gu.page].k[0].id == GV_DZ_BANK &&
          gu_t()->p[GL_CUTOFF] == cutoff_initial,
          "ALGO toggles back to BANK/PATTERN and leaves cutoff unchanged");

    /* Four actual encoder inputs, not direct model setters. */
    turn(EN_K1, 1);
    turn(EN_K2, 2);
    turn(EN_K3, 1);
    turn(EN_K4, -1);
    check(dz_model.bank == 1 && dz_model.pattern == 2 &&
          knob_value(0) == 1 && knob_value(1) == 3,
          "BANK/PATTERN K1/K2 select bank B pattern 03");
    check(dz_model.song_mode == 1 && dz_model.loop_mode == 0 &&
          knob_value(2) == 1 && knob_value(3) == 0,
          "BANK/PATTERN K3/K4 set SONG ON and ONCE");

    turn(EN_ALGO, 1);
    check(gu.dz_seq_mode == 2 && GU_PAGES[gu.page].k[0].id == GV_DZ_SONG_STEP,
          "ALGO advances to SONG editor");
    turn(EN_ALGO, 1);
    check(gu.dz_seq_mode == 1 && GU_PAGES[gu.page].k[0].id == GV_DZ_BANK,
          "ALGO never returns to legacy NORMAL via encoder");
    turn(EN_ALGO, -1);
    check(gu.dz_seq_mode == 2 && GU_PAGES[gu.page].k[0].id == GV_DZ_SONG_STEP,
          "Reverse ALGO returns to SONG");

    /* Selecting another song row must not alter any row or extend song_count. */
    dz_song_entry_t row0 = dz_model.song[0];
    dz_song_entry_t row1 = dz_model.song[1];
    turn(EN_K1, 1);
    check(dz_model.song_step == 1 && dz_model.song_count == 1 &&
          memcmp(&dz_model.song[0], &row0, sizeof row0) == 0 &&
          memcmp(&dz_model.song[1], &row1, sizeof row1) == 0,
          "SONG K1 browses without modifying entries or song length");
    turn(EN_K2, -1); /* 64 -> 63 play steps */
    turn(EN_K3, 2);  /* bank C */
    turn(EN_K4, 4);  /* pattern 05 */
    check(dz_model.song_count == 2 &&
          dz_model.song[1].play_steps == 63 &&
          dz_model.song[1].bank == 2 && dz_model.song[1].pattern == 4,
          "SONG K2/K3/K4 edit only selected row");
    turn(EN_K1, -1);
    check(dz_model.song_step == 0 &&
          memcmp(&dz_model.song[0], &row0, sizeof row0) == 0,
          "SONG returns to unchanged row 1");
    turn(EN_K1, 1);
    check(knob_value(1) == 63 && knob_value(2) == 2 && knob_value(3) == 5,
          "SONG browsing restores each row's displayed values");

    /* The new END option must be selectable from BANK or PATTERN knob. */
    turn(EN_K1, 1); /* song row 3 */
    turn(EN_K3, 4); /* BANK = END */
    char end_name[16];
    gu_text(&GU_PAGES[gu.page].k[2], end_name);
    check(dz_model.song_count == 3 && dz_seq_song_is_end(&dz_model.song[2]) &&
          !strcmp(end_name, "END"), "BANK END creates a terminal song row");
    gu_text(&GU_PAGES[gu.page].k[3], end_name);
    check(!strcmp(end_name, "END"), "PATTERN display also shows END");
    turn(EN_K4, -1); /* PATTERN END -> 16 */
    check(!dz_seq_song_is_end(&dz_model.song[2]), "Turning away from END restores an editable row");
    turn(EN_K4, 1); /* PATTERN END */
    check(dz_seq_song_is_end(&dz_model.song[2]), "PATTERN END creates a terminal song row");
    turn(EN_K1, -2); /* back to row 1 */

    /* Back to BANK/PATTERN, then open the existing REC editor. */
    turn(EN_ALGO, -1);
    check(gu.dz_seq_mode == 1 && dz_model.bank == 1 && dz_model.pattern == 2,
          "BANK/PATTERN selection survives SONG browsing");
    step_t original_steps[NSTEP];
    memcpy(original_steps, gu_t()->step, sizeof original_steps);
    int16_t original_len = gu_t()->p[P_SLEN];
    uint8_t original_dirty = gu.dirty;

    press(B_REC);
    check(gu.dz_edit_active && GU_PAGES[gu.page].k[0].id == GV_SSTEP,
          "REC from BANK/PATTERN opens first REC editor page");
    check(gu.dz_edit_index == dz_seq_pat_index(1, 2),
          "REC edits bank B pattern 03");
    uint8_t rec_note = (uint8_t)kb_map(gu_t(), 12);
    web_keys(1u << 12);
    advance(100);
    web_keys(0);
    advance(90);
    check(gu_t()->step[0].time == ST_NOTE &&
          gu_t()->step[0].note[0] == rec_note,
          "REC keyboard enters the first note");
    press(B_SAVE);
    check(gu.dz_edit_active && !gu.save,
          "SAVE in RAM pattern editor cannot write legacy preset");
    press(B_REC); /* second REC page: SETUP */
    check(gu.dz_edit_active && GU_PAGES[gu.page].k[0].id == GV_SLEN,
          "REC advances to pattern LENGTH setup");
    turn(EN_K1, -1);
    check(gu_t()->p[P_SLEN] == 63, "REC K1 changes independent pattern length");
    press(B_SEQ); /* commit to RAM and restore synthesizer */
    dz_pattern_t *selected = &dz_model.patterns[dz_seq_pat_index(1, 2)];
    check(!gu.dz_edit_active && GU_PAGES[gu.page].k[0].id == GV_DZ_BANK,
          "SEQ exits REC back to BANK/PATTERN");
    check(selected->length == 63 && selected->step[0].note == rec_note,
          "REC saves note and length in selected RAM pattern");
    check(!memcmp(original_steps, gu_t()->step, sizeof original_steps) &&
          gu_t()->p[P_SLEN] == original_len && gu.dirty == original_dirty,
          "REC restores original synth steps, length and dirty state");

    turn(EN_K2, 1);
    check(dz_model.pattern == 3 &&
          dz_model.patterns[dz_seq_pat_index(1, 3)].step[0].note == 0,
          "Another pattern remains independent");
    turn(EN_K2, -1);
    press(B_REC);
    check(gu.dz_edit_active && gu_t()->step[0].note[0] == rec_note,
          "Reopening REC retrieves the saved RAM pattern");
    press(B_SEQ);

    /* Audio-thread song transitions: two different banks/patterns and lengths. */
    dz_model.song_mode = 1;
    dz_model.loop_mode = 0;
    dz_model.song_count = 2;
    dz_model.song[0].bank = 1;
    dz_model.song[0].pattern = 2;
    dz_model.song[0].play_steps = 1;
    dz_model.song[1].bank = 0;
    dz_model.song[1].pattern = 0;
    dz_model.song[1].play_steps = 64;
    dz_model.patterns[0].step[0].note = 72;
    peak = 0;
    press(B_PLAY);
    check(song.playing && dz_play.running && dz_play.song_step == 0 &&
          dz_play.bank == 1 && dz_play.pattern == 2 && dz_play.play_steps == 1,
          "PLAY starts with song row 1 and its 1-step limit");
    advance(1100);
    check(song.playing && dz_play.running && dz_play.song_step == 1 &&
          dz_play.bank == 0 && dz_play.pattern == 0 && dz_play.play_steps == 64 &&
          gu_t()->step[0].note[0] == 72,
          "Audio clock advances to row 2 and loads its pattern/64-step limit");
    press(B_PLAY);
    advance(80);
    check(!song.playing && !dz_play_has_backup && gu_t()->seq_n == 0,
          "STOP clears sequencer notes and playback backup");
    check(!memcmp(original_steps, gu_t()->step, sizeof original_steps) &&
          gu_t()->p[P_SLEN] == original_len,
          "STOP restores the original synth sequence");
    check(web_flash_writes_count() == writes_initial,
          "Stage 5 UI/REC/PLAY caused no NOR flash writes");
    press(B_HOME);
    turn(EN_ALGO, 1);
    check(gu.dz_seq_mode == 1 && GU_PAGES[gu.page].k[0].id == GV_DZ_BANK,
          "ALGO from HOME always opens MicroSTEP menu");
    press(B_REC);
    check(gu.dz_edit_active, "REC from BANK opens selected DZ pattern");
    turn(EN_ALGO, 1);
    check(!gu.dz_edit_active && gu.dz_seq_mode == 1 && GU_PAGES[gu.page].k[0].id == GV_DZ_BANK,
          "ALGO during REC commits DZ pattern and returns to MicroSTEP menu");
    press(B_SAVE);
    check(gu.save, "SAVE opens WRITE dialog");
    turn(EN_ALGO, 1);
    check(!gu.save && gu.dz_seq_mode == 1 && GU_PAGES[gu.page].k[0].id == GV_DZ_BANK,
          "ALGO during WRITE safely cancels dialog and opens MicroSTEP menu");

    printf("\nDZ Stage 5 UI / REC / song: %d/%d passed; %d failures\n",
           total - fails, total, fails);
    return fails ? 1 : 0;
}
