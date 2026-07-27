# Hybrid PCM / Virtual-Analog Soft Synth Architecture

## 1. Product goal

Build a polyphonic software synthesizer inspired by the architectural ideas of late-1980s and early-1990s instruments such as the Korg M1, Roland D-50, and Korg Wavestation, without copying their ROM data, presets, or proprietary implementation details.

The instrument should combine:

- sampled attack transients;
- looped multisampled sustaining instruments;
- single-cycle PCM waves;
- band-limited virtual-analog oscillators;
- four Partials per Patch;
- two-Partials-per-Tone structures;
- two Tones per Patch;
- ring modulation, layering, velocity splits, and serial routing;
- flexible envelopes, LFOs, and modulation;
- chorus, delay, and reverb;
- Audio Unit, and standalone targets.

The central design rule is:

```text
Partial = sound source + pitch + tone shaping + amplitude

2 Partials = Tone
2 Tones = Patch
Patch = effects + performance controls + preset state
```

---

# 2. Top-level architecture

```text
Host / Standalone App
        |
        v
Plugin Wrapper
        |
        v
Parameter and MIDI Layer
        |
        v
Synth Engine
        |
        +-- Voice Allocator
        |      |
        |      +-- Voice 1
        |      +-- Voice 2
        |      +-- ...
        |
        +-- Global Modulation
        +-- Global Effects
        +-- Preset State
        +-- Sample Asset Manager
```

Recommended code separation:

```text
synth-core/
  Framework-independent DSP engine

plugin-wrapper/
  VST3 / AU / standalone integration

editor/
  GUI, patch editing, sample browsing

assets/
  Factory sample metadata and licensed content

tests/
  Offline rendering, tuning, stability, and preset tests
```

The DSP engine must not depend on JUCE, Steinberg VST classes, or Apple Audio Unit classes.

---

# 3. Sound hierarchy

## 3.1 Partial

A Partial is the smallest complete sound-generating unit.

```text
PCM or VA oscillator
    -> pitch stage
    -> optional transient/sustain blend
    -> waveshaper
    -> filter
    -> amplifier
    -> pan
```

Each Partial can use one of these source modes:

```cpp
enum class SourceType {
    PCM,
    VirtualAnalog,
    Noise
};
```

Recommended Partial parameters:

```cpp
struct PartialParameters {
    bool enabled = true;
    SourceType sourceType = SourceType::PCM;

    PcmSourceParameters pcm;
    VaSourceParameters va;
    NoiseSourceParameters noise;

    PitchParameters pitch;
    WaveshaperParameters waveshaper;
    FilterParameters filter;
    AmplifierParameters amp;
    PanParameters pan;

    EnvelopeParameters pitchEnvelope;
    EnvelopeParameters filterEnvelope;
    EnvelopeParameters ampEnvelope;

    LfoParameters lfo1;
    LfoParameters lfo2;

    std::vector<ModulationRoute> modulationRoutes;
};
```

## 3.2 Tone

A Tone contains two Partials and one structure algorithm.

```text
Tone A
  Partial 1
  Partial 2
  Structure

Tone B
  Partial 3
  Partial 4
  Structure
```

Recommended Tone structures:

```cpp
enum class ToneStructure {
    Mix,
    RingMod,
    RingModWithDry,
    VelocityCrossfade,
    KeyCrossfade,
    SerialFilter,
    ParallelFilter,
    OscillatorSync,
    PcmAttackVaSustain
};
```

## 3.3 Patch

A Patch contains two Tones.

```cpp
enum class PatchStructure {
    Layer,
    KeySplit,
    VelocitySplit,
    VelocityCrossfade,
    VectorMix,
    SerialToneProcessing
};
```

Patch-level state:

```cpp
struct PatchParameters {
    ToneParameters toneA;
    ToneParameters toneB;

    PatchStructure patchStructure;
    VoiceMode voiceMode;
    PerformanceParameters performance;
    GlobalEffectsParameters effects;
    MasterParameters master;
};
```

---

# 4. Per-voice DSP graph

Each active note owns one complete voice.

```text
MIDI note
   |
   v
Voice state
   |
   +--> Tone A
   |      +--> Partial A1
   |      +--> Partial A2
   |      +--> Tone structure
   |
   +--> Tone B
          +--> Partial B1
          +--> Partial B2
          +--> Tone structure
   |
   v
Patch structure
   |
   v
Per-voice pan / gain
   |
   v
Voice mix bus
```

Maximum active DSP load is based on Partials, not notes.

Example:

```text
32-note polyphony × 4 Partials = 128 active Partials
```

A performance budget should be calculated for the worst case:

- all four Partials enabled;
- all filters active;
- all envelopes active;
- modulation matrix active;
- both Tone structures active;
- maximum unison;
- effects enabled.

