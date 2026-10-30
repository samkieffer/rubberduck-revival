# RubDuck (repository rubberduck-revival) — notes for Claude

Goal: faithful re-implementation of d-lusion's Rubber Duck H3O+ as an AU/VST3/Standalone
plugin for macOS (Apple Silicon), primarily used in Ableton Live 12. Two engines: v2.12
(Win32, 2000, `rd::Engine`) and v1.02 (RUBDUCK.EXE, 1997, `rd::EngineV1`, a different voice).

Fidelity to the original sound is the top priority. The engine is re-derived from the
decompiled original (see `docs/reverse-engineering.md`), not designed from scratch: when
in doubt, match what the original code does, including its integer/fixed-point quirks.

## Layout
- `core/`   portable C++17 engines, no dependencies, run internally at 44.1 kHz like the originals.
- `tools/`  CLI tools (render `.h30` to WAV, A/B comparison).
- `plugin/` JUCE wrapper (AU, VST3, Standalone).
- `docs/`   reverse-engineering notes.
- `reference/` (git-ignored) original binaries and `.h30` patterns, never committed.

## Conventions
- Keep `core/` free of JUCE so it can be tested headless on Linux and macOS.
- Every engine change should be checked against reference renders of the original.
- Do not commit original d-lusion binaries, graphics or patterns.

## Build & test
- Engine + tools: `cmake -S . -B build -DRD_BUILD_PLUGIN=OFF && cmake --build build -j && (cd build && ctest)`
- Plugin (fetches JUCE 9.0.4; on macOS installs AU/VST3 into ~/Library/Audio/Plug-Ins):
  `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --config Release -j`
- `rdcheck tests/fixtures tests/fixtures/hashes.txt` must report 0 mismatches on every platform;
  if it fails on macOS/arm64, suspect FMA contraction or libm differences (sin/log/exp).
- The oracles (tools/oracle) need Wine, mingw-w64 (i686) and the original rdh3o.exe / RUBDUCK.EXE
  in reference/ (`oracle.exe` for v2.12, `oracle_v1.exe` for v1.02).
