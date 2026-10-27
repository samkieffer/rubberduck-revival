# Reverse-engineering notes — Rubber Duck H3O+ v2.12

## Binary

| | |
|---|---|
| File | `rdh3o.exe` (from NSIS installer `Setup_RD_2.12.exe`) |
| SHA-256 | `3719f6d5d717286f3ce7658e9ad7332c8e8b944d1d331593ab985cf90dac9774` |
| Version info | "Rubber Duck H3O+ - the virtual bassline machine", 2.12, d-lusion interactive media, © 1996-2000 |
| Build | MSVC 6 / MFC 4.2 (static), PE32 i386, linked 2005-08-28, image base `0x400000`, no relocations |
| Helper DLLs | `scope.dll`, `selector.dll`, `switch.dll`, `text.dll` (UI controls only) |
| Audio out | `waveOut*` or DirectSound (loaded at runtime), or d-lusion "DAS" audio server via shared memory `d-lusion.com:DAS.mem` |
| Sample rate | 44 100 Hz hard-coded (constant `0xAC44` appears ~16 times) |

Application code lives in `0x401000–0x41a000`; above that is statically linked MFC/CRT.

Tooling: Ghidra 11.4.2 headless. Many functions are only reached through vtables and are
not found by auto-analysis; they were created from 16-byte-aligned entry points following
`ret`/padding.

## MIDI / control surface of the original

`Setup > Midi & Joystick setup` exposes only 10 realtime targets (Cutoff, Attack, Decay,
Modulation, Resonance, Osc1/Osc2, Volume, Tempo, Accent, Tuning) plus Program Change
(pattern select) and MIDI clock sync. Squelch, Drive, Tweak, Tweet, delay and distortion
are **not** MIDI-controllable in the original — one of the reasons for this project.

## Synth object (constructor `FUN_00408ac0`, vtable `0x4352d8`)

| Offset | Content |
|---|---|
| `+0x004` | running flag |
| `+0x008` / `+0x00c` | sequencer position (fixed point, `>>20` = step) / previous position |
| `+0x010` | tempo, float (default `120.0f`) |
| `+0x014` / `+0x018` | slide (portamento) rate / accumulator |
| `+0x01c` / `+0x01d` | target note / current note |
| `+0x020` | sub-object, class ctor `0x40d040` — vtable slot 0 = note on, 1 = render, 2 = note off |
| `+0x02c` | second instance of class `0x40d040` |
| `+0x03c`, `+0x050` | voice-state structs, class `0x40bb50` (`+0x3e` = amplitude from envelope) |
| `+0x038` | envelope stage (0 idle, 1 trigger, 2 attack, 3 decay, 4 release) |
| `+0x03a` | envelope peak (accent: `(accent >> 1) - 0x80`, else `0xa0`) |
| `+0x068` | envelope value (fixed point) |
| `+0x070` | class `0x40d3b0`, initialised with `(0x3fc, 44100)` |
| `+0x090`, `+0x2a4` | 2 × class `0x410880` (size `0x214`) |
| `+0x4b8` | pointer to pattern memory (16 patterns × `0xc0` bytes, 12 bytes per step) |
| `+0x4bc` | noise accumulator (`+= rand() * 0x200`) |
| `+0x4c0` | class `0x40d870` |
| `+0x4fc`, `+0x520` | 2 × class `0x40d590` (tempo-synced, delay candidates) |

## Render function `FUN_00408ca0(this, params, globals, start, count)`

Control-rate loop over blocks. Per block:
1. Dispatch queued MIDI events (note on/off, program change → pattern, transport, song position).
2. Advance sequencer; on step change, read step (`note`, `accent`, `slide`; `-1` = note off,
   `-2` = rest) and handle pattern chaining (sequence / shuffle modes).
3. Portamento: target vs current note, rate computed from note distance.
4. Envelope state machine:
   - attack: `env += (attack + 25) * 64` until `peak << 8`
   - decay: `env -= decay * 6 + 40` (only once slide/glide settled or env > `0xefff`)
   - release: `env -= 0x1000`
   - amplitude = `env >> 8` (or `>> 10` depending on a mode flag)
5. Render oscillators / filter through the sub-objects' vtables, then effects.

(Work in progress — details of oscillators, filter, distortion and delay to follow.)
