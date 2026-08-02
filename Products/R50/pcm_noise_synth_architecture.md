# R50 — PCM + Noise Synthesizer Architecture

Adapted from `hybrid_pcm_va_soft_synth_architecture.md` for this repository's
platform, with the virtual-analog source removed. **PCM (looped multisamples,
one-shot transients, single-cycle waves) and noise are the only sound
sources.**

---

## 0. Status and relationship to existing code

This is the earlier PCM/noise-only target architecture, not a description of
what is built today. The implementation status and current product boundary
are tracked in §34–35 of `hybrid_pcm_va_soft_synth_architecture.md`; where the
documents disagree, that later roadmap is authoritative.

The R50 that exists in this repo is a virtual-analog subtractive synth: one
PolyBLEP oscillator per voice into the shared ZDF filter. Under this design its
*oscillator* is replaced outright. Everything else R50 already has survives and
is the reason this is a tractable project rather than a rewrite:

| Existing R50 asset | Fate under this design |
|---|---|
| `R50DSPKernelAdapter.mm` render/MIDI loop | Keep as-is |
| Atomic parameter store + per-control-block `snapshotParams()` | Keep; extend |
| Voice allocation, sustain/key-state handling | Keep; extend to Partials |
| `Shared/DSPCore` filter, ADSR, LFO, utils | Keep |
| `Shared/DSPCore/Oscillator.hpp` (PolyBLEP) | **Dropped from the signal path** |
| SwiftUI editor shell, theme, parameter model | Keep; restructure into pages |
| `Tests/test_r50.cpp` + `scripts/test-r50.sh` (incl. TSan pass) | Keep; extend |

The source document targets a generic C++/JUCE-style project with VST3, AU and
standalone builds. This repo is narrower and more opinionated, and the
architecture is adjusted to it rather than the other way round.

---

## 1. Product goal

A polyphonic PCM instrument in the spirit of late-1980s workstations — sampled
attack transients over looped sustains, single-cycle digital waves, noise as a
structural element — without copying any ROM data, presets or proprietary
implementation details.

The instrument combines:

- looped multisampled sustaining instruments;
- sampled attack transients;
- single-cycle PCM waves, band-limited across the keyboard;
- a first-class noise source with selectable spectrum;
- four Partials per Patch, two Partials per Tone, two Tones per Patch;
- ring modulation, layering, velocity/key crossfades, serial routing;
- multi-stage envelopes, LFOs, and a modulation matrix;
- chorus, delay, and reverb;
- AUv3 and standalone targets.

The central design rule is unchanged from the source document:

```text
Partial = sound source + pitch + tone shaping + amplitude

2 Partials = Tone
2 Tones    = Patch
Patch      = effects + performance controls + preset state
```

### 1.1 What removing VA actually costs, and what pays for it

Dropping the VA oscillator is not merely deleting a `SourceType`. Three things
the source document leaned on VA for must be replaced:

| VA provided | Replacement in this design |
|---|---|
| Band-limited saw/pulse/tri at any pitch | Single-cycle PCM waves with a **mip pyramid** (§5.2) — this is the single most important technical requirement in this document |
| Continuous sustain that never loops audibly | Looped multisamples with long multi-cycle loops (§5.6); single-cycle waves for synthetic timbres |
| Pulse-width modulation, sync, supersaw | Wave-sequence morphing between single-cycle tables, phase-reset sync on single-cycle sources, unison detune across PCM voices (§7) |
| Noise as a minor VA sub-feature | A first-class noise source with spectrum, filtering and S&H modes (§6) |

The gain is a smaller, more focused engine: one source type to optimise, one
interpolation path to get right, and a coherent sonic identity that does not
compete with Hybrid 8 (which is already the analog-modelled product in this
repo). R50 becoming a PCM instrument is what makes two products worth having.

---

## 2. Platform mapping

This is the section that most differs from the source document.

### 2.1 Repository layout

The source document recommends `synth-core / plugin-wrapper / editor / assets /
tests`. This repo already enforces that separation with different names, and the
existing split should be used rather than a parallel one:

```text
Shared/DSPCore/                 Framework-independent, header-only, reusable DSP
Shared/InstrumentHost/          Product identity value types
Shared/PerformanceKeyboard/     On-screen keyboard, musical typing
Products/R50/DSP/               R50's engine — header-only C++, no AU/ObjC types
Products/R50/Extension/         AUv3 appex: parameters, AU class, ObjC++ bridge
Products/R50/Extension/UI/      SwiftUI editor
Products/R50/Extension/Resources/  Factory presets + factory sample assets
Products/R50/App/               Standalone container app
Tests/test_r50.cpp              Offline regression tests
scripts/{build,install,test}-r50.sh
```

The rule the source document states as "the DSP engine must not depend on JUCE,
VST classes, or Apple Audio Unit classes" already holds here and must continue
to: `Products/R50/DSP/*.hpp` includes only the C++ standard library,
`Shared/DSPCore`, and the plain-C `R50Parameters.h`. The only file that knows
about AudioToolbox is `R50DSPKernelAdapter.mm`.

### 2.2 Targets

**AU only.** There is no VST3 target and none is planned — that would mean the
Steinberg SDK, a second wrapper, and a second signing/notarisation story. The
two targets are the AUv3 app-extension and the standalone container app that
registers it, both declared in `project.yml`.

"Standalone" here means the container app hosting its own appex in-process
(`R50AudioUnitHost`), not a separate engine build.

### 2.3 Reusable prior art in this repo

Three pieces of Hybrid 8 code solve problems this design hits directly:

| Problem | Prior art | Status |
|---|---|---|
| Single-cycle aliasing | `Products/Hybrid8/DSP/Hybrid8Wavetable.hpp` — 10-level mip pyramid, additive per-level rebuild, per-octave crossfade, loudness normalisation | Product code. Copy into `Products/R50/DSP/`, or promote a generalised `Shared/DSPCore/MipTable.hpp` |
| Chorus / delay / reverb / compressor | `Products/Hybrid8/DSP/Hybrid8EffectsRack.hpp` | Product code. Same choice |
| Sandboxed user sample import | `Extension/UI/WavetableStore.swift` — imports via user-selected read-only, copies into Application Support | Product code. Same pattern applies directly |

Recommendation, consistent with how R50 was built: **copy first, promote after.**
A second real caller is what reveals which parts are genuinely generic. The mip
pyramid in particular is currently entangled with Hybrid 8's spectrum model and
should not be promoted until R50's PCM needs are known concretely.

### 2.4 Constraints this platform imposes that the source document does not discuss

