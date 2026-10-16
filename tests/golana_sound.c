/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Gadlele, based on Felucca's tests/hostsim.c */
/* Host renders of Golana's programs (the firmware's engine, voices, arp and FX, as tests/hostsim.c builds them):
 *   cc -O2 -w -Ibuild/gen -Ifirmware/src -o build/host/golana_sound tests/golana_sound.c -lm
 *   build/host/golana_sound OUTDIR [PROG]          every factory program (or one): OUTDIR/NN_NAME.wav and a line each
 * Each render: a phrase of single notes, then a held chord (POLY) or a held note (MONO), then silence; the arp
 * programs hold a chord for their arp. Printed: peak and RMS (dBFS), clipping, DC, the tail's silence, and the
 * render's cost against ANALOG's SAW LEAD (the same notes). Exit 1 when a program clips, is silent or never ends. */
/* golana_sound exercises factory synthesis, not program chaining.
 * The hostsim unity build includes seq.c but no golana_ui.c, whose
 * gu_song_begin/next implementations live in the full device firmware.
 * No-op stubs model disabled Songmode for this sound-only test. */
#if defined(GOLANA) && GOLANA
static void gu_song_begin(void) {}
static int gu_song_next(void) { return 1; }
#endif
#define main hostsim_main
#include "hostsim.c"
#undef main

static int32_t pk_l, pk_r, nclip;
static double sum2, sumx;
static uint64_t nfr;
static int16_t *rec;
static uint32_t rec_n, rec_cap;

static void midi_note(uint32_t on, uint32_t note, uint32_t vel)
{
    uint32_t st = on ? 0x90u : 0x80u;
    midi_enqueue((st >> 4) | st << 8 | (note & 127u) << 16 | (vel & 127u) << 24, 1);
}

static void render_ms(uint32_t ms, int keep)
{
    uint32_t n = ms * 441u / 10u, i;
    int32_t out[2 * CTL];
    while (n) {
        uint32_t b = CTL;
        mix_block(out, b);
        for (i = 0; i < b; i++) {
            int32_t l = out[2 * i] >> 1, r = out[2 * i + 1] >> 1;   /* (Q15 at the master's full level) */
            int32_t al = l < 0 ? -l : l, ar = r < 0 ? -r : r;
            if (al > pk_l) pk_l = al;
            if (ar > pk_r) pk_r = ar;
            if (al >= 32767 || ar >= 32767) nclip++;
            sum2 += (double)l * l + (double)r * r;
            sumx += l + r;
            nfr++;
            if (keep && rec_n + 2u <= rec_cap) {
                rec[rec_n++] = (int16_t)(l > 32767 ? 32767 : l < -32768 ? -32768 : l);
                rec[rec_n++] = (int16_t)(r > 32767 ? 32767 : r < -32768 ? -32768 : r);
            }
        }
        n = n > b ? n - b : 0;
    }
}

static void load_prog(uint32_t pi, char *name)
{
    int16_t v[P_COUNT];
    uint32_t k;
    host_tracks_init();
    for (k = 0; k < NTRK; k++) {
        gl_factory(0xFF, v, 0);
        gl_apply(&trk[k], v);
        trk[k].engine = trk[k].eng_req;
        eng_mem_claim(&trk[k]);
    }
    gl_factory(pi, v, name);
    {   /* PSET=id:v,id:v.. (after the program) */
        const char *e = getenv("PSET");
        while (e && *e) {
            int id = atoi(e), val;
            const char *c = strchr(e, ':');
            if (!c) break;
            val = atoi(c + 1);
            if (id >= 0 && id < P_COUNT) v[id] = (int16_t)val;
            e = strchr(c, ',');
            if (e) e++;
        }
    }
    gl_apply(&trk[0], v);
    trk[0].engine = trk[0].eng_req;
    song.sel = 0;
    song.g[G_ROUTE] = 1;                          /* SEL: every channel plays part 1 */
    song.g[G_BPM] = 120;
}

