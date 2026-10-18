# Golana DZ Edition

**64-step sequencing, Song Mode and USB MIDI OUT for the M-VAVE FM-1.**

[![Release](https://img.shields.io/badge/release-v0.1.0--beta-orange)](https://github.com/dzwerg/Golana-DZ/releases/tag/v0.1.0-beta)
![Device](https://img.shields.io/badge/device-M--VAVE%20FM--1-blue)
[![License: GPL-3.0](https://img.shields.io/badge/license-GPL--3.0-blueviolet)](LICENSE)

**Golana DZ Edition** is an experimental, open-source fork of [Golana by Gadlele](https://github.com/Gadlele/Golana), itself based on [Felucca](https://github.com/hugelton/Felucca). It keeps Golana's expressive synthesizer and hardware-friendly interface while extending the FM-1 into a more capable MIDI song sequencer.

> **This is the DZ fork, not the original Golana 1.0 firmware.** Use the DZ download below. The original Golana website, web installer and firmware links install **Gadlele's original version**, not this fork.

### [Download Golana DZ Edition v0.1.0-beta (.fwsc)](https://github.com/dzwerg/Golana-DZ/releases/download/v0.1.0-beta/Golana-DZ-v0.1.0-beta.fwsc)

[**All DZ releases**](https://github.com/dzwerg/Golana-DZ/releases) · [DZ extension notes](docs/DZ_EDITION.md) · [Original Golana](https://github.com/Gadlele/Golana) · [Original Golana manual](docs/MANUAL.md)

## What's new in the DZ Edition?

| Feature | DZ Edition v0.1.0-beta |
| --- | --- |
| Step sequencer | **Up to 64 steps** per program (instead of 16) |
| Programs | **32** sound-and-pattern slots |
| Maximum sequential song length | **32 × 64 = 2,048 steps** (128 bars at 4/4 and 1/16 resolution) |
| Song Mode | Sequentially plays programs between selectable **FIRST** and **LAST** |
| Playback | **ONCE** or **REPEAT** (loop the program range) |
| USB MIDI OUT | Transmits played/sequence notes to external MIDI instruments |
| Persistence | Extended steps and Song Mode settings stored in a separate flash extension |
| Browser emulator | DZ compatibility changes in the emulator sources |
| Synth engine | The original Golana synthesizer and interface remain available |

The DZ extension has been **built and exercised on a physical M-VAVE FM-1**. Song Mode, sequential program playback, USB MIDI OUT and MIDI melody import have been tested on hardware. This is still a **beta**, not a comprehensive compatibility or long-term reliability certification.

## Quick start on the FM-1

### Edit a 64-step pattern

1. Turn **PRESETS** to select a program.
2. Press **REC** to reach **PATTERN**; enter notes and choose NOTE, REST or TIE for each step.
3. Press **REC** again for **SETUP** and set **LENGTH** (1–64), resolution, gate and swing.
4. Stop playback and use **SAVE** to write the program (sound **and** pattern) to its slot.

**Important:** Each saved program contains both the synthesizer settings and the sequencer pattern. Saving to an occupied program slot overwrites its previous contents.

### Play a song across programs

1. Create and **save** the patterns in consecutive program slots.
2. Press **SEQ** repeatedly until the **SONGMODE** page appears.
3. Set **SONGMODE = ON**.
4. Set **FIRST PROG** and **LAST PROG** to your desired range (for example, 1 to 3).
5. Choose **PLAY MODE = ONCE** or **REPEAT**.
6. Press **PLAY**.

The player advances in order from FIRST to LAST. **REPEAT** loops the complete selected range; **ONCE** stops at its end. Each program can have its own sound and pattern settings.

**Example:** Three 64-step programs contain up to 192 steps. At 1/16 resolution and 4/4 time, that is 12 bars. All 32 slots together can reach 2,048 steps when each uses its full 64-step length.

### Sequence external instruments via USB MIDI

Connect the FM-1 to a compatible USB-MIDI host (computer, supported mobile device or other host equipment), then route its outgoing MIDI notes to a synthesizer or sampler. **USB MIDI OUT has been verified on real hardware**, including sequence playback. Actual routing and host compatibility depend on the connected equipment.

The FM-1's internal Golana sound engine can still be used; **this is not yet a multi-track MIDI workstation**.

## Installation

**Download the DZ firmware package from this repository's [Releases](https://github.com/dzwerg/Golana-DZ/releases)**. The experimental v0.1.0-beta package is:

`Golana-DZ-v0.1.0-beta.fwsc`

Before flashing:

- Confirm that you have an **M-VAVE FM-1**, a reliable USB **data** cable and a suitable computer.
- Back up presets, patterns and any other data you cannot afford to lose.
- Keep a copy of your current working firmware and make sure you understand the recovery procedure.
- **Do not** use the original Golana web installer or its `Golana-1.0.fwsc` download if you intend to install DZ Edition.

### Command-line installation (Windows PowerShell)

The project includes the installer script inherited from Felucca/Golana. With Python installed:

```powershell
py -m pip install mido python-rtmidi
git clone https://github.com/dzwerg/Golana-DZ.git
cd Golana-DZ
py tools/fm1_install.py --info
py tools/fm1_install.py "C:\path\to\Golana-DZ-v0.1.0-beta.fwsc"
```

Replace the example path with the actual location of your downloaded `.fwsc` file. Read the installer's prompts before proceeding. For Linux/macOS, use `python3` instead of `py` if appropriate.

For background on building, installing and recovery, see [BUILDING.md](BUILDING.md). **Flash experimental firmware at your own risk.**

## Storage and firmware compatibility — read before flashing

The original Golana user-preset format stores the first 16 steps. DZ Edition adds **48 extra steps per program** (steps 17–64) and Song Mode settings in a separate flash record. Its code uses an A/B commit scheme and tries to select an unused legacy project-sector pair, preferring project D; if all such pairs are occupied, saving the extension is blocked.

These safeguards are implemented in [`firmware/src/golana_ext.c`](firmware/src/golana_ext.c), but **they are not a guarantee of compatibility with every existing flash layout or firmware downgrade**.

In particular:

- Make a backup before switching to DZ or switching back to original Golana/Felucca.
- Test **save → power off → restart → reload → verify steps 17–64** on your own hardware before relying on stored songs.
- Do not assume that another firmware understands or preserves DZ's extra-step storage.
- A program's first 16 steps and its additional 48 steps use different stored data structures; losing the extension can lose the latter.
- A/B storage reduces some failure risks but does **not** eliminate all risk of data loss or failed writes.

## Current limitations

- **One sequenced note per step** in the stored pattern. The Golana synth itself supports polyphonic playing, but stored patterns are not yet polyphonic piano-roll sequences.
- Song Mode plays a **contiguous program range**; it does not yet offer arbitrary song arrangements such as A–B–A–C without duplicating patterns.
- **Sounds and patterns are not independent**: a program stores both.
- No built-in Standard MIDI File (`.mid`) player and no complete General MIDI sound set. MIDI import demonstrated during development used **separate host-side scripts**, not an on-device MIDI file browser.
- Flash persistence, firmware transitions and every possible host/device MIDI combination still need wider testing.

## The Golana synthesizer is still here

DZ Edition retains the original Golana architecture: two oscillators (including saw, pulse, triangle and sine options), oscillator modulation, five filter types, independent filter and amplifier envelopes, LFOs, modulation routes, arpeggiator, effects and its distinctive on-device visual interface.

For the detailed instrument controls, see the [original Golana manual](docs/MANUAL.md) and [upstream project](https://github.com/Gadlele/Golana). Some original manual pages may describe the upstream **16-step** sequencer rather than DZ's 64-step extension; the instructions in this README take precedence for DZ-specific features.

## Building from source

This repository includes the source for the firmware and host tests. Builds need the JieLi toolchain, the AC79 SDK and the other prerequisites documented in [BUILDING.md](BUILDING.md).

On a configured Linux/WSL build system:

```bash
./build.sh
```

The ordinary development build writes `build/felucca.bin` and `build/felucca.fwsc`. The published v0.1.0-beta firmware package was built successfully, with the image and linker memory checks passing.

**Do not confuse the `.fwsc` update package with the `.bin` application file.**

## Roadmap (not yet implemented)

- Separate **synth sounds**, **sequencer patterns** and **song arrangements**.
- Independent pattern banks, reusable arrangements and better MIDI sequencing workflows.
- Extended persistent song capacity, with careful flash-layout design and testing.
- Explore a lightweight General MIDI-compatible sound engine / MIDI-file playback mode.

## Credits and licence

- **Golana**: [Gadlele](https://github.com/Gadlele/Golana) — original synthesizer, UI and project.
- **Felucca**: [Leo Kuroshita / Hügelton Instruments](https://github.com/hugelton/Felucca) — underlying firmware, tools and components.
- **Golana DZ Edition**: [dzwerg](https://github.com/dzwerg) — DZ-specific sequencer, Song Mode and MIDI extensions built on these projects.
- Other authors and third-party dependencies: [LICENSING.md](LICENSING.md) and [LICENSES/](LICENSES/).

Licensed under **GPL-3.0-only** as described in [LICENSE](LICENSE). The firmware update package also contains separately licensed components from the JieLi AC79 SDK; see the licence material attached to the [release](https://github.com/dzwerg/Golana-DZ/releases/tag/v0.1.0-beta).

This is an independent community firmware project. **M-VAVE and FM-1 are trademarks of their respective owners**; Golana DZ Edition is not affiliated with or endorsed by M-VAVE.

**Related project:** [miniBRUTE](https://github.com/dzwerg/miniBRUTE) - independent FM-1 synthesizer firmware (in development).