1. **AU parameter budget.** See §17. Hybrid 8 exposes 123 AU parameters; the
   source document's full hierarchy implies roughly twice that. This needs an
   explicit two-tier strategy, not a bigger enum.
2. **App-extension sandbox.** The appex has `com.apple.security.app-sandbox` and
   only `files.user-selected.read-only`. It cannot browse the filesystem for
   samples. Factory content ships inside the appex bundle; user content is
   imported through a panel and copied into Application Support (§19).
3. **Bundle size and distribution.** The appex is embedded in the app, which is
   ad-hoc signed and distributed as a zip. A 50–100 asset factory set is fine;
   a multi-gigabyte library is not, and would need a separate installer.
4. **Process model.** Each AU instance may run in its own appex process. Sample
   data is therefore *not* shared between instances by default. Large factory
   sets should be memory-mapped read-only rather than heap-copied per instance.
5. **MIDI.** The current adapter handles MIDI 1.0 channel-voice events and
   explicitly ignores SysEx and MIDI 2.0 UMP. MPE (§12) requires the UMP /
   `AUMIDIEventListBlock` path and is deliberately deferred.

---

## 3. Sound hierarchy

### 3.1 Partial

A Partial is the smallest complete sound-generating unit.

```text
PCM or Noise source
    -> pitch stage
    -> optional waveshaper
    -> filter
    -> amplifier
    -> pan
```

```cpp
enum class SourceType {
    Pcm,        // multisample, one-shot, or single-cycle
    Noise
};
```

Two source types, not three. `VirtualAnalog` is gone; the synthetic waveforms it
provided are PCM single-cycle assets.

All parameter structs are plain-old-data with **fixed capacity** — no
`std::vector` anywhere reachable from the render thread. The source document
uses `std::vector<ModulationRoute>` and `std::vector<EnvelopePoint>`; both become
fixed arrays with counts.

```cpp
struct PartialParameters {
    bool       enabled    = true;
    SourceType sourceType = SourceType::Pcm;

    PcmSourceParameters   pcm;
    NoiseSourceParameters noise;

    PitchParameters       pitch;
    WaveshaperParameters  waveshaper;
    FilterParameters      filter;
    AmplifierParameters   amp;
    PanParameters         pan;

    EnvelopeParameters    pitchEnvelope;
    EnvelopeParameters    filterEnvelope;
    EnvelopeParameters    ampEnvelope;

    ModulationRoute routes[kMaxRoutesPerPartial];   // fixed
    int             routeCount = 0;
};
```

### 3.2 Tone

Two Partials plus one structure algorithm.

```cpp
enum class ToneStructure {
    Mix,
    RingMod,
    RingModWithDry,
    VelocityCrossfade,
    KeyCrossfade,
    SerialFilter,
    ParallelFilter,
    PhaseSync,          // was OscillatorSync — single-cycle sources only
    AttackSustain       // was PcmAttackVaSustain — now PCM attack + PCM sustain
};
```

### 3.3 Patch

```cpp
enum class PatchStructure {
    Layer,
    KeySplit,
    VelocitySplit,
    VelocityCrossfade,
    VectorMix,
    SerialToneProcessing
};

struct PatchParameters {
    ToneParameters         toneA;
    ToneParameters         toneB;
    PatchStructure         patchStructure;
    VoiceMode              voiceMode;
    PerformanceParameters  performance;
    GlobalEffectsParameters effects;
    MasterParameters       master;
};
```

---

## 4. Per-voice DSP graph

```text
MIDI note
   |
   v
Voice state (note, velocity, age, key/pedal state)
   |
   +--> Tone A --> Partial A1 --+
   |                Partial A2 --+--> Tone structure --+
   |                                                    |
   +--> Tone B --> Partial B1 --+                       +--> Patch structure
                    Partial B2 --+--> Tone structure --+          |
                                                                  v
                                                        per-voice pan / gain
                                                                  |
                                                                  v
                                                            voice mix bus
```

### 4.1 Load budget on this platform

DSP load scales with **Partials, not notes**. With the source document's target
of 32 voices × 4 Partials = 128 concurrent Partials, each Partial running an
interpolating sample reader, a filter and three envelopes.

R50 ships today with 8 voices. The realistic staging on Apple Silicon:

```text
v1:  16 voices × 2 Partials  =  32 Partials
v2:  16 voices × 4 Partials  =  64 Partials
v3:  32 voices × 4 Partials  = 128 Partials
```

Voice and Partial storage is preallocated at maximum size in the engine object;
`enabled = false` Partials are skipped, not deallocated. Measure before raising
the count — the ZDF filter runs three Newton iterations per sample and is the
dominant per-Partial cost, not the sample reader.

---

## 5. PCM source architecture

### 5.1 Source categories

1. **Single-cycle waves** — one period, band-limited via mip pyramid, looped
   forever. The replacement for VA oscillators.
2. **One-shot transients** — attack samples, played once, no loop.
3. **Looped multisamples** — recorded sustaining instruments, key/velocity
   mapped, multi-cycle loops.

A single `PcmSource` reads all three; they differ only in loop mode, mapping,
and whether a mip pyramid is present.

### 5.2 Band-limiting single-cycle waves — the central requirement

**This is the problem that VA removal creates and it must be solved first.**

A single-cycle table played back at an arbitrary rate is not band-limited. A
2048-sample saw table contains harmonics up to the 1024th; transposed up two
octaves at 44.1 kHz, every harmonic above the 250th folds back as inharmonic
aliasing. PolyBLEP solved this for VA. Naive interpolation does not.

The solution, already proven in `Hybrid8Wavetable.hpp` in this repo:

```text
Build a mip pyramid per single-cycle asset:
  level 0  = full spectrum          (lowest octave)
  level L  = harmonics limited to those below ~20 kHz when played an
             octave higher than level L-1
  10 levels covers the full keyboard

At render time:
  fractionalLevel = log2(playbackIncrement) + offset
  read the two adjacent integer levels and crossfade

Crossfading adjacent levels — rather than snapping — is what keeps glide,
pitch envelopes and vibrato from stepping audibly across octave boundaries.
```

Each level is built by additive resynthesis from the asset's spectrum, with
loudness normalised across levels so the timbre does not jump in level as pitch
crosses a boundary.

Pyramid construction is **offline** — it allocates and runs FFT-scale work, and
must happen on the loader thread, never on the render thread (§19).

Notes:

- **Looped multisamples do not need a full pyramid.** Real recordings have
  little energy near Nyquist, and transposition ranges are small (a well-mapped
  multisample transposes at most a few semitones). Good interpolation is
  sufficient. Add per-region mips only if measurement shows a problem.
