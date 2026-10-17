/* SPDX-License-Identifier: GPL-3.0-only
 * Golana DZ Stage 6: crash-consistent persistent bank patterns and song.
 * Two disjoint, physically blank Felucca project pairs, A/B per pair.
 * A transaction is complete ONLY when both pages have the same generation.
 * This is an included .c (felucca.c / felucca_web.c), after storage.c and gx.
 */
#define DZ_STORE_MAGIC 0x365A4450u /* PDZ6 */
#define DZ_STORE_VERSION 1u
#define DZ_STORE_HALF_PATTERNS 32u
#define DZ_STORE_PACKED_BYTES (DZ_STORE_HALF_PATTERNS * DZ_NSTEP * 3u / 2u)

typedef struct {
    uint32_t magic, generation;
    uint8_t version, half;
    uint8_t bank, pattern, song_mode, loop_mode, song_step, song_count;
    uint8_t length[DZ_STORE_HALF_PATTERNS];
    uint8_t step[DZ_STORE_PACKED_BYTES];
    uint8_t song[DZ_NSONG * 3u]; /* duplicated in both pages */
} dz_store_page_t;
_Static_assert(sizeof(dz_store_page_t) == 3312u, "DZ page layout changed");
_Static_assert(sizeof(dz_store_page_t) <= ST_PAYLOAD_MAX, "DZ page exceeds 4 KiB sector payload");
_Static_assert(DZ_NPAT == 64u && DZ_NSTEP == 64u && DZ_NSONG == 64u, "DZ model layout changed");

static dz_store_page_t dz_store_io __attribute__((section(".pool")));
static uint8_t dz_store_enabled, dz_store_dirty;
static uint8_t dz_store_have_commit, dz_store_copy[2];
static uint32_t dz_store_generation, dz_store_edited_ms, dz_store_last_try_ms;
static uint8_t dz_store_tried;
/* Set on a real NOR write/verify failure; cleared only by a complete two-page commit. */
static uint8_t dz_store_error;

/* 12 bits: note 7, flags 5 (TIE bit2, RATCH bits3..4). */
static void dz_store_pack12(uint8_t *dst, const dz_pattern_step_t *a, const dz_pattern_step_t *b)
{
    uint16_t x = (uint16_t)(a->note & 127u) | (uint16_t)(a->flags & 31u) << 7;
    uint16_t y = (uint16_t)(b->note & 127u) | (uint16_t)(b->flags & 31u) << 7;
    dst[0] = (uint8_t)x;
    dst[1] = (uint8_t)((x >> 8) | (y << 4));
    dst[2] = (uint8_t)(y >> 4);
}
static void dz_store_unpack12(const uint8_t *src, dz_pattern_step_t *a, dz_pattern_step_t *b)
{
    uint16_t x = (uint16_t)src[0] | (uint16_t)(src[1] & 15u) << 8;
    uint16_t y = (uint16_t)(src[1] >> 4) | (uint16_t)src[2] << 4;
    a->note = (uint8_t)(x & 127u); a->flags = (uint8_t)(x >> 7);
    b->note = (uint8_t)(y & 127u); b->flags = (uint8_t)(y >> 7);
}
static void dz_store_encode(dz_store_page_t *p, const dz_seq_model_t *m, uint8_t half, uint32_t generation)
{
    uint32_t j, k, n = 0;
    memset(p, 0, sizeof *p);
    p->magic = DZ_STORE_MAGIC;
    p->generation = generation;
    p->version = DZ_STORE_VERSION;
    p->half = half;
    p->bank = m->bank;
    p->pattern = m->pattern;
    p->song_mode = m->song_mode;
    p->loop_mode = m->loop_mode;
    p->song_step = m->song_step;
    p->song_count = m->song_count;
    for (j = 0; j < DZ_STORE_HALF_PATTERNS; j++) {
        const dz_pattern_t *pat = &m->patterns[(uint32_t)half * DZ_STORE_HALF_PATTERNS + j];
        p->length[j] = pat->length;
        for (k = 0; k < DZ_NSTEP; k += 2u, n += 3u)
            dz_store_pack12(p->step + n, &pat->step[k], &pat->step[k + 1u]);
    }
    for (j = 0; j < DZ_NSONG; j++) {
        p->song[3u * j] = m->song[j].bank;
        p->song[3u * j + 1u] = m->song[j].pattern;
        p->song[3u * j + 2u] = m->song[j].play_steps;
    }
}
/* Both pages repeat the song and UI selection. A matched transaction must
 * agree on these bytes as well as generation, even after unusual resets. */