---

# 5. PCM source architecture

## 5.1 PCM source types

Support three categories:

1. **Single-cycle waves**
2. **One-shot transients**
3. **Looped multisamples**

Example use:

```text
Partial A1 = short breath attack sample
Partial A2 = looped flute sustain sample

Partial B1 = virtual-analog saw
Partial B2 = noise layer
```

## 5.2 Sample asset model

```cpp
struct SampleData {
    std::vector<float> left;
    std::vector<float> right;

    double sourceSampleRate = 44100.0;
    int rootKey = 60;

    uint32_t loopStart = 0;
    uint32_t loopEnd = 0;

    LoopMode loopMode = LoopMode::None;

    std::string assetId;
    uint64_t contentHash = 0;
};
```

## 5.3 Multisample regions

```cpp
struct SampleRegion {
    int lowKey = 0;
    int highKey = 127;

    int lowVelocity = 1;
    int highVelocity = 127;

    int rootKey = 60;
    float tuneCents = 0.0f;
    float gainDb = 0.0f;

    uint32_t sampleStart = 0;
    uint32_t sampleEnd = 0;

    uint32_t loopStart = 0;
    uint32_t loopEnd = 0;

    LoopMode loopMode = LoopMode::Forward;

    std::string sampleAssetId;
};
```

```cpp
struct Multisample {
    std::string id;
    std::string name;
    std::vector<SampleRegion> regions;
};
```

## 5.4 Recommended starter sample set

A useful first factory set should be small but broad.

### Attack transients

- piano hammer;
- tine attack;
- mallet;
- pick;
- slap;
- breath;
- chiff;
- bowed scrape;
- brass tongue;
- metallic strike;
- noise burst;
- reverse transient.

### Looped multisamples

- piano;
- electric piano;
- organ;
- flute;
- clarinet;
- cello;
- string ensemble;
- choir;
- brass section;
- bass guitar;
- synth pad;
- bell sustain.

### Single-cycle waves

- sine;
- triangle;
- saw;
- square;
- pulse 25%;
- pulse 12.5%;
- organ waves;
- additive spectra;
- metallic spectra;
- vocal/formant waves;
- noisy waves;
- digital stepped waves.

For a first version, roughly 50–100 source assets are enough.

## 5.5 Loop lengths

For sampled sustained instruments, use multi-cycle loops.

Practical starting ranges:

```text
Single-cycle synthetic waves:
  one period

Short stable loop:
  30–100 ms

Typical orchestral sustain loop:
  100–400 ms

Complex choir/string loop:
  250–1000 ms
```

The loop should be long enough to avoid obvious repetition but short enough to conserve memory.

Support:

- forward loops;
- ping-pong loops;
- loop crossfades;
- separate release samples;
- one-shot attacks feeding looped sustains.

## 5.6 Playback position

Use double precision or fixed-point.

```cpp
struct PlaybackState {
    double position = 0.0;
    double increment = 1.0;
    int direction = 1;
};
```

Pitch ratio:

```cpp
double semitones =
    midiNote - region.rootKey
    + region.tuneCents / 100.0
    + pitchModulationSemitones;

double ratio = std::exp2(semitones / 12.0);

state.increment =
    ratio
    * sample.sourceSampleRate
    / outputSampleRate;
```

## 5.7 Interpolation

Provide selectable quality:

```cpp
enum class InterpolationMode {
    Nearest,
    Linear,
    Cubic,
    Sinc
};
```

Recommended defaults:

```text
Draft / historical:
  Linear

Normal:
  Cubic

High quality:
  Polyphase sinc
```

Linear interpolation:

```cpp
float interpolateLinear(
    const float* data,
    int i0,
    int i1,
    float fraction)
{
    return data[i0]
         + fraction * (data[i1] - data[i0]);
}
```

Loop-aware interpolation must wrap every interpolation tap through the loop.

---

# 6. Virtual-analog source

## 6.1 Core waveform set

```cpp
enum class VaWaveform {
    Sine,
    Triangle,
    Saw,
    Pulse,
    Noise,
    Supersaw
};
```

Initial implementation:

- sine: table or analytic;
- saw: PolyBLEP;
- pulse: PolyBLEP;
- triangle: integrated band-limited square;
- noise: high-quality pseudo-random generator;
- supersaw: multiple detuned saw voices.

## 6.2 VA source parameters

```cpp
struct VaSourceParameters {
    VaWaveform waveform = VaWaveform::Saw;

    int octave = 0;
    int semitone = 0;
    float fineCents = 0.0f;

    float pulseWidth = 0.5f;
    float pulseWidthModAmount = 0.0f;

    float phaseOffset = 0.0f;
    bool phaseReset = true;

    float drift = 0.0f;
    float subOscLevel = 0.0f;
    float noiseLevel = 0.0f;

    int unisonVoices = 1;
    float unisonDetuneCents = 0.0f;
    float stereoSpread = 0.0f;
};
```

