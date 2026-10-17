/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdio.h>
#include "golana_pattern_model.h"

static dz_seq_model_t model;
int main(void)
{
    dz_seq_playback_t p;
    dz_seq_model_init(&model);
    assert(DZ_NPAT == 64 && DZ_NSTEP == 64);
    assert(dz_seq_pat_index(0, 0) == 0);
    assert(dz_seq_pat_index(3, 15) == 63);
    model.patterns[dz_seq_pat_index(3, 15)].step[63].note = 60;
    assert(model.patterns[63].step[63].note == 60);
    assert(model.patterns[0].step[63].note == 0);

    /* SONG KNOB 1 selects an entry but must NOT change any assignment. */
    dz_seq_select_song_step(&model, 4);
    assert(model.song_count == 1 && model.song[4].bank == 0);
    dz_seq_set_song_bank(&model, 1);
    dz_seq_set_song_pattern(&model, 7);
    dz_seq_set_song_steps(&model, 32);
    assert(model.song_count == 5);
    assert(model.song[4].bank == 1 && model.song[4].pattern == 7);
    assert(model.song[4].play_steps == 32);
    dz_seq_select_song_step(&model, 0);
    assert(model.song[0].bank == 0 && model.song[0].pattern == 0);

    /* SONG MODE OFF, ONCE -> the selected pattern plays exactly once. */
    dz_seq_select_pattern(&model, 3, 15);
    model.patterns[63].length = 24;
    model.song_mode = 0;
    model.loop_mode = 0;
    dz_seq_play_start(&model, &p);
    assert(p.bank == 3 && p.pattern == 15 && p.play_steps == 24);
    assert(!dz_seq_play_next(&model, &p) && !p.running);

    /* SONG MODE OFF, LOOP -> the selected pattern repeats. */
    model.loop_mode = 1;
    dz_seq_play_start(&model, &p);
    assert(dz_seq_play_next(&model, &p));
    assert(p.bank == 3 && p.pattern == 15 && p.play_steps == 24);

    /* SONG MODE ON, ONCE -> selected SONG entries in order and then stop. */
    model.song_mode = 1;
    model.loop_mode = 0;
    dz_seq_play_start(&model, &p);
    assert(p.bank == 0 && p.pattern == 0 && p.song_step == 0);
    for (int i = 1; i < 5; ++i)
        assert(dz_seq_play_next(&model, &p) && p.song_step == i);
    assert(p.bank == 1 && p.pattern == 7 && p.play_steps == 32);
    assert(!dz_seq_play_next(&model, &p));

    /* SONG MODE ON, LOOP -> after the last song step start from 0. */
    model.loop_mode = 1;
    dz_seq_play_start(&model, &p);
    for (int i = 0; i < 5; ++i)
        assert(dz_seq_play_next(&model, &p));
    assert(p.song_step == 0 && p.running);
    printf("PASS: 64x64 patterns, bank selection, song-step editing, ONCE/LOOP modes\n");
    return 0;
}
