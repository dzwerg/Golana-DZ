/* SPDX-License-Identifier: GPL-3.0-only
 * Golana 64 extension: 48 compact steps for each of 32 programs, and the
 * four Songmode settings. Compatible with the original 192-byte user presets.
 *
 * Object GX uses two UNUSED Felucca project sectors (A/B; prefer project D),
 * under a DIFFERENT storage type. Valid legacy project headers are never
 * overwritten. When all four project pairs are occupied, GX writes are blocked.
 * No flash write takes place during playback. st_save uses an A/B commit.
 */
#define GX_MAGIC 0x34365847u /* "GX64" */
#define GX_EXTRA (NSTEP - 16u)
typedef struct {
    uint32_t magic;
    uint8_t enabled, first, last, repeat;     /* first/last are zero-based */
    uint32_t sig[GL_NPROG];                 /* FNV-1a of corresponding up_rec_t */
    uint8_t pattern[GL_NPROG][GX_EXTRA][2]; /* [note, flags] steps 17..64 */
} gx_store_t;
_Static_assert(NSTEP == 64u && GL_NPROG == 32u, "Golana 64 extension dimensions");
_Static_assert(sizeof(gx_store_t) <= 3840u, "Golana 64 extension must fit one NOR sector");
static gx_store_t gx __attribute__((section(".pool")));
static uint8_t gx_blocked, gx_dirty;
static uint32_t gx_edited_ms;

static uint32_t gx_signature(const up_rec_t *r)
{
    const uint8_t *p = (const uint8_t *)r;
    uint32_t i, h = 2166136261u;
    for (i = 0; i < sizeof *r; i++) h = (h ^ p[i]) * 16777619u;
    return h ? h : 1u;
}
static void gx_reset(void)
{
    memset(&gx, 0, sizeof gx);
    gx.magic = GX_MAGIC;
    gx.last = GL_NPROG - 1u;
}
static int gx_pair_blank(uint8_t slot)
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
static void gx_boot(void)
{
#if FELUCCA_FLASH
    uint32_t i;
#endif
    gx_reset();
    gx_blocked = 0;
#if FELUCCA_FLASH
    if (flash_ok) {
        st_hdr_t h;
        gx_blocked = 1u;
        /* Find an existing extension first. Reject any sector pair that also
         * contains a valid old project record, even in the other A/B copy. */
        for (i = 4u; i > 0u; i--) {
            gx_project_slot = (uint8_t)(i - 1u);
            if (st_head(OBJ_PROJECT0 + i - 1u, 0u, &h) == 0 ||
                st_head(OBJ_PROJECT0 + i - 1u, 1u, &h) == 0) continue;
            if (st_head(OBJ_GOLANA_EXT, 0u, &h) == 0 ||
                st_head(OBJ_GOLANA_EXT, 1u, &h) == 0) {
                gx_blocked = 0u;
                break;
            }
        }
        /* New install: first free pair, preferring Felucca Project D. */
        if (gx_blocked)
            for (i = 4u; i > 0u; i--) {
                gx_project_slot = (uint8_t)(i - 1u);
                if (st_head(OBJ_PROJECT0 + i - 1u, 0u, &h) != 0 &&
                    st_head(OBJ_PROJECT0 + i - 1u, 1u, &h) != 0 &&
                    gx_pair_blank((uint8_t)(i - 1u))) {
                    gx_blocked = 0u;
                    break;
                }
            }
        if (!gx_blocked &&
            (st_load(OBJ_GOLANA_EXT, &gx, sizeof gx) != (int)sizeof gx || gx.magic != GX_MAGIC))
            gx_reset();
    }
#endif
    gx.enabled = gx.enabled != 0u;
    gx.repeat = gx.repeat != 0u;
    if (gx.first >= GL_NPROG) gx.first = 0;
    if (gx.last >= GL_NPROG) gx.last = GL_NPROG - 1u;
    if (gx.last < gx.first) gx.last = gx.first;
}
static void gx_mark_dirty(void)
{
    gx_dirty = 1u;
    gx_edited_ms = fm1_ms;
}
static void gx_poll(void)
{
#if FELUCCA_FLASH
    if (!gx_dirty || gx_blocked || !flash_ok || transport_busy() ||
        fm1_ms - gx_edited_ms < 600u)
        return;
    if (!st_save(OBJ_GOLANA_EXT, &gx, sizeof gx))
        gx_dirty = 0;
#endif
}
/* load_pat16() has already initialised all 64 steps; overlay steps 17..64. */
static void gx_apply(uint32_t k, track_t *t)
{
    uint32_t i;
    if (k >= GL_NPROG || !up_used(k) || gx.sig[k] != gx_signature(up_rec(k)))
        return;
    for (i = 0; i < GX_EXTRA; i++) {
        uint32_t note = gx.pattern[k][i][0] & 127u, fl = gx.pattern[k][i][1];
        step_t *s = &t->step[16u + i];
        if (fl & 4u) { s->time = ST_TIE; s->n = 0; }
        else if (note) {
            s->time = ST_NOTE;
            s->n = 1;
            s->note[0] = (uint8_t)note;
            s->vel = 96;
            s->flags = (uint8_t)(fl & (SF_ACCENT | SF_SLIDE | SF_RATCH));
        } else { s->time = ST_REST; s->n = 0; }
    }
}
static int gx_store(uint32_t k, const up_rec_t *r, const step_t *steps)
{
    uint32_t i;
    if (k >= GL_NPROG || gx_blocked) return 2;
    for (i = 0; i < GX_EXTRA; i++) {
        const step_t *s = &steps[16u + i];
        uint8_t n = s->time == ST_NOTE && s->n ? s->note[0] : 0u;
        uint8_t f = s->time == ST_TIE ? 4u : (uint8_t)(s->flags & ~SF_NUDGE);
        up_pat_norm(&n, &f);
        gx.pattern[k][i][0] = n;
        gx.pattern[k][i][1] = f;
    }
    gx.sig[k] = gx_signature(r);
#if FELUCCA_FLASH
    if (flash_ok && st_save(OBJ_GOLANA_EXT, &gx, sizeof gx))
        return 2;
#endif
    gx_dirty = 0;
    return 0;
}