## 6.3 PCM attack plus VA sustain

This is one of the most important structures.

```text
Attack PCM
    \
     -> crossfade envelope -> Tone output
    /
VA sustain
```

The attack Partial fades quickly while the sustain Partial takes over.

```cpp
float attackGain = attackEnvelope.next();
float sustainGain = sustainEnvelope.next();

float output =
    attackGain * pcmPartial
  + sustainGain * vaPartial;
```

---

# 7. Tone structures

## 7.1 Mix

```cpp
toneOutput =
    partial1Level * p1
  + partial2Level * p2;
```

## 7.2 Ring modulation

```cpp
float ring = p1 * p2;

toneOutput =
    dry1 * p1
  + dry2 * p2
  + ringLevel * ring;
```

Provide optional oversampling for ring modulation.

## 7.3 Velocity crossfade

```cpp
float x = velocityCurve(velocity);

toneOutput =
    equalPowerA(x) * p1
  + equalPowerB(x) * p2;
```

## 7.4 Key crossfade

```cpp
float x =
    smoothStep(
        key,
        crossfadeLow,
        crossfadeHigh);

toneOutput =
    equalPowerA(x) * p1
  + equalPowerB(x) * p2;
```

## 7.5 Serial filter

```text
Partial 1 -> Filter 1 -> Partial 2 input or mixer -> Filter 2
```

A simpler version:

```cpp
float mixed = p1 + p2;
float stage1 = filter1.process(mixed);
float output = filter2.process(stage1);
```

## 7.6 Oscillator sync

Available when both Partials use compatible VA oscillators.

```cpp
if (master.wrapped())
    slave.resetPhase();
```

## 7.7 Transient/sustain structure

```cpp
toneOutput =
    transientEnvelope * pcmTransient
  + sustainEnvelope * sustainSource;
```

---

# 8. Filter architecture

Each Partial has its own filter.

Recommended types:

```cpp
enum class FilterType {
    Off,
    LowPass12,
    LowPass24,
    HighPass12,
    BandPass12,
    Notch,
    M1DigitalLowPass,
    LadderLowPass
};
```

## 8.1 M1-style digital filter

Use a simple non-resonant one- or two-pole low-pass.

```cpp
s1 += g * (input - s1);
s2 += g * (s1 - s2);

return s2;
```

## 8.2 Resonant multimode filter

A topology-preserving state-variable filter is a good general choice.

Outputs:

- low-pass;
- band-pass;
- high-pass;
- notch.

## 8.3 Analog-style ladder

Add later as an optional heavier model.

Use:

- nonlinear input drive;
- resonance feedback;
- oversampling;
- stable zero-delay feedback or iterative solution.

## 8.4 Filter modulation

Cutoff sources:

- filter envelope;
- key tracking;
- velocity;
- aftertouch;
- LFO;
- modulation matrix;
- macro controls.

Calculate cutoff in semitone space:

```cpp
float cutoffSemitones =
    baseCutoff
    + keyTrackAmount * (note - 60)
    + envelopeAmount * filterEnvelope
    + modulationAmount;

float cutoffHz =
    referenceHz
    * std::exp2(cutoffSemitones / 12.0f);
```

---

# 9. Waveshaping

Every Partial may optionally include a waveshaper before or after the filter.

```cpp
enum class WaveshaperType {
    Off,
    SoftClip,
    HardClip,
    Fold,
    Rectify,
    Polynomial,
    Table
};
```

Suggested routing options:

```cpp
enum class WaveshaperPosition {
    PreFilter,
    PostFilter
};
```

Table-based shaping:

```cpp
float Waveshaper::process(float x)
{
    float u =
        std::clamp(
            0.5f * (x + 1.0f),
            0.0f,
            1.0f);

    float position =
        u * static_cast<float>(table.size() - 1);

    int i0 = static_cast<int>(position);
    int i1 = std::min(
        i0 + 1,
        static_cast<int>(table.size() - 1));

    float fraction = position - i0;

    return table[i0]
         + fraction * (table[i1] - table[i0]);
}
```

Oversample strong nonlinear modes.

---

# 10. Envelopes

Each Partial has:

- pitch envelope;
- filter envelope;
- amplitude envelope.

Use a multi-stage envelope rather than only ADSR.

