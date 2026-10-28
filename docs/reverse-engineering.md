# Reverse-engineering notes — Rubber Duck H3O+ v2.12 and v1.02

Everything below was derived from the decompiled originals and is implemented in
`core/`. Each block was verified bit-exact against the original routines with
`tools/oracle` (see the end of each part). The first part covers v2.12
(`rd::Engine`), the second v1.02 (`rd::EngineV1`), whose voice is different.

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

---

# Rubber Duck H3O+ v1.02 (1997)

## Binary

| | |
|---|---|
| File | `RUBDUCK.EXE` |
| SHA-256 | `69e952781f252f799333f1694882672797c46c83b5478f725afd4de6c9c477e1` |
| Version info | "Rubber Duck H3O+ - ACiD for the masses", 1.02, D-LUSiON Interactive Media, © 1996, 1997 |
| Build | MSVC 5 / MFC42.DLL and MSVCRT.DLL (dynamic), PE32 i386, linked 1997-06-09, image base `0x400000`, no relocations |
| Audio out | `waveOut*` or DirectSound, through the in-process mixer |
| Sample rate | 44 100 Hz; the voice is processed in **64-sample** blocks |

Application code is `0x401000–0x410000`. `_controlfp(_PC_53, _MCW_PC)` at start-up:
the x87 runs with 53-bit precision as in v2.12; `_ftol` is imported from MSVCRT.

## Parameters

Parameter struct (constructor `0x40c720`, filled from the dialog by `0x402170`)
and pattern memory (constructor `0x40c640`):

| field | default | notes |
|---|---|---|
| `+0x08` accent | 200 | |
| `+0x0c` osc1 type (byte) | 1 | 0 saw/pulse, 1 saw, 2 square (no triangle) |
| `+0x0d` osc2 type (byte) | 0 | 0 sine (octave up), 1 saw, 2 noise |
| `+0x10` osc2 ×2 | 0 | |
| `+0x14` tune | 0 | knob − 128 |
| `+0x18` volume | 100 | |
| `+0x1c` osc mix | 128 | |
| `+0x20` attack | 255 | |
| `+0x24` decay | 128 | |
| `+0x28` modulation | 128 | |
| `+0x2c` cutoff | 64 | |
| `+0x30` resonance | 64 | |
| `+0x34` tempo knob | 54 | 60 + k·190/255 BPM |
| pattern memory `+0xb14` | 64 | distortion amount (wet, 0 = off) |
| `+0xb18` | 64 | distortion curve |
| `+0xb1c` | 0 | delay time (×4 ms) |
| `+0xb20` | 64 | delay gain |
| `+0xb24` | 0 | delay feedback |

There is no Filter Modification, Squelch, Drive, Tweak or Tweet.

## Patterns and play modes

14 patterns of 16 steps (`{note, accent, slide}`, 12 bytes each) at pattern
memory `+0`, then current pattern `+0xa80`, step `+0xa88`, range start/end
`+0xa8c/+0xa90` (UI combos "1".."E") and play mode `+0xa94`: 0 single, 1 next
pattern in the range at each bar, 2 random `start + rand()·(end − start + 1)/32767`,
which picks `end + 1` when `rand()` returns 32767. Range checks accept index 14,
one past the last pattern, which then reads the sequencer state and the sample
deck; that case plays as rests here.

## .h30 files

v1.02 writes **version 2** files (saver `0x40b1c0`, loader `0x40b5e0`); version 1
files ("RuBBeR DuCK Sequence") have the same layout without parameters:

```
CString "Rubber Duck H3O+ * D-LUSiON", int32 2,
int32 range start, int32 range end, 14 patterns × 16 × {uint8 note, int32 accent, int32 slide},
int32 tempo knob, volume, tune, accent, uint8 osc1, uint8 osc2, int32 osc2×2, mix,
cutoff, attack, decay, modulation, resonance, play mode,
distortion amount, distortion curve, delay time, delay gain, delay feedback
```

Unlike v2.12, the v1.02 loader applies the volume. The v2.12 loader reads this
format as "song 0..range end", ignores the play mode and swaps the two
distortion fields into its own, different, distortion.

## Voice (class vtable `0x412ea0`: init `0x40c870`, render `0x40c910`)

Init snaps tune, cutoff, modulation and resonance smoothing and sets the
"recompute" flags (filter reset, oscillator types, tempo). Per 64-sample block:

