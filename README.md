# Hybrid 8 — AUv3 Analog + Wavetable Polysynth for Logic Pro

An 8-voice hybrid **analogue-modelled + wavetable** software synthesizer built as
an **Audio Unit v3 (AUv3) app extension** for macOS. Loads in Logic Pro and any
AU host. Header-only real-time C++ DSP, an Obj-C++ render bridge, and a SwiftUI
editor.

## Features

| Section | Details |
|---|---|
| **Voices** | 8-voice polyphony with voice stealing; selectable 1–8 (mono ↔ poly), true legato, glide/portamento |
| **Osc 1 / Osc 2** | Two oscillators, each Saw / Square / Pulse (band-limited PolyBLEP) **or Wavetable**; independent octave, pulse width; Osc 2 semitone + fine detune |
| **Wavetable** | 2D wavetable osc: 4 tables (Harmonic→guitar, FM, Choir, Metallic), 32 morphable frames, "liveness" phase-drift; mip-pyramid band-limiting with per-octave crossfade |
| **Osc interaction** | Hard sync, Roland-style cross-mod (osc 2 → osc 1 FM), exponential or through-zero FM |
| **Mixer** | Independent Osc 1 / Osc 2 / Noise levels |
| **Filter** | TPT/Cytomic state-variable low-pass, **12 dB / 24 dB** slope, resonance, overdrive **Drive**, key tracking |
| **Envelopes** | Dedicated ADSR for the VCA and for the filter (bipolar filter-env amount) |
| **LFO 1 / LFO 2** | Sine / Square / Saw; LFO 1 has key-trigger + delay and a hardwired vibrato route; LFO 2 is a matrix source |
| **Mod matrix** | 6 assignable slots — sources: LFO 1/2, Filter/Amp Env, Velocity, Key Track, **Mod Wheel**, **Aftertouch**, Random → destinations: pitch, PW, cutoff, reso, drive, WT frame/liveness, cross-mod, amp |
| **Arpeggiator** | Up / Down / Up-Down / Random, 1–4 octave range, free-running rate, gate length, **Hold** latch (early-80s Roland style) |
| **Effects** | Four-voice stereo chorus followed by a filtered, saturated stereo/ping-pong delay |
| **Velocity** | Velocity → volume (hardwired) plus cutoff / reso / drive via the matrix |
| **Analogue modeling** | Per-voice pitch drift/detune, controllable phase un-sync (Spread), tanh filter saturation |
| **Anti-aliasing** | 2× oversampled osc/sync/FM path with an 11-tap linear-phase half-band decimator |
| **Presets** | 55 factory presets grouped by category, plus user-preset save |

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
  FactoryPresets.swift          Built-in presets (+ legacy → mod-matrix migration)
  AudioUnitViewController.swift AU factory + hosts the SwiftUI editor
  SynthDSPKernelAdapter.{h,mm}  Obj-C++ bridge + real-time render/MIDI loop
  DSP/                          Real-time-safe C++ DSP core (header-only)
    Oscillator, Wavetable, ADSR, Filter, LFO, Decimator, Effects (chorus+delay),
    Voice, SynthEngine (voices + arp + mod matrix), Params, Utils
  UI/                           SwiftUI editor (ParameterModel, SynthView, SynthTheme, SynthHelp)
Tests/test_dsp.cpp              Offline DSP regression tests
scripts/                        build.sh, install.sh, test.sh
```

## Plug-in identity

- Type `aumu` · Subtype `Hy8v` · Manufacturer `Jhgn`
- Bundle IDs: app `com.johangorsjo.Hybrid8`, extension `com.johangorsjo.Hybrid8.AUv3`

Signed **ad-hoc** (`-`) for local use — no paid developer account needed. To
distribute to other machines, re-sign with a Developer ID and notarize.