```cpp
enum class EnvelopeCurve {
    Linear,
    Exponential,
    Logarithmic,
    SShape,
    Step
};

struct EnvelopePoint {
    float level;
    float timeSeconds;
    EnvelopeCurve curve;
};

struct EnvelopeParameters {
    std::vector<EnvelopePoint> attackDecayPoints;
    int sustainPoint = -1;
    std::vector<EnvelopePoint> releasePoints;

    bool velocityToTime = false;
    bool velocityToLevel = false;
    bool keyTrackTime = false;
};
```

A practical maximum:

```text
4 stages before sustain
1 sustain point
3 release stages
```

This is enough for evolving LA-style and workstation-style patches.

---

# 11. LFOs

Each Tone gets two LFOs shared by its Partials.

```cpp
enum class LfoWaveform {
    Sine,
    Triangle,
    SawUp,
    SawDown,
    Square,
    SampleHold,
    SmoothRandom
};
```

Parameters:

```cpp
struct LfoParameters {
    LfoWaveform waveform;
    float rateHz;
    bool tempoSync;
    float tempoDivision;
    bool retrigger;
    float phase;
    float delaySeconds;
    float fadeSeconds;
    bool unipolar;
};
```

Optionally allow one additional global LFO.

---

# 12. Modulation matrix

## 12.1 Sources

```cpp
enum class ModSource {
    Velocity,
    ReleaseVelocity,
    KeyTrack,
    PitchBend,
    ModWheel,
    ChannelPressure,
    PolyPressure,
    MpePressure,
    MpeTimbre,
    Lfo1,
    Lfo2,
    PitchEnvelope,
    FilterEnvelope,
    AmpEnvelope,
    RandomPerVoice,
    NoteAge,
    Macro1,
    Macro2,
    Macro3,
    Macro4
};
```

## 12.2 Destinations

```cpp
enum class ModDestination {
    Pitch,
    FinePitch,
    PcmSampleStart,
    PcmLoopPosition,
    VaPulseWidth,
    VaShape,
    OscillatorLevel,
    WaveshaperDrive,
    FilterCutoff,
    FilterResonance,
    AmpLevel,
    Pan,
    ToneBalance,
    PatchBalance,
    EffectSend1,
    EffectSend2
};
```

## 12.3 Route

```cpp
struct ModulationRoute {
    ModSource source;
    ModDestination destination;

    float amount;
    ModCurve curve;

    bool bipolar;
    bool audioRate;
};
```

## 12.4 Processing rates

Audio-rate:

- oscillator pitch;
- FM-like modulation;
- pulse width;
- hard sync;
- ring-mod-related modulation;
- filter cutoff when needed.

Control-rate:

- envelopes;
- slow LFO;
- pan;
- effects sends;
- most MIDI controller changes.

---

# 13. Voice allocation

```cpp
enum class VoiceMode {
    Poly,
    Mono,
    Legato,
    Unison
};
```

Voice allocator priorities:

1. free voice;
2. quiet released voice;
3. oldest released voice;
4. quietest active voice;
5. oldest active voice.

```cpp
SynthVoice* VoiceAllocator::allocate()
{
    if (auto* free = findFreeVoice())
        return free;

    if (auto* released = findQuietestReleased())
        return released;

    return findOldestQuietestActive();
}
```

When stealing:

- fade old voice for 1–5 ms;
- start new voice on a second temporary voice slot if possible;
- avoid hard reset clicks.

Support:

- sustain pedal;
- sostenuto;
- repeated-note policy;
- note IDs;
- MPE per-note controls;
- mono note priority;
- glide.

---

# 14. Effects architecture

## 14.1 Per-voice effects

Only effects that belong to sound generation should be per voice:

- ring modulation;
- waveshaping;
- filter;
- pan;
- partial-specific drive.

## 14.2 Patch effects

```text
Voice sum
    -> Insert 1
    -> Insert 2
    -> Chorus send
    -> Delay send
    -> Reverb send
    -> Master EQ
    -> Output
```

Recommended effects for version one:

```cpp
enum class EffectType {
    Off,
    Chorus,
    Ensemble,
    Phaser,
    Flanger,
    StereoDelay,
    Reverb,
    Overdrive,
    ParametricEq
};
```

Effects are important to the target sound. Many classic workstation patches rely on processing as much as source material.

---

# 15. Plugin engine interface

Framework-independent engine:

```cpp
class SynthEngine {
public:
    void prepare(
        double sampleRate,
        int maximumBlockSize,
        int outputChannels);

    void reset();

    void process(
        const MidiEvent* events,
        size_t eventCount,
        float** outputs,
        int outputChannels,
        int sampleCount);

    void setParameter(
        ParameterId id,
        float normalizedValue);

    PatchState getState() const;
    void setState(const PatchState& state);

private:
    VoiceAllocator allocator;
    std::vector<SynthVoice> voices;
    GlobalEffectRack effects;
    SampleManager sampleManager;
};
```

