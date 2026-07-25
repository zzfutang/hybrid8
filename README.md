# Hybrid 8 — AUv3 Analog + Wavetable Polysynth for Logic Pro

An 8-voice hybrid **analogue-modelled + wavetable** software synthesizer built as
an **Audio Unit v3 (AUv3) app extension** for macOS. Loads in Logic Pro and any
AU host. Header-only real-time C++ DSP, an Obj-C++ render bridge, and a SwiftUI
editor.

Coding by claude and codex.


## Features

| Section | Details |
|---|---|
| **Voices** | 8-voice polyphony with voice stealing; selectable 1–8 (mono ↔ poly), true legato, glide/portamento, two-card Unison with detune |
| **Osc 1 / Osc 2** | Two oscillators, each Saw / Square / Pulse (band-limited PolyBLEP) **or Wavetable**; independent octave, pulse width; Osc 2 semitone + fine detune |
| **Wavetable** | 2D wavetable osc: 5 built-in tables (Harmonic→guitar, FM, Choir, Metallic, **Piano** — extracted from a real sample), external 2048-sample WAV/AIFF tables can be imported, 32 morphable frames, "liveness" phase-drift; mip-pyramid band-limiting with per-octave crossfade |
| **Osc interaction** | Hard sync, Roland-style cross-mod (osc 2 → osc 1 FM), exponential or through-zero FM (inharmonic ratios give struck-bell timbres) |
| **Mixer** | Independent Osc 1 / Osc 2 / Noise levels, plus per-voice Stereo spread |
| **Filter** | Analogue-modelled **12 dB TPT state-variable** and **24 dB four-pole ladder** topologies; LP / BP / HP modes, stable self-oscillation, nonlinear Drive, key tracking and per-voice VCF tolerances; the ladder is passband-gain compensated so the two slopes stay level-matched |
| **Envelopes** | Dedicated ADSR for the VCA and for the filter (bipolar filter-env amount) |
| **LFO 1 / 2 / 3** | Three modulation sources with Sine, Square, Saw Up, Saw Down and Sample & Hold; independent rate, delay, bipolar/unipolar polarity and phase; per-LFO **run mode — Loop (free-running), Trig (key-synced restart) or One-Shot (single cycle then hold)**; selectable global vibrato source |
| **Mod matrix** | 6 assignable slots — sources: LFO 1/2/3, Filter/Amp Env, Velocity, Key Track, **Mod Wheel**, **Aftertouch**, Random → destinations: pitch, PW, cutoff, reso, drive, WT frame/liveness, cross-mod, amp, pan, filter slope/mode |
| **Arpeggiator** | Up / Down / Up-Down / Random, 1–4 octave range, free-running rate, gate length, **Hold** latch (early-80s Roland style) |
| **Effects** | Tempo-synced FX chain: stereo-linked Compressor → four-voice Chorus → filtered/saturated stereo ping-pong Delay → FDN Reverb |
| **Chord** | Optional chord trigger — expands each key into a chord (triads, 7ths, sus, dim, aug) with inversions, ahead of the arpeggiator |
| **Velocity** | Velocity → volume (hardwired) plus cutoff / reso / drive via the matrix |
| **Analogue modeling** | Per-voice pitch drift/detune, controllable phase un-sync (Spread), nonlinear filter feedback and deterministic voice-card component tolerances |
| **Anti-aliasing** | Complete oscillator, sync/FM, noise and nonlinear filter path runs at 2×, followed by an 11-tap linear-phase half-band FIR decimator |
| **Presets** | Factory presets loaded from a bundled, editable JSON catalog and grouped by category, plus user-preset save |

## Filter modelling

Hybrid 8 does not obtain its 24 dB response by simply cascading two copies of
the 12 dB filter. It runs two distinct virtual-analogue topologies:

- **12 dB/oct:** a two-pole topology-preserving-transform (TPT) state-variable
  filter based on the zero-delay-feedback approach associated with Cytomic and
  Zavalishin. The damping path is nonlinear, with three Newton iterations per
  sample solving the implicit feedback equation. At small signal levels the
  normalized nonlinearity reduces to the expected linear SVF response.
- **24 dB/oct:** four trapezoidal one-pole stages inside a nonlinear ladder
  feedback loop. The loop is solved with three Newton iterations and has no
  artificial unit delay. Maximum resonance slightly exceeds the theoretical
  four-pole oscillation threshold, producing bounded, cutoff-tracking
  self-oscillation. Because a ladder's linear passband gain is `1/(1+k)`, its
  output is compensated by `(1+k)` so the passband returns to unity — the two
  slopes stay level-matched and resonance peaks above the passband, matching the
  SVF.

Both filters produce low-pass, band-pass and high-pass outputs. The selected
mode and the 12/24 dB slope are interpolated continuously; both topologies keep
running during a slope change so automation does not expose stale state or
produce a topology-switch click.

The **Drive** control is a gain-compensated pre-filter `tanh` stage with an
exponential control taper. The 12 dB model also compresses its damping feedback,
while the 24 dB ladder saturates inside its resonance loop. Consequently Drive
changes harmonic content and resonance behavior rather than acting only as a
post-filter waveshaper. The **Analog** control adds a subtler normalized
nonlinearity and progressively reveals fixed per-voice VCF calibration:

