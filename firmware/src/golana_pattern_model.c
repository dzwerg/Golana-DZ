/* SPDX-License-Identifier: GPL-3.0-only */
#include "golana_pattern_model.h"

static uint8_t dz_min_u8(uint8_t value, uint8_t max)
{
    return value > max ? max : value;
}

uint8_t dz_seq_pat_index(uint8_t bank, uint8_t pattern)
{
    return (uint8_t)(dz_min_u8(bank, DZ_NBANK - 1u) * DZ_NPAT_PER_BANK +
                     dz_min_u8(pattern, DZ_NPAT_PER_BANK - 1u));
}

void dz_seq_model_init(dz_seq_model_t *m)
{
    uint32_t i;
    uint8_t *dst = (uint8_t *)m;
    for (i = 0; i < sizeof *m; ++i) dst[i] = 0;
    m->song_count = 1u;
    m->loop_mode = 1u;
    for (i = 0; i < DZ_NPAT; ++i)
        m->patterns[i].length = DZ_NSTEP;
    for (i = 0; i < DZ_NSONG; ++i)
        m->song[i].play_steps = DZ_NSTEP;
}

void dz_seq_select_pattern(dz_seq_model_t *m, uint8_t bank, uint8_t pattern)
{
    m->bank = dz_min_u8(bank, DZ_NBANK - 1u);
    m->pattern = dz_min_u8(pattern, DZ_NPAT_PER_BANK - 1u);
}

void dz_seq_select_song_step(dz_seq_model_t *m, uint8_t step)
{
    m->song_step = dz_min_u8(step, DZ_NSONG - 1u);
}

int dz_seq_song_is_end(const dz_song_entry_t *e)
{
    return e->bank == DZ_SONG_END_BANK && e->pattern == DZ_SONG_END_PATTERN;
}

static dz_song_entry_t *dz_song_edit(dz_seq_model_t *m)
{
    if (m->song_step >= m->song_count)
        m->song_count = (uint8_t)(m->song_step + 1u);
    return &m->song[m->song_step];
}

void dz_seq_set_song_bank(dz_seq_model_t *m, uint8_t bank)
{
    dz_song_entry_t *e = dz_song_edit(m);
    if (bank >= DZ_SONG_END_BANK) {
        e->bank = DZ_SONG_END_BANK;
        e->pattern = DZ_SONG_END_PATTERN;
    } else {
        /* Moving away from END creates a normal A01-like row, not a half-END. */
        if (dz_seq_song_is_end(e)) e->pattern = 0u;
        e->bank = bank;
    }
}

void dz_seq_set_song_pattern(dz_seq_model_t *m, uint8_t pattern)
{
    dz_song_entry_t *e = dz_song_edit(m);
    if (pattern >= DZ_SONG_END_PATTERN) {
        e->bank = DZ_SONG_END_BANK;
        e->pattern = DZ_SONG_END_PATTERN;
    } else {
        if (dz_seq_song_is_end(e)) e->bank = 0u;
        e->pattern = pattern;
    }
}

void dz_seq_set_song_steps(dz_seq_model_t *m, uint8_t count)
{
    if (count == 0)
        count = 1u;
    dz_song_edit(m)->play_steps = dz_min_u8(count, DZ_NSTEP);
}

static void dz_seq_play_song_entry(const dz_seq_model_t *m, dz_seq_playback_t *p)
{
    const dz_song_entry_t *e = &m->song[p->song_step];
    p->bank = dz_min_u8(e->bank, DZ_NBANK - 1u);
    p->pattern = dz_min_u8(e->pattern, DZ_NPAT_PER_BANK - 1u);
    p->play_steps = e->play_steps ? dz_min_u8(e->play_steps, DZ_NSTEP) : 1u;
}

void dz_seq_play_start(const dz_seq_model_t *m, dz_seq_playback_t *p)
{
    uint8_t *dst = (uint8_t *)p;
    uint32_t i;
    for (i = 0; i < sizeof *p; ++i) dst[i] = 0;
    p->song_mode = !!m->song_mode;
    p->loop_mode = !!m->loop_mode;
    p->running = 1u;
    if (p->song_mode) {
        if (dz_seq_song_is_end(&m->song[0])) {
            /* Empty song: seq_start may proceed, but its first step must be silent. */
            p->running = 0u;
            p->play_steps = 1u;
        } else
            dz_seq_play_song_entry(m, p);
    }
    else {
        p->bank = m->bank;
        p->pattern = m->pattern;
        p->play_steps = m->patterns[dz_seq_pat_index(m->bank, m->pattern)].length;
        if (p->play_steps == 0 || p->play_steps > DZ_NSTEP)
            p->play_steps = DZ_NSTEP;
    }
}

int dz_seq_play_next(const dz_seq_model_t *m, dz_seq_playback_t *p)
{
    if (!p->running)
        return 0;
    if (!p->song_mode) {
        if (p->loop_mode)
            return 1;
        p->running = 0;
        return 0;
    }
    /* The first END marker terminates the song, even when old song rows
     * remain after it. On LOOP, start again at row 0 instead. */
    if ((uint32_t)p->song_step + 1u >= m->song_count ||
        dz_seq_song_is_end(&m->song[p->song_step + 1u])) {
        if (!p->loop_mode || dz_seq_song_is_end(&m->song[0])) {
            p->running = 0u;
            return 0;
        }
        p->song_step = 0;
    } else
        ++p->song_step;
    dz_seq_play_song_entry(m, p);
    return 1;
}