The wrapper handles:

- VST3;
- Audio Unit;
- standalone;
- host automation;
- state serialization;
- MIDI event conversion;
- bus configuration;
- editor lifecycle.

---

# 16. Real-time safety

The audio thread must never:

- allocate memory;
- free large objects;
- lock a mutex;
- access the filesystem;
- parse JSON;
- decode samples;
- refresh the GUI;
- log synchronously.

Use:

- preallocated voice storage;
- lock-free event queues;
- immutable sample objects;
- deferred deletion;
- background sample loading;
- atomic parameter snapshots;
- block-level modulation caches.

---

# 17. Parameter system

Use stable parameter IDs.

```cpp
enum class ParameterId : uint32_t {
    PartialA1SourceType,
    PartialA1OscWave,
    PartialA1Cutoff,
    PartialA1Resonance,
    PartialA1AmpAttack,
    PartialA1AmpRelease,

    ToneAStructure,
    ToneALevel,

    PatchStructure,
    PatchToneBalance,

    ChorusMix,
    DelayMix,
    ReverbMix,

    Macro1,
    Macro2,
    Macro3,
    Macro4
};
```

Do not use array positions as persistent IDs.

Expose only meaningful automatable parameters to the host.

Good host parameters:

- macros;
- cutoff;
- resonance;
- oscillator mix;
- effects mix;
- patch balance;
- envelope macros;
- performance controls.

Keep deep multisample editing and every envelope point as internal patch state unless automation is specifically required.

---

# 18. Preset format

Use a versioned format.

```json
{
  "format": "HybridPartialPatch",
  "version": 1,
  "name": "Glass Horizon",
  "author": "Factory",
  "category": "Pad",
  "patchStructure": "Layer",
  "tones": [
    {
      "structure": "PcmAttackVaSustain",
      "partials": [
        {
          "sourceType": "PCM",
          "multisampleId": "breath_attack_01"
        },
        {
          "sourceType": "VirtualAnalog",
          "waveform": "Saw"
        }
      ]
    }
  ],
  "effects": {
    "chorus": {
      "enabled": true,
      "mix": 0.25
    },
    "reverb": {
      "enabled": true,
      "mix": 0.30
    }
  }
}
```

Preset state should reference sample assets by:

- asset ID;
- relative path;
- content hash.

Do not embed large sample files inside ordinary plugin state.

---

# 19. Sample manager

```cpp
class SampleManager {
public:
    SampleHandle request(
        const std::string& assetId);

    void preloadPatchAssets(
        const PatchState& patch);

    void unloadUnused();

private:
    AssetDatabase database;
    BackgroundLoader loader;
};
```

The audio thread uses stable read-only handles.

Sample loading flow:

```text
Patch selected
    -> collect asset IDs
    -> background loader reads files
    -> decode and normalize
    -> build immutable SampleData
    -> publish handle atomically
    -> voices use new sample on next note
```

Existing notes should generally keep using the old sample object until release.

---

# 20. User interface architecture

Use a page-based editor.

```text
1. Browser
2. Patch
3. Tone A
4. Tone B
5. Partial Editor
6. Modulation
7. Samples
8. Effects
9. Settings
```

---

# 21. Global UI layout

Recommended desktop layout:

```text
+--------------------------------------------------------------+
| Preset Browser | Patch Name | Save | Undo | Redo | Settings |
+--------------------------------------------------------------+
| Main Page Tabs                                               |
| Browser | Patch | Tone A | Tone B | Mod | Samples | FX       |
+--------------------------------------------------------------+
|                                                              |
|                    Active Page                               |
|                                                              |
+--------------------------------------------------------------+
| Macro 1 | Macro 2 | Macro 3 | Macro 4 | Output | CPU | Voices|
+--------------------------------------------------------------+
```

Always-visible controls:

- patch name;
- previous/next preset;
- undo/redo;
- four macros;
- master volume;
- voice count;
- CPU indicator;
- panic button.

---

# 22. Browser page

```text
+-------------------+----------------------+-------------------+
| Categories        | Preset List          | Preset Information|
|                   |                      |                   |
| Bass              | Glass Horizon       | Author            |
| Bell              | Warm Breath Pad     | Tags              |
| Brass             | Digital Choir       | Description       |
| Keys              | ...                  | Required samples  |
+-------------------+----------------------+-------------------+
```

Features:

- search;
- category filtering;
- favorites;
- tags;
- author;
- sample dependency warning;
- initialize patch;
- compare;
- save as.

---

# 23. Patch page

Purpose: edit the relationship between Tone A and Tone B.