- cutoff: up to ±18 cents;
- resonance: up to ±3%;
- saturation character: up to ±4%.

Those tolerances are deterministically seeded for each voice card, so voices
are slightly different while sessions and offline bounces remain repeatable.
At Analog = 0 the cards converge to the nominal mathematical response.

The oscillator mix, noise source and complete nonlinear filter path run at
twice the host sample rate. An 11-tap, linear-phase half-band FIR decimator
(approximately −45 dB stopband) filters the result before returning to the host
rate. This reduces fold-back from oscillator discontinuities, hard sync, FM,
filter drive and self-oscillation.

## Interface

The editor is a wide hardware-style fascia that scales uniformly to fit the host
window. A row of pills next to the title switches between two tabs:

- **Synth** — the sound-design engine: a combined **Oscillators** panel (Osc 1 with
  the wavetable display + Osc 2), **Mixer**, a combined **Envelopes** panel (Amp +
  Filter ADSR), **Filter**, **LFO** (LFO 1/2/3), **X-Mod**, and the **Mod Matrix**.
- **Performance** — how the instrument plays: **Effects Chain**, **Global** (voicing,
  unison, master, vibrato…), **Glide** (legato, portamento, glide start, pitch-bend
  range), **Arpeggiator**, **Chord Trigger**, and a searchable, category-filtered
  **Patch Navigator**.

The top bar carries the preset browser (◀ patch ▶ / Save), a live hover-help
readout that describes whatever control is under the pointer, and the stereo
output meter. Hovering any control also flips its label to its live value.

## Requirements

- **macOS 13+**
- **Full Xcode** (not just Command Line Tools) — required to build app extensions.
  Install it from the App Store. The build scripts auto-detect `/Applications/Xcode.app`
  via `DEVELOPER_DIR`. To make it the system default:
  ```sh
  sudo xcode-select -s /Applications/Xcode.app/Contents/Developer
  ```
- [XcodeGen](https://github.com/yonaskolb/XcodeGen) (`brew install xcodegen`) — generates the project.

## Build & install

```sh
./scripts/install.sh
```

This regenerates the Xcode project, builds the app + extension, copies the app to
`/Applications` (which registers the AUv3 with the system), launches it once, and
runs `auval` to validate. Then open Logic Pro — the instrument appears under
**AU Instruments → Johan → Hybrid 8**.

To just build, or run the offline DSP tests:

```sh
./scripts/build.sh          # Release
./scripts/build.sh Debug    # Debug
./scripts/test.sh           # build & run Tests/test_dsp.cpp (no Xcode needed)
```

## Using it in Logic Pro

1. Create a **Software Instrument** track.
2. Click the Instrument slot → **AU Instruments → Johan → Hybrid 8** (Stereo).
3. Play. Tweak the custom UI, or automate any parameter from Logic.

The bundled host app is also a standalone player: an on-screen keyboard, Musical
Typing, and CoreMIDI input for hardware controllers (mod wheel and aftertouch feed
the mod matrix).

If it doesn't appear in Logic, quit/reopen so it re-scans, or reset the AU cache:
```sh
killall -9 AudioComponentRegistrar 2>/dev/null; killall Logic\ Pro 2>/dev/null
```

## Project layout

```
project.yml                     XcodeGen project definition (2 targets)
App/                            Host app (registers + auditions the AU)
  Hybrid8App.swift, ContentView.swift, SynthHost.swift, TypingKeyboard.swift
Extension/                      The AUv3 extension
  SynthParameters.h             Shared parameter addresses (C ↔ Swift ↔ C++)
  ModMatrix.h                   Shared mod-matrix source/destination indices
  SynthAudioUnit.swift          AUAudioUnit subclass + factory presets
  SynthParametersTree.swift     Parameter tree definition
  FactoryPresets.swift          Validated loader for the bundled preset catalog
  Resources/FactoryPresets.json External factory preset definitions and categories
  AudioUnitViewController.swift AU factory + hosts the SwiftUI editor
  SynthDSPKernelAdapter.{h,mm}  Obj-C++ bridge + real-time render/MIDI loop
  DSP/                          Real-time-safe C++ DSP core (header-only)
    Oscillator, Wavetable (+ WavetablePianoData), ADSR, Filter, LFO, Decimator,
    Effects (compressor/chorus/delay/reverb), Voice,
    SynthEngine (voices + arp + chord + mod matrix), Params, Utils
  UI/                           SwiftUI editor (ParameterModel, SynthView, SynthTheme, SynthHelp)
Tools/                          Offline analysis/utilities (wav_analyze, synth_render,
                                synth_match, wt_extract) — see Tools/README.md
Tests/test_dsp.cpp              Offline DSP regression tests
scripts/                        build.sh, install.sh, test.sh
```

## Plug-in identity

- Type `aumu` · Subtype `Hy8v` · Manufacturer `Jhgn`
- Bundle IDs: app `com.johangorsjo.Hybrid8`, extension `com.johangorsjo.Hybrid8.AUv3`

Signed **ad-hoc** (`-`) for local use — no paid developer account needed. To
distribute to other machines, re-sign with a Developer ID and notarize.
