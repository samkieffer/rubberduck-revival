# Reverse-engineering notes — Rubber Duck H3O+ v2.12

Everything below was derived from the decompiled original and is implemented in
`core/`. Each block was verified bit-exact against the original routines with
`tools/oracle` (see the end of this file).

## Binary

| | |
|---|---|
| File | `rdh3o.exe` (from NSIS installer `Setup_RD_2.12.exe`) |
| SHA-256 | `3719f6d5d717286f3ce7658e9ad7332c8e8b944d1d331593ab985cf90dac9774` |
| Version info | "Rubber Duck H3O+ - the virtual bassline machine", 2.12, d-lusion interactive media, © 1996-2000 |
| Build | MSVC 6 / MFC 4.2 (static), PE32 i386, linked 2005-08-28, image base `0x400000`, no relocations |
| Helper DLLs | `scope.dll`, `selector.dll`, `switch.dll`, `text.dll` (UI controls only) |
| Audio out | `waveOut*` or DirectSound, through d-lusion's in-process "DAS" mixer |
| Sample rate | 44 100 Hz hard-coded; the voice is processed in 128-sample blocks |

Application code lives in `0x401000–0x41a000`; above that is statically linked
MFC/CRT. Many functions are only reached through vtables and must be created by
hand in Ghidra (16-byte-aligned entry points after `ret`/padding), then run the
"Decompiler Parameter ID" pass to get usable output.

Arithmetic: x87 with 53-bit precision (MSVC default). Values kept in registers
behave as doubles; values stored to memory are rounded to float. `__ftol`
truncates, `fistp` rounds to nearest even. The re-implementation mirrors where
each value is stored, and is built with `-ffp-contract=off`.

## Parameters

Parameter descriptors are created in `0x401660` (name, id, range, default) and
copied to the engine struct by `0x4027c0` without scaling, except Tune
(stored as knob − 128).

| id | name | range / default | engine field |
|---|---|---|---|
| 0 | Tempo | 0..255 / 128 → 60 + k·190/255 BPM | synth `+0x10` (float) |
| 1 | Volume | 0..255 / 128 | `+0x1c` |
| 2 | Tune | 0..255 / 128 | `+0x18` = k − 128 |
| 3 | Accent | 0..255 / 204 | `+0x08` |
| 4 | Osc1 Type | 0..3 / 1 (triangle, saw/pulse, saw, square) | `+0x0c` (byte) |
| 5 | Osc2 Type | 0..2 / 1 (sine, saw, noise) | `+0x0d` (byte) |
| 6 | Osc2 Freq×2 | 0..1 / 1 | `+0x10` |
| 7 | Osc1/Osc2 Mix | 0..255 / 0 | `+0x14` |
| 8 | Attack | 0..255 / 255 | `+0x20` |
| 9 | Decay | 0..255 / 128 | `+0x24` |
| 10 | Cut Off | 0..255 / 128 | `+0x2c` |
| 11 | Modulation | 0..255 / 128 | `+0x28` |
| 12 | Resonance | 0..255 / 64 | `+0x30` |
| 13 | Filter Modification ("m") | 0..1 / 1 | `+0x34` |
| 14 | Filter Squelch | 0..255 / 64 | `+0x44` |
| 15 | Filter Drive | 0..255 / 64 | `+0x40` |
| 16 | Filter Tweak | 0..1 / 0 | `+0x38` |
| 17 | Filter Tweet | 0..1 / 1 | `+0x3c` |
| 18 | FX Delay Time | 0..255 / 0 (×4 ms) | pattern memory `+0xdb4` |
| 19 | FX Delay Gain | 0..255 / 128 | `+0xdb8` |
| 20 | FX Delay Feedback | 0..255 / 128 | `+0xdbc` |
| 21 | FX Distort Drive | 0..255 / 128 | `+0xdb0` |
| 22 | FX Distort Gain | 0..255 / 0 | `+0xdac` |
| 23 | Sample Deck Volume | (sample players, not reproduced) | |

The original MIDI set-up can only map ids 10, 8, 9, 11, 12, 7, 1, 0, 3, 2 to
controllers; Squelch, Drive and the effects are not MIDI-controllable.

## .h30 machine-state files

MFC `CArchive`, little-endian:

```
CString  "Rubber Duck H3O+ * D-LUSiON"   (v1: "RuBBeR DuCK Sequence * D-LUSiON")
int32    version (1..4)
v3/v4:   int32 song length; uint8 song[99]; 16 patterns
v1/v2:   int32 (ignored); int32 n (song = 0..n); 14 patterns
pattern: 16 steps × { uint8 note (0xFE rest, 0xFF note off); int32 accent; int32 slide }
params (v2+, loader 0x403010):
  int32 tempo knob, int32 volume (ignored by the loader), int32 tune (signed),
  int32 accent, uint8 osc1 (+1 in v3), uint8 osc2 (+1 in v2), int32 osc2×2,
  int32 mix, int32 cutoff, int32 attack, int32 decay, int32 modulation,
  int32 resonance, [v4: int32 modification; v3: forced 1; v2: forced 0],
  [v3+: int32 squelch, drive, tweak, tweet], int32 (unknown, dialog +0x1830),
  int32 distortion A, int32 distortion B, int32 delay time, gain, feedback
```

The saver writes the distortion fields as (Gain, Drive) but the loader applies
them as (Drive, Gain), so they swap on every save/load; `H30File` reproduces
the loader.

## Voice (class vtable `0x4352d8`, render `0x408ca0`)

Per 128-sample block:

1. **Sequencer.** Position advances by `ftol(blockMs · 262144 · tempo / 60000)`
   per block (2^16 per step, 2^20 per bar), so steps start on block boundaries.
   On a new step: note `-1` → release, other notes set the target and, unless
   slide, retrigger; accent sets the peak to `128 + accent/2` (else 160) and
   always retriggers. The glide rate covers the note distance in one step. On
   a new bar, sequence/shuffle modes pick the next pattern from the song.
2. **Portamento.** Pitch = note·128 + fine; moves towards the target by the
   glide rate (8.8 fixed point).
3. **Pitch drift.** `phase += rand() << 9`; offset = `table[phase >> 23]`
   added to `(tune + 512) << 7`, times 65.41. The table is a 256-entry sine,
   but the index goes to 511 and reads the next 256 bytes of `.rdata`
   (vtables and constants): a pseudo-random ±3-cent jitter.
4. **Envelope** (fixed point, 0..65535): retrigger divides the level by 3;
   attack adds `(attack + 25)·64` up to `peak·256`; decay subtracts
   `decay·6 + 40`, but holds while gliding (unless above 0xEFFF); release
   subtracts 0x1000. Amplitude = level >> 8 (>> 10 with Filter Modification).
5. **Oscillators** (32-bit phase, increment from the pitch table
   `0.0625·2^(k/384)` × base / 44100 × 32768): triangle, saw-then-silence,
   saw, square, sine (two cycles per phase turn, so an octave up), noise
   (`rand()`, unipolar). Osc2 copies osc1's phase every block unless ×2.
   Levels are split by Mix and summed by the fixed-point mixer.
6. **Cutoff.** `c = 1 − ln(256 − cutoff)/ln 256` (smoothed 15/16 per block);
   the envelope is shaped by Modulation between a log curve
   (`ln(env+1)/ln 65536`), linear and an inverse-log curve; frequency =
   `(3200c + 100)·shape + 60` Hz.
7. **Filter, Modification off:** 4-stage one-pole cascade with resonance
   feedback (`0x40d930`).
   **Modification on** (default), two branches mixed by the fixed-point mixer:
   - main: state-variable "double low-pass" (input >> 3, damping 0.5) at the
     cutoff, track gain `(266 − resonance)·1024`;
   - acid: half level → band-pass SVF at the cutoff (×2 with Tweet), damping
     from Squelch × inverse-log envelope → table clipper with gain
     `1 + 100·(1 − ln(256 − drive/2)/ln 256)` (rectifying + DC blocker with
     Tweak), track gain `resonance·512`.
   The weighted sum is then brought back to unity gain with saturation, so
   Resonance both mixes in the acid branch and drives it into clipping.
8. After all blocks of a period: distortion (clipping one-pole low-pass mixed
   with the dry signal) and a feedback delay (up to 1020 ms).

The output stage applies Volume (left `vol/256`, right `vol·129/32768`).

## Mixer ("DAS")

Tracks of 128-sample slots with a gain each (0x8000 = unity). Merging two
tracks weights them by `gain/(gainA+gainB)` with the MMX path (`pmulhw`/
`pmullw` split of a 16.16 weight, `paddsw` saturation) and adds the gains;
"apply gain" rescales samples (saturating above unity). In a fresh process,
slots start at unity gain and never-used slots leak into the first period —
a start-up artefact not reproduced.

## Verification

`tools/oracle` loads `rdh3o.exe` into a Wine process at its image base,
patches `rand`, `malloc` and `free`, and calls the original routines next to
the re-implementation:

- ladder filter, all SVF modes, all shaper modes and every shaper table;
- the full voice render driven through a mock mixer: all 112 pattern files of
  the installer (v1–v4) and randomised parameter automation, for any number
  of blocks per period — 0 differing samples.

`tests/reference_hashes.txt` holds hashes of the original's output for every
pattern; `rdcheck` (ctest `bit_exact`) checks a build against them.
