# Tools — sample analysis & sound matching

Command-line utilities for recreating a recorded sample as a Hybrid 8 patch, via
**spectral-distance sound matching** (render the synth, minimise a spectral
distance to the target — the standard approach from the automatic-synth-
programming literature, here with a plain parameter search instead of a neural
net).

| Tool | What it does |
|---|---|
| `wav_analyze.cpp` | Reads a WAV (PCM16/24/32 + float32, skips JUNK). Prints format, amplitude **envelope** (attack/sustain/decay character), **fundamental** pitch + note, **harmonic magnitudes** vs. the fundamental (waveform fingerprint), **spectral-centroid over time** (filter-envelope sweep), and **stereo width / detune-beat** cues. |
| `synth_render.cpp` | Renders a hard-coded patch through the real `SynthEngine` to `/tmp/out.wav` (note-on at 0, note-off at `gate`). Key params are `key=value` overridable (`cut=`, `res=`, `env=`, `o2oct=`, `o1=`, `o2=`, …) for fast manual iteration. |
| `synth_match.cpp` | Grid-searches osc/filter params to match a target's **steady-state harmonic** vector (precise on the sustained timbre). |
| `synth_match2.cpp` | The full matcher: minimises a **time-segmented log-magnitude spectrogram** distance (16 frames × log-freq bands) by coordinate descent over ~20 params — so the **envelope** (filter closing, amp decay) is matched too, not just one frame. Reads the target WAV directly; writes the best render to `/tmp/out.wav`. |
| `wt_extract.cpp` | **Wavetable extractor.** Pitch-detects a recorded sample and samples `WT_NUM_FRAMES` single-cycle frames across the note's evolution (attack → decay), converting each to a harmonic-magnitude spectrum. Emits a C++ header that the synth compiles in as a **built-in** wavetable set (band-limited into mip pyramids at load, like the hand-designed tables). Used to build the **Piano** table from `piano.wav`. |
| `hybrid8_wt_resolution_audit.cpp` | Renders matched Clean / 12-bit / 8-bit / Vintage wavetable tones through the raw oscillator and full engine, writes 24-bit listening WAVs, and reports null-residual distortion relative to Clean. |

### Regenerating the built-in Piano wavetable

```sh
clang++ -std=c++17 -O2 Tools/wt_extract.cpp -o /tmp/wt_extract
/tmp/wt_extract piano.wav Piano > Extension/DSP/WavetablePianoData.hpp
# rebuild — Wavetable.hpp includes the header and exposes it as WT set 4
```

To make another sample a built-in set: run `wt_extract yoursample.wav Name`, add a
`wtSpectrum<Name>` generator + `case` in `Wavetable.hpp`, bump `WT_NUM_SETS`, and
add the name to `WavetableStore.factoryEntries`.

## Build & run

```sh
clang++ -std=c++17 -O2 Tools/wav_analyze.cpp -o /tmp/wav_analyze
/tmp/wav_analyze keys.wav

clang++ -std=c++17 -O2 -I Extension Tools/synth_match2.cpp -o /tmp/synth_match2
/tmp/synth_match2 juno_pad.wav 2.0 48      # target, gate(s), MIDI note
```

## Notes / limitations

- The spectrogram matcher's time frames are ~125 ms, so it does not resolve very
  fast attacks well — take the **attack timing from `wav_analyze`'s envelope**
  and the **spectrum/filter from the matchers**.
- A subtractive synth can only approximate real-instrument recordings (inharmonic
  tine/formant detail is out of reach); the tools get the oscillator mix, filter
  cutoff/resonance and envelopes into the right region.
