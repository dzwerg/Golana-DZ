#include <stdio.h>
#include <string.h>
#include "../web/emu/felucca_web.c"

static int fails;
static float peak;

static void advance(unsigned ms)
{
    unsigned n = (unsigned)(ms * 44.1 + 0.5);
    while (n) {
        unsigned m = n > 128 ? 128 : n;
        web_render(m);
        for (unsigned i = 0; i < m; i++) {
            float v = out_l[i] < 0 ? -out_l[i] : out_l[i];
            if (v > peak) peak = v;
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

static void check(int ok, const char *name)
{
    printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) fails++;
}

int main(void)
{
    web_nor_erase();
    web_boot();
    advance(1600);
    web_keys(1u << 12);
    advance(100);
    web_keys(0);
    advance(300);

    check(gu.ready, "Simulator ready");

    step_t original = trk[0].step[0];
    int16_t original_len = trk[0].p[P_SLEN];

    dz_model.patterns[0].length = 4;
    dz_model.patterns[0].step[0].note = 60;
    dz_model.patterns[1].length = 4;
    dz_model.patterns[1].step[0].note = 67;

    dz_model.song_mode = 0;
    dz_model.loop_mode = 0;

    peak = 0;
    press(B_PLAY);
    advance(120);
    check(song.playing && peak > 0.01f,
          "Pattern ONCE starts and makes sound");

    advance(2500);
    check(!song.playing, "Pattern ONCE stops");
    check(!dz_play_has_backup &&
          !memcmp(&original, &trk[0].step[0], sizeof original) &&
          trk[0].p[P_SLEN] == original_len,
          "Synth pattern restored");

    dz_model.loop_mode = 1;
    peak = 0;
    press(B_PLAY);
    advance(2500);
    check(song.playing && peak > 0.01f,
          "Pattern LOOP keeps playing");

    press(B_SAVE);
    check(!gu.save, "SAVE blocked during playback");

    press(B_PLAY);
    check(!song.playing, "PLAY stops pattern loop");

    dz_model.song_mode = 1;
    dz_model.loop_mode = 0;
    dz_model.song_count = 2;
    dz_model.song[0].bank = 0;
    dz_model.song[0].pattern = 0;
    dz_model.song[0].play_steps = 2;
    dz_model.song[1].bank = 0;
    dz_model.song[1].pattern = 1;
    dz_model.song[1].play_steps = 3;

    press(B_PLAY);
    advance(2500);
    check(!song.playing, "Song ONCE stops after both entries");

    dz_model.loop_mode = 1;
    press(B_PLAY);
    advance(2500);
    check(song.playing, "Song LOOP keeps playing");
    press(B_PLAY);

    printf("\nDZ Stage 4: %d failures\n", fails);
    return fails ? 1 : 0;
}
