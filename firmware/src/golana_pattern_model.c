/* SPDX-License-Identifier: GPL-3.0-only */
#include "golana_pattern_model.h"
#include <string.h>

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
    memset(m, 0, sizeof *m);
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

static dz_song_entry_t *dz_song_edit(dz_seq_model_t *m)
{
    if (m->song_step >= m->song_count)
        m->song_count = (uint8_t)(m->song_step + 1u);
    return &m->song[m->song_step];
}

void dz_seq_set_song_bank(dz_seq_model_t *m, uint8_t bank)
{
    dz_song_edit(m)->bank = dz_min_u8(bank, DZ_NBANK - 1u);
}

void dz_seq_set_song_pattern(dz_seq_model_t *m, uint8_t pattern)
{
    dz_song_edit(m)->pattern = dz_min_u8(pattern, DZ_NPAT_PER_BANK - 1u);
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
    memset(p, 0, sizeof *p);
    p->song_mode = !!m->song_mode;
    p->loop_mode = !!m->loop_mode;
    p->running = 1u;
    if (p->song_mode)
        dz_seq_play_song_entry(m, p);
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
    if ((uint32_t)p->song_step + 1u < m->song_count)
        ++p->song_step;
    else if (p->loop_mode)
        p->song_step = 0;
    else {
        p->running = 0;
        return 0;
    }
    dz_seq_play_song_entry(m, p);
    return 1;
}
