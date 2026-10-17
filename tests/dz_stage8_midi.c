#define _POSIX_C_SOURCE 200809L
/* SPDX-License-Identifier: GPL-3.0-only
 * Stage 8 MIDI: captures actual outgoing USB-MIDI packets from the IRQ path.
 * In the host simulator usb_poll is stubbed, so the producer's MIDI ring
 * is visible without a real USB device.
 */
#include <stdio.h>
#include <string.h>
#include "../web/emu/felucca_web.c"
static int fails, checks;
static void ck(int ok, const char *name)
{
    ++checks;
    printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++fails;
}
static void advance(unsigned ms)
{
    unsigned n = (unsigned)(ms * 44.1 + 0.5);
    while (n) {
        unsigned m = n > 128 ? 128 : n;
        web_render(m);
        n -= m;
    }
}
static void press(unsigned b)
{
    web_buttons(1u << b); advance(40);
    web_buttons(0); advance(40);
}
static void midi_clear(void) { mo_r = mo_w; }
static int count_note(unsigned pitch, int on)
{
    uint32_t k;
    int hits = 0;
    for (k = mo_r; k < mo_w && k - mo_r < MQ; ++k) {
        uint32_t pkt = midi_out_q[k % MQ];
        unsigned stat = (pkt >> 8) & 0xF0u;
        unsigned n = (pkt >> 16) & 127u;
        unsigned v = (pkt >> 24) & 127u;
        if (n == pitch && (on ? (stat == 0x90u && v > 0) :
                                 (stat == 0x80u || (stat == 0x90u && !v))))
            ++hits;
    }
    return hits;
}
static int note_order(unsigned a, unsigned b)
{
    uint32_t k;
    int saw_off = 0;
    for (k = mo_r; k < mo_w && k - mo_r < MQ; ++k) {
        uint32_t pkt = midi_out_q[k % MQ];
        unsigned stat = (pkt >> 8) & 0xF0u;
        unsigned n = (pkt >> 16) & 127u;
        unsigned v = (pkt >> 24) & 127u;
        if (n == a && (stat == 0x80u || (stat == 0x90u && !v))) saw_off = 1;
        if (n == b && stat == 0x90u && v) return saw_off;
    }
    return 0;
}
int main(void)
{
    int i;
    web_nor_erase();
    web_boot();
    advance(1700);
    /* Native harness has no USB host, so explicitly emulate a configured
       MIDI interface to enable the real firmware's midi_out_event queue. */
    usb.config = 1;
    midi_clear();
    ck(gu.ready && !song.playing, "Golana MIDI host simulator ready");

    /* Force two contrasting notes at a one-step song boundary. */
    dz_model.patterns[0].length = 1;
    dz_model.patterns[0].step[0].note = 60;
    dz_model.patterns[1].length = 1;
    dz_model.patterns[1].step[0].note = 67;
    dz_model.song_mode = 1;
    dz_model.loop_mode = 0;
    dz_model.song_count = 2;
    dz_model.song[0].bank = dz_model.song[1].bank = 0;
    dz_model.song[0].pattern = 0;
    dz_model.song[1].pattern = 1;
    dz_model.song[0].play_steps = dz_model.song[1].play_steps = 1;
    press(B_PLAY);
    for (i = 0; i < 250 && song.playing; ++i) advance(20);
    ck(!song.playing, "Two-row MIDI song ONCE auto-stops");
    ck(count_note(60, 1) >= 1 && count_note(67, 1) >= 1,
       "USB MIDI Note On emitted for both song rows");
    ck(count_note(60, 0) >= 1 && count_note(67, 0) >= 1,
       "USB MIDI Note Off emitted for both song rows");
    ck(note_order(60, 67), "Previous row Note Off precedes next row Note On");
    ck(count_note(60, 1) == count_note(60, 0) &&
       count_note(67, 1) == count_note(67, 0),
       "Outgoing MIDI Note Ons and Offs are balanced");
    ck(gm_seq[60 >> 5] == 0u && gm_seq[67 >> 5] == 0u &&
       gu_t()->seq_n == 0,
       "Stop leaves no held sequencer MIDI bits or active notes");
    ck(mo_w - mo_r < MQ, "MIDI queue has not overflowed");

    /* A seq note shared with the ARP must not be ended by just one source. */
    midi_clear();
    gm_all_off();
    midi_clear();
    gm_note(0u, 72u, 100u);
    gm_note(1u, 72u, 100u);
    gm_note(0u, 72u, 0u);
    ck(count_note(72, 1) == 1 && count_note(72, 0) == 0,
       "Shared ARP note remains held after SEQ releases it");
    gm_note(1u, 72u, 0u);
    ck(count_note(72, 0) == 1,
       "Shared ARP note sends exactly one final Note Off");
    ck(gm_seq[72 >> 5] == 0u && gm_arp[72 >> 5] == 0u,
       "Shared MIDI source masks both clear");
    printf("\nDZ Stage 8 MIDI: %d/%d passed; %d failures\n", checks - fails, checks, fails);
    return fails ? 1 : 0;
}
