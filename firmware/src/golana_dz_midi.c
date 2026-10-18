/* SPDX-License-Identifier: GPL-3.0-only
 * Editor command 96. All bytes 7-bit. Request op, token, args; reply
 * op, token, rc, data. rc: 0 OK, 1 malformed, 2 busy, 3 RAM only, 4 flash.
 * Included after golana_ui.c; runs only in the main loop.
 */
static uint32_t dz_midi_request(const uint8_t *a, uint32_t n, uint8_t *out)
{
    uint32_t op = n ? a[0] : 127u, i, k, len = 3u;
    out[0] = (uint8_t)op; out[1] = n > 1u ? a[1] : 0u; out[2] = 1u;
    if (n < 2u) return len;
    for (i = 0; i < n; i++) if (a[i] > 127u) return len;
    if (!op && n == 2u) {
        out[2] = 0; out[3] = 2; /* protocol v2: SONG END row support */
        out[4] = dz_store_enabled; out[5] = dz_store_dirty;
        out[6] = dz_store_error; out[7] = dz_store_have_commit;
        return 8u;
    }
    if (!gu.ready || transport_busy() || gu.dz_edit_active || gu.save || dz_play_has_backup) {
        out[2] = 2; return len;
    }
    if (op == 1u) { /* explicitly erase ONLY 0x97000..0x9efff */
        static const uint8_t key[] = {67, 76, 69, 65, 82}; /* CLEAR */
        if (n != 7u || memcmp(a + 2, key, 5u)) return len;
        if (!flash_ok) { out[2] = 4; return len; }
        dz_store_enabled = 0; gx_blocked = 1;
        for (i = 0; i < 8u; i++) {
            if (st_erase(0x97000u + i * ST_SECTOR)) { out[2] = 4; return len; }
            fm1_wdt_feed();
        }
        for (i = 0; i < 4u; i++)
            if (!dz_store_pair_blank((uint8_t)i)) { out[2] = 4; return len; }
        memset(proj_slot, 0, sizeof proj_slot);
        gx_boot(); gx_dirty = 0;
        dz_store_boot(&dz_model); /* retain current RAM patterns */
        if (!dz_store_enabled) { out[2] = 3; return len; }
        dz_store_mark_dirty();
        out[2] = 0; gu_say("PROJECTS CLEARED", 0); return len;
    }
    if (op == 2u || op == 3u) { /* put/get pattern: bank, pattern, length, 64*(note,flags) */
        if (n < 4u || a[2] >= DZ_NBANK || a[3] >= DZ_NPAT_PER_BANK) return len;
        k = (uint32_t)a[2] * 16u + a[3];
        if (op == 2u) {
            if (n != 133u || !a[4] || a[4] > 64u) return len;
            for (i = 0; i < 64u; i++) if (a[6u + 2u*i] > 31u) return len;
            dz_model.patterns[k].length = a[4];
            for (i = 0; i < 64u; i++) {
                dz_model.patterns[k].step[i].note = a[5u + 2u*i];
                dz_model.patterns[k].step[i].flags = a[6u + 2u*i];
            }
            dz_store_mark_dirty();
        } else {
            if (n != 4u) return len;
            out[len++] = dz_model.patterns[k].length;
            for (i = 0; i < 64u; i++) {
                out[len++] = dz_model.patterns[k].step[i].note;
                out[len++] = dz_model.patterns[k].step[i].flags;
            }
        }
    } else if (op == 4u || op == 5u) { /* put/get complete song: count, loop, triples */
        if (op == 4u) {
            if (n < 4u || !a[2] || a[2] > 64u || a[3] > 1u || n != 4u + 3u*a[2]) return len;
            for (i = 0; i < a[2]; i++) {
                uint8_t bank = a[4u+3u*i], pat = a[5u+3u*i];
                if (!((bank < DZ_NBANK && pat < DZ_NPAT_PER_BANK) ||
                      (bank == DZ_SONG_END_BANK && pat == DZ_SONG_END_PATTERN)) ||
                    !a[6u+3u*i] || a[6u+3u*i] > DZ_NSTEP) return len;
            }
            dz_model.song_count = a[2]; dz_model.loop_mode = a[3];
            dz_model.song_mode = 1; dz_model.song_step = 0;
            for (i = 0; i < a[2]; i++) {
                dz_model.song[i].bank = a[4u+3u*i]; dz_model.song[i].pattern = a[5u+3u*i];
                dz_model.song[i].play_steps = a[6u+3u*i];
            }
            /* Do not install END as the selected pattern outside SONG mode. */
            if (a[4] < DZ_NBANK && a[5] < DZ_NPAT_PER_BANK) {
                dz_model.bank = a[4]; dz_model.pattern = a[5];
            }
            dz_store_mark_dirty();
        } else {
            if (n != 2u) return len;
            out[len++] = dz_model.song_count; out[len++] = dz_model.loop_mode;
            for (i = 0; i < dz_model.song_count; i++) {
                out[len++] = dz_model.song[i].bank; out[len++] = dz_model.song[i].pattern;
                out[len++] = dz_model.song[i].play_steps;
            }
        }
    } else if (op == 6u && n == 2u) { /* request normal verified autosave; poll status until clean */
        if (!dz_store_enabled) { out[2] = 3; return len; }
        dz_store_poll(&dz_model, 1);
        if (dz_store_error) { out[2] = 4; return len; }
    } else return len;
    out[2] = 0;
    return len;
}