static uint32_t dz_store_common_crc(const dz_store_page_t *p)
{
    uint32_t c = st_crc32_add(0xFFFFFFFFu, &p->bank, 6u);
    return ~st_crc32_add(c, p->song, sizeof p->song);
}
static int dz_store_valid(const dz_store_page_t *p, uint8_t half)
{
    uint32_t j;
    if (p->magic != DZ_STORE_MAGIC || p->version != DZ_STORE_VERSION || p->half != half ||
        !p->generation || p->bank >= DZ_NBANK || p->pattern >= DZ_NPAT_PER_BANK ||
        p->song_mode > 1u || p->loop_mode > 1u || p->song_step >= DZ_NSONG ||
        !p->song_count || p->song_count > DZ_NSONG)
        return 0;
    for (j = 0; j < DZ_STORE_HALF_PATTERNS; j++)
        if (!p->length[j] || p->length[j] > DZ_NSTEP) return 0;
    for (j = 0; j < DZ_NSONG; j++)
        if (p->song[3u * j] >= DZ_NBANK || p->song[3u * j + 1u] >= DZ_NPAT_PER_BANK ||
            !p->song[3u * j + 2u] || p->song[3u * j + 2u] > DZ_NSTEP) return 0;
    return 1;
}
static void dz_store_decode(dz_seq_model_t *m, const dz_store_page_t *p)
{
    uint32_t j, k, n = 0;
    for (j = 0; j < DZ_STORE_HALF_PATTERNS; j++) {
        dz_pattern_t *pat = &m->patterns[(uint32_t)p->half * DZ_STORE_HALF_PATTERNS + j];
        pat->length = p->length[j];
        for (k = 0; k < DZ_NSTEP; k += 2u, n += 3u)
            dz_store_unpack12(p->step + n, &pat->step[k], &pat->step[k + 1u]);
    }
    m->bank = p->bank; m->pattern = p->pattern;
    m->song_mode = p->song_mode; m->loop_mode = p->loop_mode;
    m->song_step = p->song_step; m->song_count = p->song_count;
    for (j = 0; j < DZ_NSONG; j++) {
        m->song[j].bank = p->song[3u * j];
        m->song[j].pattern = p->song[3u * j + 1u];
        m->song[j].play_steps = p->song[3u * j + 2u];
    }
}

/* Both sectors must be completely erased before a NEW owner is assigned.
 * Header-only checks are insufficient after an interrupted/foreign write. */
static int dz_store_pair_blank(uint8_t slot)
{
    uint32_t c, off, i;
    uint8_t buf[64];
    uint32_t base = 0x97000u + (uint32_t)slot * 2u * ST_SECTOR;
    if (slot >= 4u) return 0;
    for (c = 0; c < 2u; c++)
        for (off = 0; off < ST_SECTOR; off += sizeof buf) {
            if (st_read(base + c * ST_SECTOR + off, buf, sizeof buf)) return 0;
            for (i = 0; i < sizeof buf; i++) if (buf[i] != 0xFFu) return 0;
        }
    return 1;
}