- **One-shot transients** are played at or near unity rate and need no pyramid.
- The pyramid is a property of the *asset*, built once at load, shared
  immutably by every voice.

### 5.3 Sample asset model

```cpp
struct SampleData {
    // Interleaved-free: separate channels, immutable after publish.
    std::vector<float> left;
    std::vector<float> right;   // empty when mono

    double   sourceSampleRate = 44100.0;
    int      rootKey          = 60;
    float    tuneCents        = 0.0f; // playback correction from WAV metadata
    uint32_t loopStart        = 0;
    uint32_t loopEnd          = 0;
    LoopMode loopMode         = LoopMode::None;

    // Present only for single-cycle assets.
    bool        hasMipPyramid = false;
    MipPyramid  pyramid;

    std::string assetId;
    uint64_t    contentHash = 0;
};
```

`SampleData` is built entirely on a background thread and is **immutable once
published**. The render thread only ever holds a raw pointer to a published,
never-mutated instance (§19).

For RIFF/WAVE input the loader reads the `smpl` chunk's MIDI unity note, MIDI
pitch fraction, and first supported loop record. The pitch fraction describes
how far above the integer unity note the recording lies; R50 stores the inverse
as its playback correction (`tuneCents`), so a sample declared 14 cents sharp
is played 14 cents down. `smpl` loop ends are inclusive and must be converted
once, at load time, to R50's exclusive `loopEnd`. Loop type 0 maps to Forward
and type 1 to PingPong; unsupported loop types are diagnosed and treated as no
loop rather than guessed. Malformed roots or loop bounds never reach the render
thread.

### 5.4 Multisample regions

```cpp
struct SampleRegion {
    int      lowKey = 0,  highKey = 127;
    int      lowVelocity = 1, highVelocity = 127;
    int      rootKey = 60;
    float    tuneCents = 0.0f;
    float    gainDb    = 0.0f;
    uint32_t sampleStart = 0, sampleEnd = 0;
    uint32_t loopStart   = 0, loopEnd   = 0;
    LoopMode loopMode    = LoopMode::Forward;
    int      sampleAssetIndex = -1;   // index, not string, on the render thread
};

struct Multisample {
    std::string  id;
    std::string  name;
    SampleRegion regions[kMaxRegions];
    int          regionCount = 0;
};
```

Region lookup on note-on is a linear scan over `regionCount` — bounded, cheap,
and allocation-free. Sort regions by key at load time so the scan exits early.

A WAV `smpl` chunk describes one sample, not a multisample keyboard map.
Therefore a multisample is formed by grouping WAV files into one instrument.
The factory manifest supplies explicit key/velocity bounds; a future
multi-file user import may derive key bounds from the midpoints between
adjacent `smpl` roots, but must show the derived map before committing it.
Explicit manifest/import values override embedded metadata; omitted root,
tuning and loop values inherit from each WAV.

#### 5.4.1 Directory-based factory instruments

Factory content also needs a low-friction path that does not require editing
the monolithic catalog for every ordinary multisample. Each immediate child
directory of `factory_samples/` may represent one instrument:

```text
factory_samples/Flute/
    instrument.json       # optional
    flute-C3.wav
    flute-F3.wav
    flute-A3.wav
    flute-C4.wav
```

With no `instrument.json`, this is an automatic root-zoned instrument. Every
WAV must state a valid `smpl` unity note. Sort by `(rootKey, filename)` and put
each boundary halfway between adjacent roots; the lower-root zone owns an exact
tie, and the outside zones extend to keys 0 and 127. Embedded pitch fraction
and loop data remain per-zone. Do not infer roots from filenames.

Automatic mode supports exactly one velocity layer and one WAV per root.
Duplicate/missing roots, malformed metadata, unsupported loop types, decode
failure, or region-count overflow reject the complete directory atomically.
These constraints prevent a convenient import convention from becoming an
ambiguous sampler format.

An optional versioned `instrument.json` supplies `id`, display `name`, and
explicit zones. It is required for velocity layers, manual key bounds,
intentional overlaps/gaps, or metadata overrides. Zone fields use the same
precedence as the top-level manifest:

```text
explicit zone field
    > embedded WAV smpl field
    > loader fallback (only where explicitly allowed)
```

Only direct child WAV filenames are accepted in the directory manifest:
absolute paths, `..`, symlinks escaping the directory, and recursive discovery
are rejected.

Identity and presentation are separate:

```text
Instrument display name:  "Concert Flute"             (shown in the selector)
Instrument ID:            "factory.concert_flute"     (stored in patch/preset state)
Zone ID:                  "factory.concert_flute/c4"  (loader/database identity)
Source file:              "Flute C4 -45.wav"          (storage only)
Render-thread slot:        integer             (resolved before note-on)
```

`instrument.json.name` defaults to the directory basename. Its explicit `id`
is globally unique. Each zone has an `id` unique within that instrument; the
complete zone asset ID is `instrument-id + "/" + zone-id`. Renaming the browser
label or WAV file therefore does not break a preset when the IDs and mapping
remain unchanged. Patch state stores only the instrument ID. The selected
region carries a pre-resolved integer slot, so no strings or paths enter the
render thread.

For zero-manifest auditioning, canonical IDs are derived from the relative
directory path and WAV filename stem under `factory.auto.*`. Loose WAVs use
`factory.loose.*`. These IDs are rename-sensitive and must not be used by
released presets. Explicit IDs accept lowercase ASCII letters, digits, `.`,
`-`, and `_`; collisions at any scope reject the instrument. The legacy
top-level manifest gains the same instrument/zone `id` fields, with temporary
compatibility IDs only during migration.

Discovery is deterministic: load the existing ordered top-level manifest, then
append immediate child directories sorted bytewise by relative path. Loose WAV
handling remains separate. Every instrument needs a globally unique persistent
ID; an automatic directory temporarily derives one from its relative path,
while factory content used by presets should declare one explicitly. AU
automation retains the numeric selector, but `fullState` stores an
`R50SampleAssetIDs` sidecar for all four Partials and resolves those IDs on
restore, so adding a directory cannot silently repoint saved sound.

Note the asset reference is an **index**, not a `std::string`. String comparison
and `std::string` copies must not occur on the render thread; asset IDs are
resolved to indices when the patch is applied.

### 5.5 Playback position and interpolation

```cpp
struct PlaybackState {
    double position  = 0.0;   // double precision, not float
    double increment = 1.0;
    int    direction = 1;     // ping-pong
};
```