```text
+--------------------------------------------------------------+
| Patch Structure                                              |
| [Layer] [Split] [Velocity] [Crossfade] [Vector]              |
+--------------------------------------------------------------+
| Tone A                           Tone B                       |
| Level                            Level                        |
| Pan                              Pan                          |
| Key Range                        Key Range                    |
| Velocity Range                   Velocity Range               |
| Transpose                        Transpose                    |
+--------------------------------------------------------------+
| Tone Balance / Vector Controller                              |
|                                                              |
|                    XY PAD                                    |
|                                                              |
+--------------------------------------------------------------+
```

Patch page controls:

- patch structure;
- Tone enable;
- Tone level;
- Tone pan;
- key zones;
- velocity zones;
- split point;
- crossfade width;
- transpose;
- vector X/Y;
- unison;
- mono/poly;
- glide.

---

# 24. Tone page

Each Tone page shows two Partials and their structure.

```text
+--------------------------------------------------------------+
| Tone A Structure: [Mix] [Ring] [Crossfade] [Attack/Sustain]  |
+--------------------------------------------------------------+
| Partial 1                         Partial 2                   |
| Source                            Source                      |
| PCM / VA                          PCM / VA                    |
| Wave / Multisample                Wave / Multisample          |
| Level                             Level                       |
| Tune                              Tune                        |
| Pan                               Pan                         |
+--------------------------------------------------------------+
| Structure Parameters                                         |
| Ring Level | Crossfade | Sync | Serial Routing               |
+--------------------------------------------------------------+
```

Clicking a Partial opens the detailed Partial page.

---

# 25. Partial page

Recommended sections:

```text
+--------------------------------------------------------------+
| Source | Pitch | Filter | Amp | Envelopes | LFO | Mod        |
+--------------------------------------------------------------+
```

## 25.1 PCM source panel

```text
Multisample browser
Waveform display
Start position
End position
Loop start
Loop end
Loop mode
Crossfade
Root key
Fine tune
Interpolation quality
Reverse
```

## 25.2 VA source panel

```text
Waveform
Octave
Semitone
Fine tune
Pulse width
PWM
Phase reset
Drift
Sub oscillator
Noise
Unison
Detune
Stereo spread
```

## 25.3 Filter panel

```text
Type
Cutoff
Resonance
Drive
Key tracking
Envelope amount
Velocity amount
```

## 25.4 Amplifier panel

```text
Level
Velocity sensitivity
Pan
Pan key tracking
Random pan
```

---

# 26. Envelope editor

Use a graphical multi-stage editor.

```text
Level
 1.0 |          o------ sustain
     |        /
     |   o---o
     |  /
 0.0 o---------------------------- time
          attack       release
```

Editing:

- drag points;
- drag segment times;
- choose curve per segment;
- numeric entry;
- zoom;
- velocity-to-time;
- key-track time;
- loop envelope option later.

Display:

- current voice animation;
- sustain point;
- release start;
- segment curve.

---

# 27. Modulation page

Use a matrix table.

```text
+--------------------------------------------------------------+
| Source      | Destination       | Amount | Curve | Rate       |
+--------------------------------------------------------------+
| LFO 1       | Filter Cutoff     | +32    | Linear| Control    |
| Velocity    | Amp Level         | +80    | Exp   | Note       |
| Macro 1     | Tone Balance      | +100   | Linear| Control    |
| MPE Timbre  | Pulse Width       | +40    | S     | Audio      |
+--------------------------------------------------------------+
```

Features:

- drag source onto destination;
- route enable;
- bipolar/unipolar;
- curve;
- smoothing;
- audio/control-rate indicator;
- per-Tone or per-Partial scope;
- route depth visualization.

---

# 28. Sample page

The sample page should initially be a browser and loop editor, not a full sampler workstation.

```text
+-------------------+------------------------------------------+
| Sample Browser    | Waveform                                 |
|                   |                                          |
| Piano             |  Start   Loop Start   Loop End    End    |
| Flute              |   |          |           |        |     |
| Cello              |---|----------|===========|--------|-----|
| Attacks            |                                          |
+-------------------+------------------------------------------+
| Root Key | Tune | Loop Mode | Crossfade | Audition | Replace |
+--------------------------------------------------------------+
```

Version-one editing:

- choose sample;
- set root key;
- start/end;
- loop start/end;
- loop crossfade;
- normalize;
- audition;
- save metadata.

Later:

- automatic loop search;
- region mapping;
- velocity layers;
- batch import;
- sample recording.

---

# 29. Effects page

```text
+--------------------------------------------------------------+
| Insert 1          Insert 2                                   |
| [Chorus]          [Overdrive]                                |
| Parameters        Parameters                                 |
+--------------------------------------------------------------+
| Send Effects                                                 |
| Delay Send       Reverb Send                                 |
| Delay Parameters Reverb Parameters                           |
+--------------------------------------------------------------+
| Master EQ | Limiter | Output                                 |
+--------------------------------------------------------------+
```