/* Scan both A/B copies, using normal commit CRC, not just headers. */
static int dz_store_read_page(uint32_t half, uint32_t copy)
{
    st_hdr_t h;
    uint32_t obj = OBJ_DZ_PAGE0 + half;
    if (st_head(obj, copy, &h) || h.len != sizeof dz_store_io || st_body(obj, copy, &h))
        return 0;
    memcpy(&dz_store_io, st_buf, sizeof dz_store_io);
    return dz_store_valid(&dz_store_io, (uint8_t)half);
}
static void dz_store_boot(dz_seq_model_t *m)
{
    uint8_t found[2] = {0, 0}, used[4] = {0, 0, 0, 0};
    uint32_t gens[2][2] = {{0, 0}, {0, 0}}, common[2][2] = {{0, 0}, {0, 0}}, half, slot, c;
    int have = 0;
    uint32_t best = 0;
    uint8_t chosen[2] = {0, 0};
    dz_store_enabled = dz_store_dirty = dz_store_have_commit = 0u;
    dz_store_generation = 0u;
    dz_store_edited_ms = dz_store_last_try_ms = 0u;
    dz_store_tried = 0u;
    dz_store_error = 0u;
    dz_project_slot[0] = dz_project_slot[1] = 0xFFu;
    if (!flash_ok) return;
    if (!gx_blocked && gx_project_slot < 4u)
        used[gx_project_slot] = 1u; /* reserve GX even if not yet written */
    /* Existing DZ-owned pairs first. Do not select any pair twice. */
    for (half = 0; half < 2u; half++) {
        for (slot = 0; slot < 4u; slot++) {
            st_hdr_t h;
            int owns;
            dz_project_slot[half] = (uint8_t)slot;
            owns = st_head(OBJ_DZ_PAGE0 + half, 0u, &h) == 0 ||
                   st_head(OBJ_DZ_PAGE0 + half, 1u, &h) == 0;
            if (!owns) continue;
            /* Refuse a pair containing a valid foreign project commit. */
            for (c = 0; c < 2u; c++) {
                uint32_t off = 0x97000u + slot * 2u * ST_SECTOR + c * ST_SECTOR;
                if (st_read(off, &h, sizeof h)) return;
                if (h.magic == ST_MAGIC && h.hcrc == st_crc32(&h, sizeof h - 4u) &&
                    h.type != OBJ_DZ_PAGE0 + half) return;
            }
            if (found[half] || used[slot]) {
                dz_project_slot[0] = dz_project_slot[1] = 0xFFu;
                return; /* ambiguous/colliding ownership: RAM only */
            }
            found[half] = 1u;
            used[slot] = 1u;
            chosen[half] = (uint8_t)slot;
        }
        dz_project_slot[half] = 0xFFu;
    }
    /* Claim physically erased project pairs, preferring D -> A. */
    for (half = 0; half < 2u; half++) {
        if (found[half]) continue;
        for (slot = 4u; slot > 0u; slot--) {
            uint32_t s = slot - 1u;
            if (!used[s] && dz_store_pair_blank((uint8_t)s)) {
                chosen[half] = (uint8_t)s;
                used[s] = 1u;
                found[half] = 1u;
                break;
            }
        }
        if (!found[half]) return; /* no free pair: NEVER write */
    }
    dz_project_slot[0] = chosen[0];
    dz_project_slot[1] = chosen[1];
    dz_store_enabled = 1u;
    for (half = 0; half < 2u; half++)
        for (c = 0; c < 2u; c++)
            if (dz_store_read_page(half, c)) {
                gens[half][c] = dz_store_io.generation;
                common[half][c] = dz_store_common_crc(&dz_store_io);
            }
    for (c = 0; c < 2u; c++) {
        uint32_t d;
        for (d = 0; d < 2u; d++) {
            uint32_t g = gens[0][c];
            if (g && g == gens[1][d] && common[0][c] == common[1][d] &&
                (!have || (int32_t)(g - best) > 0)) {
                best = g; chosen[0] = (uint8_t)c; chosen[1] = (uint8_t)d;
                have = 1;
            }
        }
    }
    if (!have) return; /* no complete transaction: keep default RAM model */
    if (!dz_store_read_page(0u, chosen[0]) || dz_store_io.generation != best) return;
    dz_store_decode(m, &dz_store_io);
    if (!dz_store_read_page(1u, chosen[1]) || dz_store_io.generation != best) {
        dz_seq_model_init(m);
        return;
    }
    dz_store_decode(m, &dz_store_io);
    dz_store_copy[0] = chosen[0]; dz_store_copy[1] = chosen[1];
    dz_store_generation = best;
    dz_store_have_commit = 1u;
}
static void dz_store_mark_dirty(void)
{
    dz_store_dirty = 1u;
    dz_store_edited_ms = fm1_ms;
}
/* Main loop only; never erase flash while REC/SAVE/transport is active. */
static void dz_store_poll(const dz_seq_model_t *m, int allowed)
{
    uint8_t to[2];
    uint32_t half, generation;
    if (!dz_store_dirty || !dz_store_enabled || !flash_ok || !allowed ||
        transport_busy() || fm1_ms - dz_store_edited_ms < 1500u ||
        (dz_store_tried && fm1_ms - dz_store_last_try_ms < 5000u)) return;
    dz_store_tried = 1u;
    dz_store_last_try_ms = fm1_ms;
    generation = dz_store_generation + 1u;
    if (!generation) generation = 1u;
    for (half = 0; half < 2u; half++)
        to[half] = dz_store_have_commit ? (uint8_t)(1u - dz_store_copy[half]) : 0u;
    for (half = 0; half < 2u; half++) {
        dz_store_encode(&dz_store_io, m, (uint8_t)half, generation);
        if (st_save_to(OBJ_DZ_PAGE0 + half, &dz_store_io, sizeof dz_store_io, to[half])) {
            dz_store_error = 1u;
            return;   /* old *paired* generation still intact; retry later */
        }
    }
    dz_store_have_commit = 1u;
    dz_store_copy[0] = to[0]; dz_store_copy[1] = to[1];
    dz_store_generation = generation;
    dz_store_dirty = 0u;
    dz_store_error = 0u;
}