```cpp
double semitones = midiNote - region.rootKey
                 + region.tuneCents / 100.0
                 + pitchModulationSemitones;

double ratio = std::exp2(semitones / 12.0);

state.increment = ratio * sample.sourceSampleRate / outputSampleRate;
```

`float` position is insufficient: at 24-bit mantissa precision, a position past
~16.7 M samples (≈6 minutes at 44.1 kHz) loses sub-sample resolution. Long
one-shots and slow-scanned samples need `double`.

```cpp
enum class InterpolationMode { Linear, Cubic, Sinc };
```

| Mode | Use |
|---|---|
| Linear | Draft, deliberate lo-fi character, S&H noise |
| Cubic (Hermite) | **Default.** Good quality-to-cost ratio |
| Polyphase sinc | High-quality offline bounce; opt-in in real time |

Every interpolation tap must be **loop-aware**: a 4-point cubic kernel near the
loop end must fetch its trailing taps from after `loopStart`, not from the raw
sample past `loopEnd`. Getting this wrong produces a per-loop click that is easy
to mistake for a bad loop point. This is a required test case (§21).

### 5.6 Loops

```text
Single-cycle synthetic waves:   one period
Short stable loop:              30–100 ms
Typical orchestral sustain:     100–400 ms
Complex choir/string loop:      250–1000 ms
```

Support forward loops, ping-pong loops, loop crossfades, separate release
samples, and one-shot attacks feeding looped sustains.

Loop crossfade is precomputed into the published `SampleData` at load time, not
computed per-sample at render time.

### 5.7 Starter sample set

Roughly 50–100 assets is enough for v1.

**Attack transients** — piano hammer, tine, mallet, pick, slap, breath, chiff,
bowed scrape, brass tongue, metallic strike, noise burst, reverse transient.

**Looped multisamples** — piano, electric piano, organ, flute, clarinet, cello,
string ensemble, choir, brass section, bass guitar, synth pad, bell sustain.

**Single-cycle waves** — sine, triangle, saw, square, pulse 25%, pulse 12.5%,
organ drawbar spectra, additive spectra, metallic/inharmonic spectra,
vocal/formant waves, noisy waves, digital stepped waves.

The single-cycle set carries more weight in this design than in the source
document, because it is the only route to sustained synthetic timbres. Budget
for 20–30 single-cycle waves rather than a token handful, and generate them
analytically where possible — an analytic spectrum produces a cleaner mip
pyramid than an extracted one.

---

## 6. Noise source

Noise is promoted from a VA sub-feature to a first-class source, because it is
now one of only two source types and carries all non-pitched material.

```cpp
enum class NoiseSpectrum {
    White,
    Pink,       // -3 dB/octave
    Brown,      // -6 dB/octave
    Blue,       // +3 dB/octave
    Violet,     // +6 dB/octave
    Filtered,   // band-passed, pitch-tracked
    SampleHold  // stepped, rate-controlled — digital/aliased character
};

struct NoiseSourceParameters {
    NoiseSpectrum spectrum   = NoiseSpectrum::White;
    float         level      = 1.0f;
    bool          pitchTrack = false;  // Filtered/SampleHold follow the note
    float         bandwidth  = 1.0f;   // Filtered mode
    float         rateHz     = 8000.0f;// SampleHold step rate
    bool          stereo     = false;  // decorrelated L/R
};
```

`Shared/DSPCore/Utils.hpp` already provides `FastRandom` (xorshift64), which is
allocation-free and deterministic per voice — reuse it. Pink and brown are
one-pole filtered white; blue and violet are differentiated white.

Noise earns its place in three roles:

1. **Attack component** — noise burst under a PCM transient, shaped by a fast
   envelope. Replaces the VA noise layer.
2. **Breath / bow / air** — pitch-tracked band-passed noise layered under a
   looped sustain, which is how the source document's flute and bowed examples
   are meant to breathe.
3. **Ring-mod partner and modulation source** — S&H noise into ring modulation
   produces the metallic digital textures the era is known for.

---

## 7. Tone structures

### 7.1 Mix

```cpp
toneOutput = partial1Level * p1 + partial2Level * p2;
```

### 7.2 Ring modulation

```cpp
float ring = p1 * p2;
toneOutput = dry1 * p1 + dry2 * p2 + ringLevel * ring;
```

Ring modulation doubles the bandwidth of its inputs and will alias against
Nyquist. Oversample the multiply 2× when `ringLevel > 0`.
`Shared/DSPCore/Decimator.hpp` (11-tap linear-phase half-band FIR) is exactly
the tool for the downsample leg and is already shared, reusable code.

### 7.3 Velocity crossfade

```cpp
float x = velocityCurve(velocity);
toneOutput = equalPowerA(x) * p1 + equalPowerB(x) * p2;
```

### 7.4 Key crossfade

```cpp
float x = smoothStep(key, crossfadeLow, crossfadeHigh);
toneOutput = equalPowerA(x) * p1 + equalPowerB(x) * p2;
```

### 7.5 Serial filter

```cpp
float mixed  = p1 + p2;
float stage1 = filter1.process(mixed);
float output = filter2.process(stage1);
```

### 7.6 Phase sync (revised)

The source document's `OscillatorSync` assumed VA oscillators. The equivalent
for PCM is meaningful **only for single-cycle sources**: reset the slave
Partial's read position when the master's wraps.

```cpp
if (masterPartial.justWrapped())
    slavePartial.resetPhase();
```

For looped multisamples this is disabled — resetting a multi-cycle loop's read
position at audio rate produces noise, not sync. The UI must grey out this
structure unless both Partials are single-cycle sources.

### 7.7 Attack/sustain (revised)

Formerly `PcmAttackVaSustain`. Now PCM attack over a PCM or noise sustain, which
is the D-50 structure and remains the most important one in the instrument:

```cpp
toneOutput = transientEnvelope * transientPartial
           + sustainEnvelope   * sustainPartial;
```

The transient Partial is typically a one-shot with a fast decay; the sustain
Partial a looped multisample or single-cycle wave. Because both sides are now
PCM, the crossfade is more forgiving than the original PCM→VA blend: matching
the spectral character across the join no longer means matching a sample to an
oscillator.

### 7.8 Unison (replacing supersaw)

Supersaw was a VA feature. Its replacement is unison across PCM voices: N
detuned copies of the same Partial with per-copy stereo placement and phase
offset.

```cpp
struct UnisonParameters {
    int   voices       = 1;      // 1..7
    float detuneCents  = 0.0f;
    float stereoSpread = 0.0f;
    float phaseSpread  = 0.0f;   // start-position scatter
};
```