Show routing clearly.

---

# 30. Macro system

Always expose four macro controls.

```cpp
struct MacroAssignment {
    ParameterId destination;
    float minimum;
    float maximum;
    ModCurve curve;
};
```

Examples:

```text
Macro 1: Brightness
  filter cutoff
  PCM/VA balance
  waveshaper drive

Macro 2: Motion
  LFO depth
  vector movement
  chorus depth

Macro 3: Attack
  PCM attack level
  amp attack
  filter attack

Macro 4: Space
  delay send
  reverb send
  stereo width
```

Macros are the best host-automation interface.

---

# 31. Threading model

```text
Audio thread:
  process MIDI
  render voices
  render effects
  read atomic parameter values

Message/UI thread:
  edit parameters
  edit presets
  update display
  request sample loads

Background worker:
  load files
  decode audio
  calculate waveforms
  build sample objects
  scan preset library
```

Communication:

- atomic values for simple parameters;
- lock-free FIFO for events;
- immutable state snapshots;
- deferred object destruction.

---

# 32. Offline test harness

Build a standalone command-line renderer before relying on the plugin.

```cpp
int main()
{
    SynthEngine engine;
    engine.prepare(48000.0, 512, 2);

    PatchState patch =
        loadPatch("factory/glass_horizon.json");

    engine.setState(patch);

    renderMidiFile(
        engine,
        "test.mid",
        "output.wav");
}
```

Use it for:

- regression tests;
- CPU profiling;
- preset rendering;
- tuning tests;
- envelope tests;
- loop tests;
- deterministic output.

---

# 33. Automated tests

## Oscillator

- correct octave ratio;
- no phase drift;
- loop wrap;
- reverse playback;
- interpolation at boundaries;
- sample end behavior.

## Filter

- stable at all cutoff/resonance settings;
- no NaN;
- expected response;
- smooth parameter change.

## Envelope

- exact segment duration;
- sustain;
- release;
- retrigger policy;
- sample-rate independence.

## Voice allocator

- sustain pedal;
- repeated notes;
- note stealing;
- mono priority;
- unison.

## Presets

- save/load round trip;
- version migration;
- missing sample handling;
- stable parameter IDs.

## Real-time safety

- no audio-thread allocation;
- no locks;
- bounded CPU;
- no denormals.

---

# 34. Development roadmap

Phases 1–7 are built, bar the exceptions noted under each. Phase 8 has barely
been started. Where the shipped instrument
departs from what a phase asked for, the departure is recorded rather than the
plan quietly restated — the roadmap is only useful if it can be trusted to say
what is missing.

## Phase 1 — Minimal synth

- plugin-independent engine;
- MIDI note handling;
- one VA saw oscillator;
- amp envelope;
- stereo output;
- 16 voices.

Built, at **8 voices** rather than 16. Deliberate: R50 is a monotimbral
eight-voice instrument and the fascia says so.

## Phase 2 — Partial engine

- PCM oscillator;
- sample regions;
- loop playback;
- interpolation;
- filter;
- pitch/filter/amp envelopes;
- LFO.

Built. Interpolation is Catmull-Rom cubic; loops are forward and ping-pong,
both reachable from the factory manifest's `loopMode`.

**Multisamples were removed after the fact.** Every instrument is now one
sample stretched across the whole keyboard, which is what the D-50 did — it has
no per-Partial key range at all, and its waves break up at the extremes as part
of the sound rather than in spite of it. `SampleRegion` keeps its key range and
the loader still reads multi-zone manifests, so the machinery is intact and a
zoned instrument would still work; nothing in the factory set uses it.

The compensating control is the Partial's **Key Follow**
(`R50FieldPitchKeyFollow`), which scales how far the key's distance from middle
C reaches the pitch. It is the D-50's own answer to the same problem: rather
than restricting where a sample sounds, it asks the extremes to travel less
far.

## Phase 3 — Tone engine

- two Partials;
- Mix;
- RingMod;
- Attack/Sustain;
- velocity crossfade.

Built, with five structures rather than four: key crossfade was added beside
velocity crossfade.

## Phase 4 — Patch engine

- two Tones;
- layer;
- split;
- velocity split;
- vector mix;
- 32 voices.

