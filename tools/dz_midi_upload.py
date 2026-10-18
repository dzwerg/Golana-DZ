#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""DZ pattern uploader. Preview is default; device changes require --send.
Requires: py -m pip install mido python-rtmidi
"""
import argparse
import math
from pathlib import Path
import time

HEADER = [0x7D, 0x46, 0x4C, 96]


class Device:
    def __init__(self, mido, input_name, output_name):
        self.mido = mido
        self.rx = mido.open_input(input_name)
        try:
            self.tx = mido.open_output(output_name)
        except Exception:
            self.rx.close()
            raise
        self.token = 0

    def close(self):
        self.tx.close()
        self.rx.close()

    def call(self, op, args=(), timeout=15):
        self.token = (self.token + 1) % 128
        for _ in self.rx.iter_pending():
            pass
        self.tx.send(self.mido.Message('sysex', data=HEADER + [op, self.token] + list(args)))
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for msg in self.rx.iter_pending():
                data = list(msg.data) if msg.type == 'sysex' else []
                if data[:6] != HEADER + [op, self.token] or len(data) < 7:
                    continue
                if data[6]:
                    meanings = {1: 'invalid request', 2: 'STOP transport and leave REC/SAVE',
                                3: 'RAM ONLY: run --clear-old-projects', 4: 'flash failure'}
                    raise RuntimeError(meanings.get(data[6], f'device error {data[6]}'))
                return data[7:]
            time.sleep(.005)
        # Destructive requests are deliberately not retried after an ambiguous timeout.
        raise RuntimeError(f'No DZ reply for command {op}. Check firmware/ports; do not blindly retry CLEAR.')

    def status(self):
        data = self.call(0)
        if len(data) != 5 or data[0] not in (1, 2):
            raise RuntimeError('Unsupported DZ protocol')
        return data

    def saved(self):
        deadline = time.monotonic() + 25
        while time.monotonic() < deadline:
            version, enabled, dirty, error, committed = self.status()
            if not enabled or error:
                raise RuntimeError('Flash unavailable or SAVE FAILED; data is not confirmed durable')
            if not dirty and committed:
                return
            time.sleep(.25)
        raise RuntimeError('Autosave timed out: STOP and leave REC/SAVE; do not power off yet')


def convert(mido, filename, track=None, channels=None, slots=None):
    mid = mido.MidiFile(filename)
    if mid.type == 2:
        raise ValueError('MIDI type 2 has independent timelines; convert to type 0/1 first')
    candidates = [i for i, t in enumerate(mid.tracks)
                  if any(m.type == 'note_on' and m.velocity and
                         (channels is None or m.channel in channels) for m in t)]
    if track is None:
        if len(candidates) != 1:
            raise ValueError(f'{filename}: choose --track; note tracks: {candidates}')
        track = candidates[0]
    if track not in candidates:
        raise ValueError(f'{filename}: selected track {track} has no notes')
    active, notes, tick = {}, [], 0
    tempos = set()
    for t in mid.tracks:
        for msg in t:
            if msg.type == 'set_tempo':
                tempos.add(msg.tempo)
    max_error = 0
    for msg in mid.tracks[track]:
        tick += msg.time
        if msg.type not in ('note_on', 'note_off') or (channels is not None and msg.channel not in channels):
            continue
        key = msg.channel, msg.note
        if msg.type == 'note_on' and msg.velocity:
            if key in active:
                raise ValueError('Repeated note before Note Off: ambiguous MIDI')
            if msg.note == 0:
                raise ValueError('MIDI note 0 cannot be represented (DZ uses 0 for REST)')
            active[key] = tick
        else:
            if key not in active:
                continue
            start = active.pop(key)
            a = int(math.floor(start * 4 / mid.ticks_per_beat + .5))
            b = int(math.floor(tick * 4 / mid.ticks_per_beat + .5))
            max_error = max(max_error, abs(a-start*4/mid.ticks_per_beat), abs(b-tick*4/mid.ticks_per_beat))
            if b <= a:
                raise ValueError('A note disappears on the 1/16 grid; use another resolution/source')
            notes.append((a, b, msg.note))
    if active or not notes:
        raise ValueError('Missing Note Off or empty MIDI')
    notes.sort()
    for previous, current in zip(notes, notes[1:]):
        if current[0] < previous[1]:
            raise ValueError('Polyphonic/overlapping notes: choose a monophonic melody track/channel')
    end = max(n[1] for n in notes)
    needed = (end + 63) // 64
    count = slots or needed
    if needed > count or count > 64:
        raise ValueError(f'{filename}: needs {needed} patterns, reserved {count}')
    patterns = []
    for k in range(count):
        length = min(64, max(1, end - k*64)) if k == count-1 else 64
        patterns.append([length] + [0, 0]*64)
    for a, b, pitch in notes:
        for step in range(a, b):
            k, j = divmod(step, 64)
            # At a pattern boundary restart the pitch (no preceding held note guaranteed).
            patterns[k][1+2*j:3+2*j] = [pitch, 0] if step == a or j == 0 else [0, 4]
    bpm = ', '.join(f'{60_000_000/t:.2f}' for t in sorted(tempos)) or '120 (MIDI default)'
    print(f'{Path(filename).name}: track {track}, {len(notes)} notes, {end} steps, {needed}/{count} patterns; BPM {bpm}; quantization max {max_error:.3f} steps')
    if len(tempos) > 1:
        raise ValueError('Tempo changes cannot be represented by the current DZ song model')
    return patterns


def main():
    import mido
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ports', action='store_true')
    parser.add_argument('--input', help='Exact Windows MIDI input port')
    parser.add_argument('--output', help='Exact Windows MIDI output port')
    parser.add_argument('--send', action='store_true', help='Actually modify the connected FM-1')
    parser.add_argument('--clear-old-projects', action='store_true', help='Erase 0x97000..0x9EFFF, including old GX extension; retain DZ RAM model')
    parser.add_argument('--brahms', default='Brahms_Wiegenlied.mid')
    parser.add_argument('--musette', default='Musette_BWV_Anh126_Bonus(1).mid')
    parser.add_argument('--track', type=int, help='Zero-based melody track in both files')
    parser.add_argument('--brahms-track', type=int)
    parser.add_argument('--musette-track', type=int)
    parser.add_argument('--channel', type=int, choices=range(1,17))
    parser.add_argument('--activate', choices=['brahms','musette'], default='brahms')
    parser.add_argument('--select-only', action='store_true', help='Select song from already uploaded patterns, without reading MIDI files')
    parser.add_argument('--verify-only', action='store_true', help='Read back expected patterns/song after a powercycle; no writes')
    parser.add_argument('--status', action='store_true')
    args = parser.parse_args()
    if args.ports:
        print('INPUT:', *mido.get_input_names(), sep='\n  ')
        print('OUTPUT:', *mido.get_output_names(), sep='\n  ')
        return
    if args.clear_old_projects and (args.verify_only or args.select_only):
        parser.error('CLEAR cannot be combined with verification/selection')
    patterns = []
    if not args.status and not args.select_only:
        channels = None if args.channel is None else {args.channel-1}
        patterns = convert(mido,args.brahms,args.brahms_track if args.brahms_track is not None else args.track,channels,3) + convert(mido,args.musette,args.musette_track if args.musette_track is not None else args.track,channels,5)
        print('Target: Bank A01-A03 Brahms; A04-A08 Musette. Velocity becomes 96; long notes restart at pattern boundaries.')
    if not (args.send or args.verify_only or args.status):
        print('PREVIEW ONLY. Use --send with exact --input and --output to transfer.')
        return
    if not args.input or not args.output:
        parser.error('Specify --input and --output from --ports')
    device = Device(mido,args.input,args.output)
    try:
        print('DZ status [version, enabled, dirty, error, committed]:',device.status())
        if args.status:
            return
        if device.status()[0] < 2:
            raise RuntimeError('SONG END requires MicroSTEP firmware with DZ protocol v2; do not upload with older firmware')
        if args.clear_old_projects:
            device.call(1,list(b'CLEAR'))
            device.saved()
            print('Old project sectors cleared; DZ autosave confirmed.')
        if not device.status()[1]:
            raise RuntimeError('RAM ONLY: first run with --clear-old-projects --send')
        for i, data in enumerate(patterns):
            if not args.verify_only:
                device.call(2,[0,i]+data)
            if device.call(3,[0,i]) != data:
                raise RuntimeError(f'A{i+1:02}: readback differs')
            print(f'A{i+1:02}: readback OK')
        indices = range(3) if args.activate == 'brahms' else range(3,8)
        rows = []
        for i in indices:
            length = patterns[i][0] if patterns else device.call(3,[0,i])[0]
            rows.extend([0,i,length])
        # Explicit END prevents stale trailing rows after choosing a shorter song.
        # The 64th row must instead use song_count as its boundary.
        if len(rows)//3 < 64:
            rows.extend([4, 16, 64])  # bank END / pattern END / unused length
        arranged = [len(rows)//3,0] + rows
        if not args.verify_only:
            device.call(4,arranged)
            device.saved()
        if device.call(5) != arranged:
            raise RuntimeError('Song readback differs')
        print(f'{args.activate}: song readback OK. Set RES 1/16 and song tempo on FM-1.')
        if args.verify_only:
            print('Readback verification passed. If run after powercycle, persistence is confirmed on hardware.')
        else:
            print('Flash commit confirmed. Powercycle, then rerun with --verify-only to verify persistence.')
    finally:
        device.close()


if __name__ == '__main__':
    try:
        main()
    except (ValueError, RuntimeError, OSError, ImportError) as exc:
        raise SystemExit(f'ERROR: {exc}')