Unison multiplies Partial count — a 4-Partial patch at 7-voice unison is 28
Partials per note. Cap `voices × Partials` in the allocator and expose the
resulting effective polyphony in the UI (§20).

---

## 8. Filter architecture

Each Partial has its own filter.

```cpp
enum class FilterType {
    Off,
    DigitalLowPass,   // non-resonant, workstation-style
    LowPass12, LowPass24,
    HighPass12, BandPass12, Notch,
    LadderLowPass
};
```

### 8.1 Digital (non-resonant) low-pass

The characteristic workstation filter — two cascaded one-poles, no resonance.
Cheap, and tonally correct for the target sound.

```cpp
s1 += g * (input - s1);
s2 += g * (s1 - s2);
return s2;
```

### 8.2 Resonant multimode and ladder

`Shared/DSPCore/Filter.hpp` already provides both: a nonlinear TPT
state-variable (`SVFStage`), a four-pole zero-delay-feedback ladder
(`FourPoleLadder`), and `LadderFilter` which runs both and crossfades. Reuse it
unchanged — it is shared, already validated by Hybrid 8, and R50 already uses
it today.

Its cost is the reason for offering `DigitalLowPass`: three Newton iterations
per sample per Partial is the dominant load at high Partial counts (§4.1). Many
PCM patches do not need a resonant filter at all.

### 8.3 Filter modulation

Compute cutoff in semitone space, then exponentiate once:

```cpp
float cutoffSemitones = baseCutoff
                      + keyTrackAmount * (note - 60)
                      + envelopeAmount * filterEnvelope
                      + modulationAmount;

float cutoffHz = referenceHz * std::exp2(cutoffSemitones / 12.0f);
```

Per the existing R50 engine, coefficients are recomputed **once per control
block** (`kControlBlock = 32`), not per sample. `setParams()` runs a `tan()` and
is far too costly to call per sample per Partial.

---

## 9. Waveshaping

```cpp
enum class WaveshaperType { Off, SoftClip, HardClip, Fold, Rectify, Polynomial, Table };
enum class WaveshaperPosition { PreFilter, PostFilter };
```

Table-based shaping uses a fixed-size table in the parameter block — never a
`std::vector` sized at render time. Strong nonlinear modes (`Fold`, `HardClip`)
must be oversampled 2× using `Shared/DSPCore/Decimator.hpp`.

---

## 10. Envelopes

Each Partial has pitch, filter and amplitude envelopes. Multi-stage, not ADSR —
this is what gives PCM instruments their shape.

The source document's `std::vector<EnvelopePoint>` is replaced by fixed arrays:

```cpp
enum class EnvelopeCurve { Linear, Exponential, Logarithmic, SShape, Step };

struct EnvelopePoint {
    float         level;
    float         timeSeconds;
    EnvelopeCurve curve;
};

static constexpr int kMaxAttackPoints  = 4;
static constexpr int kMaxReleasePoints = 3;

struct EnvelopeParameters {
    EnvelopePoint attackPoints[kMaxAttackPoints];
    int           attackPointCount = 0;
    int           sustainPoint     = -1;
    EnvelopePoint releasePoints[kMaxReleasePoints];
    int           releasePointCount = 0;

    bool velocityToTime  = false;
    bool velocityToLevel = false;
    bool keyTrackTime    = false;
};
```

`4 attack + 1 sustain + 3 release` is enough for evolving workstation patches
and keeps the struct POD and copyable into the render thread's snapshot.

`Shared/DSPCore/ADSR.hpp` remains available for simple cases and for the
existing amp envelope; the multi-stage envelope is a new
`Products/R50/DSP/R50Envelope.hpp` until a second product needs it.

---

## 11. LFOs

Two LFOs per Tone, shared by its Partials.

```cpp
enum class LfoWaveform { Sine, Triangle, SawUp, SawDown, Square, SampleHold, SmoothRandom };

struct LfoParameters {
    LfoWaveform waveform;
    float rateHz;
    bool  tempoSync;
    float tempoDivision;
    bool  retrigger;
    float phase;
    float delaySeconds;
    float fadeSeconds;
    bool  unipolar;
};
```

`Shared/DSPCore/LFO.hpp` covers most of this already. Tempo sync requires the
host tempo, which the adapter obtains via `AUHostMusicalContextBlock` — Hybrid 8
already does this and R50's adapter currently does not; it must be added back
when tempo sync lands.

---

## 12. Modulation matrix

### 12.1 Sources

```cpp
enum class ModSource {
    Velocity, ReleaseVelocity, KeyTrack, PitchBend, ModWheel,
    ChannelPressure, PolyPressure,
    Lfo1, Lfo2,
    PitchEnvelope, FilterEnvelope, AmpEnvelope,
    RandomPerVoice, NoteAge,
    Macro1, Macro2, Macro3, Macro4
};
```

MPE sources (`MpePressure`, `MpeTimbre`) from the source document are **deferred**
— they require the MIDI 2.0 UMP path the adapter currently ignores (§2.4).

### 12.2 Destinations

Revised for PCM + noise:

```cpp
enum class ModDestination {
    Pitch, FinePitch,
    PcmSampleStart,       // scan into the attack
    PcmLoopPosition,      // move the loop window
    PcmPlaybackRate,      // decoupled from pitch — tape-style
    WaveIndex,            // morph between single-cycle tables
    NoiseLevel, NoiseBandwidth,
    OscillatorLevel,
    WaveshaperDrive,
    FilterCutoff, FilterResonance,
    AmpLevel, Pan,
    ToneBalance, PatchBalance,
    EffectSend1, EffectSend2
};
```

`VaPulseWidth` and `VaShape` are gone. `WaveIndex` replaces them as the primary
timbral-motion destination: morphing between adjacent single-cycle tables is
this instrument's equivalent of PWM, and is the most expressive destination in
the matrix.

### 12.3 Route

```cpp
struct ModulationRoute {
    ModSource      source;
    ModDestination destination;
    float          amount;
    ModCurve       curve;
    bool           bipolar;
    bool           audioRate;
};
```

Fixed-size arrays per Partial and per Tone, with counts.

### 12.4 Processing rates

**Audio rate** — pitch, wave index, ring modulation depth, filter cutoff when
explicitly requested.

**Control rate** (per `kControlBlock`) — envelopes, LFOs, pan, effect sends,
most MIDI controllers, and filter cutoff by default.

Defaulting cutoff to control rate is what makes high Partial counts affordable.
Expose the audio-rate flag per route rather than globally.

---

## 13. Voice allocation

