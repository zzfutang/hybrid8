# Hybrid 8 — AUv3 Analog + Wavetable Polysynth for Logic Pro

An 8-voice, analogue-modelled software synthesizer built as an **Audio Unit v3
(AUv3) app extension** for macOS. Loads in Logic Pro and any AU host.

## Features

| Section | Details |
|---|---|
| **Voices** | 8-voice polyphony with voice stealing (oldest released first) |
| **Oscillator** | Saw / Square / Pulse, band-limited (PolyBLEP anti-aliasing) |
| **Pulse Width** | Continuous pulse-width control (Pulse wave) |
| **Octave** | −4 … +4 octave selector |
| **Noise** | White-noise generator with osc↔noise **Mix** control |
| **Amp** | Dedicated ADSR envelope for the VCA |
| **Filter** | 2-pole state-variable low-pass, **12 dB / 24 dB** switch, resonance |
| **Filter Env** | Dedicated ADSR for filter cutoff, bipolar env amount, key tracking |
| **LFO** | Sine / Square / Saw, rate control |
| **LFO routing** | → Osc Freq · → Pulse Width · → Cutoff · → Resonance |
| **Analogue modeling** | Per-voice pitch drift/detune, phase un-sync, tanh filter saturation |
| **Global** | Analog amount, master gain, pitch-bend range |

## Requirements

- **macOS 13+**
- **Full Xcode** (not just Command Line Tools) — required to build app extensions.
  Install it from the App Store. The build scripts auto-detect `/Applications/Xcode.app`
  via `DEVELOPER_DIR`, so you don't strictly need `xcode-select`. If you want it to be
  the system default anyway:
  ```sh
  sudo xcode-select -s /Applications/Xcode.app/Contents/Developer
  ```
- [XcodeGen](https://github.com/yonyz/XcodeGen) (`brew install xcodegen`) — generates the project.

## Build & install

```sh
./scripts/install.sh
```

This regenerates the Xcode project, builds the app + extension, copies the app to
`/Applications` (which registers the AUv3 with the system), launches it once, and
runs `auval` to validate. Then open Logic Pro — the instrument appears under
**AU Instruments → Johan → Hybrid 8**.

To just build without installing:

```sh
./scripts/build.sh          # Release
./scripts/build.sh Debug    # Debug
```

## Using it in Logic Pro

1. Create a **Software Instrument** track.
2. Click the Instrument slot → **AU Instruments → Johan → Hybrid 8** (Stereo).
3. Play. Tweak knobs in the plug-in window (custom UI) or via Logic's controls.

If it doesn't appear, quit and reopen Logic so it re-scans, or reset the AU cache:
```sh
killall -9 AudioComponentRegistrar 2>/dev/null; killall Logic\ Pro 2>/dev/null
```

## Project layout

```
project.yml                     XcodeGen project definition (2 targets)
App/                            Host app (registers + auditions the AU)
  Hybrid8App.swift, ContentView.swift, SynthHost.swift
Extension/                      The AUv3 extension
  SynthParameters.h             Shared parameter addresses (C ↔ Swift ↔ C++)
  SynthAudioUnit.swift          AUAudioUnit subclass + parameter tree
  SynthParametersTree.swift     Parameter tree definition
  AudioUnitViewController.swift AU factory + hosts the SwiftUI editor
  SynthDSPKernelAdapter.{h,mm}  Obj-C++ bridge + real-time render/MIDI loop
  DSP/                          Real-time-safe C++ DSP core (header-only)
    Oscillator, ADSR, Filter, LFO, Voice, SynthEngine, Params, Utils
  UI/                           SwiftUI editor (ParameterModel, SynthView)
scripts/                        build.sh, install.sh
```

## Plug-in identity

- Type `aumu` · Subtype `An8v` · Manufacturer `Jhgn`
- Bundle IDs: app `com.johangorsjo.Hybrid8`, extension `com.johangorsjo.Hybrid8.AUv3`

Signed **ad-hoc** (`-`) for local use — no paid developer account needed. To
distribute to other machines, re-sign with a Developer ID and notarize.