Built, at the product's deliberate **8 voices** rather than the source
document's 32. Every voice owns four Partials arranged as two independent
Tones. The Patch layer provides Layer, Key Split, Velocity Split, Velocity
Crossfade, and Vector Mix, with independent Tone levels. Tone B has its own
complete structure controls, and Partials 3–4 have the same source, envelope,
filter, waveshaper, pan, dry, and send controls as Partials 1–2. Old presets
remain Tone-A-only because both new Partials default off. Vector Mix is a
Patch-level modulation-matrix destination and also has a dedicated
wave/rate/depth/retrigger/phase LFO, so movement above the Tone hierarchy does
not have to be constructed from Partial modulation.

## Phase 5 — Modulation

- modulation matrix;
- macros;
- MPE;
- host automation;
- smoothing.

Built bar **MPE**. Six matrix slots, four macros, host automation through the
AU parameter tree, and per-block smoothing.

## Phase 6 — Effects

- three interchangeable global slots;
- four named serial/parallel routing topologies;
- independent dry and three-slot sends per Partial;
- modulation, delay, space, EQ, nonlinear, and exciter algorithms;
- post-rack compressor.

Built. The original chorus / stereo-delay / reverb / master-EQ target grew into
the three-slot rack specified in `FX-updated.plan`: every slot can host any of
16 algorithms, from chorus, rotary, delay, and four space characters through
EQ, overdrive, distortion, and exciter. Nonlinear slots and the per-Partial
waveshaper share a 4x oversampling implementation. Slot state, routing, sends,
presets, automation, UI, hover help, tail handling, and click-free transitions
are all covered by the R50 regression suite.

## Phase 7 — Plugin and UI

- VST3;
- Audio Unit;
- standalone;
- browser;
- Patch page;
- Tone page;
- Partial page;
- modulation matrix;
- sample browser.

Built bar **VST3** — Audio Unit and standalone only. The pages are Partial,
Envelopes, Tone, Patch, Mod, FX and Samples.

## Phase 8 — Production quality

- background sample loading;
- preset migration;
- asset hashing;
- missing sample resolution;
- SIMD;
- oversampling;
- profiling;
- crash recovery;
- installer and signing.

**Barely started.** Imports decode on a background queue, but the factory set
loads synchronously the first time `SampleLibrary::shared()` is touched. None
of preset migration, asset hashing, missing-sample resolution, SIMD or signing
exists yet. Targeted 4x oversampling is now built for the nonlinear effects and
per-Partial waveshaper, but there is no engine-wide oversampling mode.

---

# 35. Version-one feature boundary

Ship version one with:

- 4 Partials;
- PCM, VA, and noise sources;
- 2 Tones;
- 1 Patch;
- Mix, RingMod, Attack/Sustain, and Crossfade structures;
- ~~multisamples~~ — dropped, see phase 2;
- forward and ping-pong loops;
- linear and cubic interpolation;
- saw, pulse, triangle, sine, and noise;
- multimode resonant filter;
- M1-style non-resonant filter;
- pitch/filter/amp envelopes;
- two LFOs per Tone;
- modulation matrix;
- four macros;
- three-slot effects rack with modulation, delay, space, EQ, nonlinear, and
  exciter algorithms;
- VST3, AU, and standalone;
- versioned presets;
- background sample loading.

Do not include initially:

- granular synthesis;
- additive resynthesis;
- full sampler recording;
- scripting;
- complex FM networks;
- dozens of analog filter models;
- cloud services;
- online account system;
- deep sample editing.

---

# 36. Recommended first implementation slice

The best first end-to-end slice is:

```text
PCM attack sample
    +
VA saw sustain
    ->
two-Partials Tone
    ->
low-pass filter
    ->
amp envelope
    ->
chorus
    ->
reverb
    ->
VST3/AU output
```

This validates:

- PCM loading;
- VA oscillation;
- tone structures;
- envelopes;
- filters;
- effects;
- voice allocation;
- plugin output;
- preset state;
- UI binding.

Once this works, expanding to four Partials and two Tones is straightforward.

---

# 37. Final design summary

The complete instrument should be organized as:

```text
Partial:
  PCM | VA | Noise
  -> pitch
  -> optional waveshaper
  -> filter
  -> amp
  -> pan

Two Partials:
  -> Tone structure

Two Tones:
  -> Patch structure

Patch:
  -> modulation
  -> global effects
  -> plugin output
```

The UI should mirror the sound hierarchy:

```text
Browser
  -> Patch
  -> Tone
  -> Partial
  -> Modulation
  -> Samples
  -> Effects
```

That symmetry between DSP structure and UI structure is the main usability principle. A user should always know whether they are editing:

- the source;
- one Partial;
- a two-Partial Tone;
- the relationship between Tones;
- the entire Patch;
- global effects.

This architecture is small enough to implement incrementally, but broad enough to support M1-like PCM layering, D-50-like transient-plus-sustain structures, Wavestation-like vector extensions, and modern virtual-analog synthesis in one coherent plugin.