```cpp
enum class VoiceMode { Poly, Mono, Legato, Unison };
```

Priority order:

1. free voice;
2. quietest released voice;
3. oldest released voice;
4. quietest active voice;
5. oldest active voice.

When stealing, fade the old voice over 1–5 ms rather than hard-resetting, to
avoid a click. With PCM this matters more than with VA: a stolen sample stops
mid-waveform at an arbitrary amplitude.

Required behaviours: sustain pedal, sostenuto, repeated-note policy, note IDs,
mono note priority, glide.

R50 already implements sustain correctly, including the case the code review
caught — physical key state is tracked separately from the CC64 gate so a note
retriggered under the pedal is not released on pedal-up. That logic carries over
unchanged and its regression test already exists in `Tests/test_r50.cpp`.

---

## 14. Effects

### 14.1 Per-voice

Only what belongs to sound generation: ring modulation, waveshaping, filter,
pan, per-Partial drive.

### 14.2 Patch effects

```text
Voice sum -> Insert 1 -> Insert 2 -> Chorus send -> Delay send
          -> Reverb send -> Master EQ -> Output
```

```cpp
enum class EffectType {
    Off, Chorus, Ensemble, Phaser, Flanger,
    StereoDelay, Reverb, Overdrive, ParametricEq
};
```

Effects matter disproportionately for this target sound — many classic
workstation patches are as much processing as source material. A dry PCM
multisample sounds thin; the chorus and reverb are not garnish.

`Hybrid8EffectsRack.hpp` provides working stereo chorus, ping-pong delay, FDN
reverb and compressor. Copy it into `Products/R50/DSP/` for v1 (§2.3).

---

## 15. Engine interface and AU bridge

The engine stays framework-independent:

```cpp
namespace r50 {

class R50Engine {
public:
    void setSampleRate(double sr);
    void reset();

    void render(float* outL, float* outR, int frameCount);

    void noteOn(uint8_t note, uint8_t velocity);
    void noteOff(uint8_t note);
    void pitchBend(int value14);
    void sustainPedal(bool down);
    void allNotesOff();
    void allSoundOff();
    void setTempo(double bpm);

    void  setParameter(uint64_t address, float value);
    float getParameter(uint64_t address) const;
    void  startParameterRamp(uint64_t address, float value, uint32_t frames);

    // Patch state beyond the AU parameter tree (§17, §18).
    bool applyPatchState(const uint8_t* data, size_t size);
    size_t writePatchState(uint8_t* data, size_t capacity) const;

    // Sample publication — called from the loader thread only (§19).
    bool publishSampleSlot(int slot, SampleData* immutableData);
};

} // namespace r50
```

This is the interface R50 already has, plus patch state and sample publication.
`R50DSPKernelAdapter.mm` remains the only AU-aware file, and its render-loop
event walk needs no change.

---

## 16. Real-time safety

The audio thread must never allocate, free, lock, touch the filesystem, parse
JSON, decode samples, or log.

This repo already enforces the important half of that and has the tooling to
prove it:

- **Atomic parameter store.** Writers touch only `std::atomic<float> store_[]`;
  the denormalised block the voices read is derived on the render thread once
  per control block in `snapshotParams()`. No locks, no allocation.
- **ThreadSanitizer in CI.** `scripts/test-r50.sh` runs the suite twice, the
  second pass under TSan with a concurrent parameter-writer test. This is a real
  race detector, not a convention — it was verified to catch a deliberately
  reintroduced plain-float write.
- **Preallocated voices.** Fixed-size arrays, no per-note allocation.

What this design adds and must preserve: immutable published sample objects,
deferred deletion of retired samples, background loading, and index-based
(never string-based) asset references on the render thread.

---

## 17. Parameter system — the AU budget problem

This is the largest single adaptation from the source document.

### 17.1 The problem

The source document says "expose only meaningful automatable parameters to the
host" but does not quantify it. On this platform the numbers are stark:

```text
Hybrid 8 today:                          123 AU parameters
R50 today:                                19 AU parameters

This design, exposed naively:
  per Partial: source + pitch + filter + amp + pan
               + 3 multi-stage envelopes (8 points × 3 fields)
               + modulation routes                    ≈ 90+
  × 4 Partials                                        ≈ 360
  + 2 Tones (structure, level, pan, zones, 2 LFOs)    ≈  60
  + Patch (structure, performance, master)            ≈  30
  + effects                                           ≈  60
                                                      -------
                                                      ≈ 510
```

AUParameterTree can hold 510 parameters. Logic's automation menu, the generic
AU view, and every host's parameter list become unusable at that size, and
`AUParameter` allocation cost at instantiation becomes noticeable.

### 17.2 The solution: two tiers

**Tier 1 — AU parameters (target ≤ 100).** Everything a user would automate or
assign to a controller:

- 4 macros (the primary automation surface);
- per-Tone level, pan, balance;
- filter cutoff/resonance/drive per Tone (not per Partial);
- amp attack/decay/sustain/release per Tone;
- effect sends and key effect parameters;
- performance: master, glide, bend range, voice mode.

**Tier 2 — patch state.** Everything else — envelope points, region maps,
per-Partial routing, sample assignments, wave selections — lives in a versioned
binary/JSON patch blob exchanged through `AUAudioUnit.fullState` /
`fullStateForDocument`, and edited only in R50's own UI.

This is what the source document means by "do not use array positions as
persistent IDs" and "keep deep multisample editing as internal patch state",
made concrete for this platform.

### 17.3 Consequences

- The `R50Param` C enum stays flat and stable; new Tier-1 parameters are
  appended, never inserted.
- `implementorValueObserver` continues to feed the atomic store.
- Tier-2 edits go through a separate, non-real-time path: the UI mutates a patch
  object, serialises it, and hands it to the engine via `applyPatchState()`,
  which is called on the main thread and swaps in a new immutable snapshot.
- Undo/redo operates on the patch object, not on AU parameters.

---

## 18. Preset format

Versioned, asset-referencing, and never embedding sample data:

```json
{
  "format": "R50PcmPatch",
  "version": 1,
  "name": "Glass Horizon",
  "author": "Factory",
  "category": "Pad",
  "patchStructure": "Layer",
  "tones": [
    {
      "structure": "AttackSustain",
      "partials": [
        { "sourceType": "Pcm", "assetId": "breath_attack_01", "loopMode": "None" },
        { "sourceType": "Pcm", "assetId": "singlecycle_glass_07", "loopMode": "Forward" }
      ]
    },
    {
      "structure": "Mix",
      "partials": [
        { "sourceType": "Pcm",   "multisampleId": "choir_ah" },
        { "sourceType": "Noise", "spectrum": "Filtered", "pitchTrack": true }
      ]
    }
  ],
  "effects": {
    "chorus": { "enabled": true, "mix": 0.25 },
    "reverb": { "enabled": true, "mix": 0.30 }
  }
}
```