1. **Sequencer**, a counter relative to the current step: increment
   `ftol(blockMs · 262144 · tempo / 60000)` with `blockMs = (float)(64000/44100)`.
   If the counter is **above** 0x10000 it loses 0x10000 and the step advances
   (wrapping the bar and applying the play mode); a counter of 0 (start)
   triggers the current step; then the increment is added. Step trigger: note
   0xFF releases, 0xFE keeps the note, other notes set the target and (without
   slide) the pitch and retrigger; accent always retriggers with peak
   `128 + accent/2`, else peak 160. Glide rate: `|distance·8 / (7·blocksPerStep)| + 1`
   (8.7 fixed point, `blocksPerStep = 0x10000 / increment`).
2. **Portamento** by the glide rate; **tune** smoothed by ±1 per block.
3. **Pitch drift** as v2.12 (`phase += rand() << 9`, 512-byte table read past its
   256-entry sine, here into `.rdata` at `0x412de0`): base frequency
   `ftol((drift + (tune + 512) << 7) · 65.41)`, doubled for osc2 with ×2.
4. **Envelope**: retrigger divides the level by 3 and restarts the oscillator
   phase; attack `+(attack + 25)·32` up to `peak·256`; decay `−(decay·3 + 25)`,
   held while gliding below 0xE600; release `−0x1000`. Amplitude = level >> 9,
   split by Mix (`(255 − mix)/255`, `mix/255`).
5. **Oscillators** (`0x404cd0`): increment `pitch[note·32 + fine/4] · freq / 44100 · 32768`
   from a 4096-entry table at `0x415030` (`0.0625·2^(k/384)` up to k = 3850,
   then whatever follows in `.data`); waveforms saw-then-silence, saw, square
   (osc1) and sine ×2 / saw / unipolar noise (osc2). Osc2 copies osc1's state
   every block, keeping its own phase with ×2.
6. **Mixer** (integer): osc2 merged into osc1 with weights `ftol(g·65536/(g1+g2))`
   (`(a·w1 + b·w2) >> 16`), then "apply gain" `g/32768` (`s·ftol(g·65536)/65536`
   toward zero, or saturating and rounding down above unity).
7. **Filter** (`0x4013f0`), two chained state-variable stages on `0.13·input`:
   ```
   L1 = l1 + f·b1;  L2 = l2 + f·b2;  l1 = L1;  l2 = L2
   b2 = (l1 − L2 − d·l1)·f + l1;  b1 += f·(0.13·x − L1 − d·b1);  y = trunc(l2)
   ```
   (states stored as float, output wrapped to 16 bits), then ×2 with saturation.
   `f = 2·sin(π·hz/22050)` (the rate is stored halved) and `d = 1/(2 + 400·R)`, with
   ```
   cut = 1 − ln(1021 − 4·cutoff)/ln 1024     M = ln(modulation + 1)/ln 256
   E   = 1 − ln(65536 − level)/ln 65536      (inverse-log envelope)
   hz  = 3000·M·cut + 11000·E·(1 − M) + 100·cut + 40
   R   = (1 − ln(256 − resonance)/ln 256) · E
   ```
   so Modulation crossfades the cutoff from the envelope (0) to the Cut Off
   knob (255), and resonance only opens with the envelope. Cutoff, modulation
   and resonance are smoothed by ±1 per block.
8. After all blocks of a period: **distortion** (`0x4011c0`/`0x401290`), a 257-point
   table `±32767·ln(1 + i·(curve + 1)/128)/ln(curve + 2)` interpolated and mixed
   `(v·amount + s·(255 − amount))/256`, then the same **delay** as v2.12 (up to 1020 ms).

The output mixer (`0x405df0`) gives the track gain `volume << 8`, split by the
centre pan into 128/256 (left) and 129/256 (right), then applies it as above.

## Verification

`tools/oracle/oracle_v1` loads `RUBDUCK.EXE` the same way (MFC42 imports left
unresolved, `rand`/`_ftol`/`malloc`/`free` redirected through the import table)
and drives the original voice with a mixer laid out like `0x405a10`:

- every pattern file of the installer content folder (70), 48 synthetic files
  and 4 files made with the original v1.02 (`tests/fixtures/user_*.h30`),
  2000 blocks each, play mode and range from the file — 0 differing samples
  (the 4 user files also over 60 s with periods of 1 to 96 blocks);
- randomised automation of every parameter, tempo and play mode, periods of
  1, 3, 8 and 32 blocks — 0 differing samples;
- all 256 distortion tables, and the output stage (both channels, all volumes).

`tests/reference_hashes.txt` and `tests/fixtures/hashes.txt` lines tagged `|v1`
hold the original's output hashes; `engine_v1_sync` checks that host-driven
positions reproduce the free-running sequencer.
