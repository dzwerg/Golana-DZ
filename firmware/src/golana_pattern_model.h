/* SPDX-License-Identifier: GPL-3.0-only
 * Golana DZ: hardware-independent pattern and song data model.
 * This module is intentionally not wired to the flash or audio ISR yet.
 */
#ifndef GOLANA_PATTERN_MODEL_H
#define GOLANA_PATTERN_MODEL_H
#include <stdint.h>

#define DZ_NBANK 4u
#define DZ_NPAT_PER_BANK 16u
#define DZ_NPAT (DZ_NBANK * DZ_NPAT_PER_BANK)
#define DZ_NSTEP 64u
#define DZ_NSONG 64u

typedef struct {
    uint8_t note;                         /* MIDI note 0..127, 0=rest */
    uint8_t flags;                        /* NOTE/TIE/ACCENT/SLIDE, encoded as in Golana */
} dz_pattern_step_t;

typedef struct {
    uint8_t length;                       /* 1..64, independent of the sound preset */
    dz_pattern_step_t step[DZ_NSTEP];
} dz_pattern_t;

typedef struct {
    uint8_t bank;                         /* 0..3 (A..D) */
    uint8_t pattern;                      /* 0..15 (01..16) */
    uint8_t play_steps;                   /* 1..64, this song occurrence's length */
} dz_song_entry_t;

typedef struct {
    uint8_t bank;                         /* current BANK/PATTERN selection */
    uint8_t pattern;
    uint8_t song_mode;                    /* OFF: selected pattern, ON: arranged song */
    uint8_t loop_mode;                    /* ONCE (0) / LOOP (1) */
    uint8_t song_step;                    /* SONG editing cursor; moving it DOES NOT extend the song */
    uint8_t song_count;                   /* highest edited song step, initially 1 */
    dz_pattern_t patterns[DZ_NPAT];
    dz_song_entry_t song[DZ_NSONG];
} dz_seq_model_t;

typedef struct {
    uint8_t song_mode;                    /* snapshot of mode at PLAY */
    uint8_t loop_mode;
    uint8_t song_step;
    uint8_t running;
    uint8_t bank;
    uint8_t pattern;
    uint8_t play_steps;
} dz_seq_playback_t;

void dz_seq_model_init(dz_seq_model_t *m);
uint8_t dz_seq_pat_index(uint8_t bank, uint8_t pattern);
void dz_seq_select_pattern(dz_seq_model_t *m, uint8_t bank, uint8_t pattern);
void dz_seq_select_song_step(dz_seq_model_t *m, uint8_t step);
void dz_seq_set_song_bank(dz_seq_model_t *m, uint8_t bank);
void dz_seq_set_song_pattern(dz_seq_model_t *m, uint8_t pattern);
void dz_seq_set_song_steps(dz_seq_model_t *m, uint8_t count);
void dz_seq_play_start(const dz_seq_model_t *m, dz_seq_playback_t *p);
/* Called at the end of the current pattern. Returns 1 if a new cycle starts, 0 to stop. */
int dz_seq_play_next(const dz_seq_model_t *m, dz_seq_playback_t *p);
#endif