/* the test phrase: notes, a chord (or for an arp, a held chord), the tail */
static void phrase(int arp)
{
    static const uint8_t MEL[8] = {48, 55, 60, 63, 67, 70, 72, 60};
    uint32_t i;
    if (getenv("SINGLE")) {                       /* SINGLE=note:ms: one held note */
        int nt = atoi(getenv("SINGLE")), ms = 2000;
        const char *c = strchr(getenv("SINGLE"), ':');
        if (c) ms = atoi(c + 1);
        midi_note(1, (uint32_t)nt, 100);
        render_ms((uint32_t)ms, 1);
        midi_note(0, (uint32_t)nt, 0);
        return;
    }
    if (!arp) {
        for (i = 0; i < 8u; i++) {
            midi_note(1, MEL[i], 100);
            render_ms(220, 1);
            midi_note(0, MEL[i], 0);
            render_ms(30, 1);
        }
    }
    midi_note(1, 48, 100);
    midi_note(1, 55, 100);
    midi_note(1, 63, 100);
    midi_note(1, 67, 100);
    render_ms(arp ? 4000 : 2500, 1);
    midi_note(0, 48, 0);
    midi_note(0, 55, 0);
    midi_note(0, 63, 0);
    midi_note(0, 67, 0);
}

static double db(double x) { return x > 0 ? 20.0 * log10(x / 32768.0) : -999.0; }

int main(int argc, char **argv)
{
    uint32_t pi, p0 = 0, p1 = NELEM(GL_FACTORY), bad = 0;
    const char *dir = argc > 1 ? argv[1] : ".";
    if (argc > 2) {
        p0 = (uint32_t)atoi(argv[2]);
        p1 = p0 + 1u;
    }
    rec_cap = 44100u * 2u * 30u;
    rec = malloc(rec_cap * sizeof *rec);
    for (pi = p0; pi < p1; pi++) {
        char name[16], path[256];
        uint64_t t0, t1;
        int32_t tail;
        uint32_t i, arp;
        FILE *f;
        load_prog(pi, name);
        arp = trk[0].p[GL_ARPON] != 0;
        pk_l = pk_r = nclip = 0;
        sum2 = sumx = 0;
        nfr = 0;
        rec_n = 0;
        t0 = now_ns();
        phrase((int)arp);
        t1 = now_ns();
        render_ms(15000, 1);                      /* the release, the delay and reverb tails (SLOW ARP's long echoes: ~12 s) */
        tail = 0;
        for (i = rec_n > 44100u ? rec_n - 44100u : 0; i < rec_n; i++)
            tail = abs(rec[i]) > tail ? abs(rec[i]) : tail;
        for (i = 0; name[i]; i++)
            if (name[i] == ' ' || name[i] == '/')
                name[i] = '_';
        snprintf(path, sizeof path, "%s/%02u_%s.wav", dir, pi + 1u, name);
        if ((f = fopen(path, "wb"))) {
            wav_hdr(f, rec_n / 2u);
            fwrite(rec, 2, rec_n, f);
            fclose(f);
        }
        {
            double rms = sqrt(sum2 / (2.0 * (double)nfr)), dc = sumx / (2.0 * (double)nfr);
            int32_t pk = pk_l > pk_r ? pk_l : pk_r;
            int fail = nclip > 0 || pk < 1000 || tail > 40;
            bad |= (uint32_t)fail;
            printf("%02u %-12s peak %6.1f dB  rms %6.1f dB  clip %5d  dc %6.1f  tail %4d  %6.1f us/ms%s\n", pi + 1u, name,
                   db(pk), db(rms), nclip, dc, tail, (double)(t1 - t0) / 1000.0 / (arp ? 4000.0 : 4500.0),
                   fail ? "  << CHECK" : "");
        }
    }
    return bad ? 1 : 0;
}