Assets are referenced by ID, relative path and content hash. Factory presets
ship as a bundled JSON catalog, exactly as Hybrid 8 does with
`Extension/Resources/FactoryPresets.json`, loaded through a validating parser
that falls back to Init on malformed input.

Missing-sample handling is a first-class requirement, not an error path: a patch
referencing an unavailable asset must load, sound with a substitute or silence
on that Partial, and surface a clear warning in the browser.

---

## 19. Sample manager under the app-extension sandbox

```cpp
class SampleManager {
public:
    int  requestSlot(const std::string& assetId);   // main/loader thread
    void preloadPatchAssets(const PatchState& patch);
    void unloadUnused();
private:
    AssetDatabase   database;
    BackgroundLoader loader;
};
```

### 19.1 Loading flow

```text
Patch selected (main thread)
    -> collect asset IDs
    -> background queue: read file, decode, normalise,
       build loop crossfade, build mip pyramid if single-cycle
    -> build immutable SampleData
    -> publish pointer into slot atomically
    -> voices pick up the new sample on their next note-on
    -> retired SampleData goes on a deferred-free list,
       released on the main thread once no voice references it
```

Existing notes keep using the old sample object until they release. The render
thread never frees anything.

This is the same shape as Hybrid 8's
`installWavetableAtSlot:samples:frameLength:`, which explicitly performs offline
allocation and spectral processing off the render thread. Reuse that pattern.

### 19.2 Sandbox specifics

The appex has `app-sandbox` and `files.user-selected.read-only` only.

- **Factory samples** ship inside the appex bundle under `Resources/`, read via
  `Bundle.main`. No entitlement needed.
- **User samples** are imported through a file panel, which grants read access to
  the chosen file only. That grant does not survive a relaunch, so the file must
  be **copied into the extension's Application Support container** at import
  time, and referenced from there afterwards.
  `Extension/UI/WavetableStore.swift` already does exactly this
  (`applicationSupportDirectory` + `copyItem`); follow it rather than inventing
  security-scoped bookmarks.
- **Large factory libraries** should be memory-mapped read-only rather than
  heap-copied, since each AU instance may be a separate process (§2.4).

---

## 20. User interface architecture

A page-based SwiftUI editor inside the existing fixed-size fascia that scales
uniformly to the host window (R50 already does this via `GeometryReader` +
`scaleEffect`). The fascia grows from today's 940×520 to roughly 1100×700 to fit
the page content.

```text
+--------------------------------------------------------------+
| R50 | Preset browser  < Glass Horizon >  Save | Undo | Redo   |
+--------------------------------------------------------------+
| Browser | Patch | Tone A | Tone B | Mod | Samples | FX        |
+--------------------------------------------------------------+
|                                                              |
|                        Active page                           |
|                                                              |
+--------------------------------------------------------------+
| Macro 1 | Macro 2 | Macro 3 | Macro 4 | Out | Voices | Panic  |
+--------------------------------------------------------------+
```

Pages: Browser, Patch, Tone A, Tone B, Partial (opened from a Tone), Modulation,
Samples, Effects, Settings.

The organising principle from the source document holds and is worth preserving
literally: **the UI hierarchy mirrors the DSP hierarchy.** The user should always
know whether they are editing a source, one Partial, a two-Partial Tone, the
relationship between Tones, or the whole Patch.

Platform-specific notes:

- Waveform displays and the multi-stage envelope editor are SwiftUI `Canvas`
  views fed by downsampled peak data computed on the loader thread — never by
  reading `SampleData` from the UI thread at draw time.
- The voice/CPU readout comes from the same 30 Hz polling timer R50's
  `R50ParameterModel` already runs for the output meter.
- Musical typing must stay in the appex (`AudioUnitViewController`), for the
  reason documented there: the AUv3 editor is a remote view service and key
  events never reach the host app's monitor.
- R50's visual identity — cold steel, cyan VFD, square-shouldered controls —
  suits a digital PCM instrument and should be kept; it is already deliberately
  distinct from Hybrid 8's brass fascia.

---

## 21. Testing

`Tests/test_r50.cpp` + `scripts/test-r50.sh` already provide the offline harness
the source document asks for in §32, including the ThreadSanitizer pass. Extend
it rather than building a separate command-line renderer first.

Required new coverage:

**PCM source**
- correct octave ratio and tuning across the keyboard;
- loop wrap continuity — no discontinuity at the loop join;
- **interpolation taps across the loop boundary** (the classic bug, §5.5);
- ping-pong direction reversal;
- one-shot end behaviour, no read past the buffer;
- reverse playback;
- region selection by key and velocity, including boundaries;
- root-key and fine-tune offsets.

**WAV / `smpl` ingestion**
- unity note and 32-bit MIDI pitch fraction, including sign conversion to the
  playback correction;
- root/tuning inheritance and explicit zone override precedence;
- inclusive RIFF loop end converted exactly once to exclusive engine form;
- forward and alternating loop types; unsupported types fail safely;
- malformed/truncated chunks, invalid roots, invalid loop bounds, and multiple
  loop records (the first supported valid loop wins);
- a multi-zone instrument selects the correct sample at every key boundary,
  starts it once, and retains it for the voice lifetime.

**Mip pyramid**
- level selection matches playback increment;
- crossfade between adjacent levels is continuous;
- **aliasing bound**: render a single-cycle saw at the top of the keyboard and
  assert no inharmonic energy above a threshold — this is the test that proves
  VA removal did not cost audio quality;
- loudness consistency across level boundaries.

**Noise**
- spectral slope of each colour within tolerance;
- deterministic per-voice seeding (repeatable renders).

**Envelopes**
- exact segment durations, sample-rate independence, retrigger policy.

**Voice allocator**
- stealing does not click (bounded first-difference after a steal);
- sustain, sostenuto, repeated notes, mono priority, unison.

**Presets and state**
- save/load round trip, version migration, missing-sample handling.

**Real-time safety**
- no allocation on the render thread (assert via an overridden global
  `operator new` in a test build);
- no NaN/Inf under parameter sweeps;
- TSan clean under concurrent parameter writes (already in place).

---

## 22. Roadmap

### Phase 1 — PCM source
Replace R50's PolyBLEP oscillator with a `PcmSource`: single-cycle playback with
mip pyramid, cubic interpolation, forward loop. Existing filter, envelope,
voice allocation and UI stay. **Ship this** — it is a complete instrument and it
validates the hardest technical risk (§5.2) first.

### Phase 2 — Sample assets
Multisample regions, key/velocity mapping, one-shot transients, loop modes and
crossfades, the background sample manager, and the bundled factory set.

Multisampled WAV instruments are explicitly back in the plan. The loader,
directory format, identity mapping, and core playback path described in steps
1–4 and 6 are implemented; multi-file user import in step 5 remains:

1. Extend `LoadedWav`/`decodeWav` to retain `smpl` MIDI pitch fraction as a
   fine-tuning correction, validate unity note and loop records, and preserve
   the existing inclusive-to-exclusive loop conversion.
2. Let manifest zones inherit root, tuning, loop points and loop mode from the
   WAV when those fields are omitted. Explicit zone metadata remains the final
   authority.
3. Publish all zones as one immutable `Multisample`, sorted and validated for
   deterministic key/velocity lookup. Resolve the region once at note-on,
   start its `SamplePlayer`, and retain the sample/region for the voice.
4. Verify forward and ping-pong sustain playback, one-shot termination,
   loop-aware cubic interpolation, sample-start behavior, and tuning across
   source/output sample rates.
5. Add user-facing multi-file import and key-map review after the factory
   manifest path is proven. Deriving key ranges from adjacent roots is a UI
   convenience, not part of WAV decoding.
6. Add atomic directory ingestion: optional `instrument.json`, automatic
   midpoint zoning for unambiguous `smpl`-rooted WAV sets, deterministic
   discovery, persistent instrument IDs, and diagnostics that reject a bad
   directory without suppressing other instruments.

Steps 1–4 and 6 are complete. The packaged set contains nine directory
instruments and 79 looped zones, all covered by ingestion, full-keyboard,
playback, loop, and persistent-ID tests. Step 5 remains future UI work.

The 106-preset factory bank includes six direct multisample showcases. Another
35 existing presets now substitute the new zoned piano, choir, violin, flute,
nylon-guitar, pizzicato, and slap-bass instruments for their equivalent legacy
sources. Preset recipes store per-Partial persistent IDs and resolve them only
when applied; runtime DSP continues to operate entirely on integer slots.

### Phase 3 — Partial engine
Multi-stage envelopes, per-Partial filter and pan, noise source, waveshaper, two
Partials per Tone with Mix / RingMod / AttackSustain / crossfade structures.

### Phase 4 — Patch engine
Complete at the product's deliberate eight-voice polyphony: two Tones, four
Partials, Layer, Key Split, Velocity Split, Velocity Crossfade, and Vector Mix.
Vector Mix is both a Patch-level matrix destination and the target of a
dedicated Vector LFO. The planned unison expansion remains outside the
implemented Patch layer.

### Phase 5 — Modulation
Modulation matrix, four macros, LFO tempo sync (requires restoring the
`AUHostMusicalContextBlock` path), parameter smoothing.

### Phase 6 — Effects
Complete as a substantially expanded three-slot global rack: four named
routing topologies, independent sends per Partial, 16 selectable algorithms
covering modulation, delay, space, EQ, nonlinear color and exciter, followed by
the global compressor. Nonlinear slots and the per-Partial waveshaper use 4x
oversampling. See `FX-updated.plan` for the completed implementation record.

### Phase 7 — UI
Page-based editor, browser, sample page with loop editor, envelope editor,
modulation matrix, macro assignment.

### Phase 8 — Production
Tier-2 patch state and `fullState`, preset migration, asset hashing, missing
sample resolution, memory-mapped factory library, SIMD, profiling.

### 22.1 Version-one boundary

**Ship v1 with:** 4 Partials; PCM and noise sources; 2 Tones; multisamples;
forward and ping-pong loops; linear and cubic interpolation; single-cycle waves
with mip band-limiting; digital and resonant multimode filters; multi-stage
pitch/filter/amp envelopes; two LFOs per Tone; modulation matrix; four macros;
chorus, delay and reverb; AU and standalone; versioned presets; background
sample loading.

**Not in v1:** VST3; MPE; granular synthesis; additive resynthesis; sample
recording; scripting; FM networks; deep sample editing; cloud or account
services.

### 22.2 First implementation slice

```text
Single-cycle PCM wave (mip band-limited)
    -> existing ZDF filter
    -> existing amp envelope
    -> existing voice allocator
    -> existing AU adapter and UI
```

Deliberately narrower than the source document's slice, because on this platform
most of the surrounding machinery already exists and works. The one genuinely
new and genuinely risky thing is band-limited PCM playback. Build that, prove it
with the aliasing test in §21, and the rest is incremental.

The second slice adds the looped multisample and the attack/sustain structure —
at which point the instrument's identity is fully established.

---

## 23. Summary of changes from the source document

| Area | Source document | This document |
|---|---|---|
| Source types | PCM, VirtualAnalog, Noise | **PCM, Noise** |
| Synthetic waveforms | PolyBLEP VA oscillators | Single-cycle PCM + **mip pyramid** |
| Anti-aliasing | PolyBLEP | Mip pyramid with per-octave crossfade |
| `PcmAttackVaSustain` | PCM attack + VA sustain | `AttackSustain` — PCM + PCM/noise |
| `OscillatorSync` | VA hard sync | `PhaseSync` — single-cycle sources only |
| Supersaw | VA multi-saw | Unison detune across PCM voices |
| PWM / VA shape mod | `VaPulseWidth`, `VaShape` | `WaveIndex` table morphing |
| Noise | VA sub-parameter | First-class source with spectra and modes |
| Plugin formats | VST3 + AU + standalone | **AU + standalone container app** |
| MPE | v1 feature | Deferred — needs the MIDI 2.0 UMP path |
| Parameters | "expose meaningful ones" | **Two-tier: ≤100 AU params + patch state** |
| `std::vector` in params | Used throughout | **Fixed-size arrays everywhere** |
| Sample loading | Background loader | Same, plus **appex sandbox rules** |
| Test harness | Build a CLI renderer first | **Extend the existing suite + TSan** |
| Effects | Build chorus/delay/reverb | Copy the working rack from Hybrid 8 |
| First slice | PCM attack + VA saw sustain | Single-cycle PCM through existing chain |

The hierarchy, the tone/patch structures, the modulation architecture, the
preset philosophy and the UI-mirrors-DSP principle are all preserved. What
changed is the source layer, the anti-aliasing strategy it forces, and the
platform realities of shipping this as a sandboxed AUv3 in this repository.
