//
//  SynthParameters.h
//  Shared parameter address definitions used by both the C++ DSP core and
//  the Swift AUAudioUnit. Plain C header (no Objective-C types) so it can be
//  included from .hpp/.cpp as well as through the Swift bridging header.
//

#ifndef SynthParameters_h
#define SynthParameters_h

#include "ModMatrix.h"

// Fixed underlying type so the values match AUParameterAddress (UInt64) and
// import cleanly into Swift as a proper enum.
typedef enum SynthParam : unsigned long long {
    SynthParamOscWaveform = 0,   // 0=saw, 1=square, 2=pulse
    SynthParamOscPulseWidth,     // 0.02 .. 0.98
    SynthParamOctave,            // -3 .. +3 (integer)
    SynthParamNoiseMix,          // 0 .. 1  (osc <-> noise blend)

    SynthParamAmpAttack,         // seconds
    SynthParamAmpDecay,          // seconds
    SynthParamAmpSustain,        // 0 .. 1
    SynthParamAmpRelease,        // seconds

    SynthParamFilterAttack,      // seconds
    SynthParamFilterDecay,       // seconds
    SynthParamFilterSustain,     // 0 .. 1
    SynthParamFilterRelease,     // seconds

    SynthParamFilterCutoff,      // Hz
    SynthParamFilterResonance,   // 0 .. 1
    SynthParamFilterEnvAmount,   // -1 .. 1 (bipolar, in octaves * range)
    SynthParamFilterSlope,       // 0 = 12 dB/oct, 1 = 24 dB/oct
    SynthParamFilterKeyTrack,    // 0 .. 1 (cutoff follows note pitch)

    SynthParamLFOWaveform,       // 0=sine, 1=square, 2=saw
    SynthParamLFORate,           // Hz
    SynthParamLFOToOscFreq,      // 0 .. 1 (vibrato depth, semitones scaled)
    SynthParamLFOToPulseWidth,   // 0 .. 1
    SynthParamLFOToCutoff,       // 0 .. 1
    SynthParamLFOToResonance,    // 0 .. 1

    SynthParamMasterGain,        // 0 .. 1 (linear)
    SynthParamAnalogAmount,      // 0 .. 1 (drift / detune / saturation depth)
    SynthParamPitchBendRange,    // semitones (display only; bend applied live)
    SynthParamOscPhaseSpread,    // 0 .. 1 (per-voice start-phase randomisation)

    SynthParamOsc2Waveform,      // 0=saw, 1=square, 2=pulse
    SynthParamOsc2Octave,        // -2 .. +2 (integer)
    SynthParamOsc2Detune,        // -100 .. +100 cents (fine detune of osc 2)
    SynthParamOscMix,            // 0 = osc1 only ... 1 = osc2 only
    SynthParamFilterDrive,       // 0 .. 1 (overdrive / distortion into filter)

    SynthParamOsc2Semitone,      // -12 .. +12 semitones (coarse tune of osc 2)
    SynthParamOsc2Sync,          // 0 = off, 1 = hard-sync osc 2 to osc 1
    SynthParamOscCrossMod,       // 0 .. 1 (osc 1 cross-modulates osc 2 frequency)

    SynthParamOsc2PitchEnv,      // -1 .. 1 (filter envelope -> osc 2 pitch)
    SynthParamLFOToCrossMod,     // 0 .. 1 (LFO -> cross-mod amount)
    SynthParamOscCrossModTZ,     // 0 = exponential FM, 1 = through-zero linear FM

    SynthParamVelToVolume,       // 0 .. 1 (key velocity -> amplitude depth)
    SynthParamVelToCutoff,       // 0 .. 1 (key velocity -> filter cutoff)
    SynthParamVelToResonance,    // 0 .. 1 (key velocity -> filter resonance)
    SynthParamVelToDrive,        // 0 .. 1 (key velocity -> filter drive)

    SynthParamOsc2PulseWidth,    // 0.02 .. 0.98 (independent pulse width for osc 2)

    SynthParamGlideTime,         // 0 .. 1.5 s (portamento; 0 = off)
    SynthParamGlideStart,        // -12 .. +12 semitones; 0 = glide from previous note

    SynthParamLFOKeyTrigger,     // 0 = free-run, 1 = reset LFO phase on key press
    SynthParamLFODelay,          // 0 .. 2 s before the LFO fades in from key press

    SynthParamVoiceCount,        // 1 .. 8 max voices (1 = mono)
    SynthParamLegato,            // 0/1 (mono): don't retrigger on overlapping notes

    // Mixer levels (replace the old OscMix balance / NoiseMix blend).
    SynthParamOsc1Level,         // 0 .. 1
    SynthParamOsc2Level,         // 0 .. 1
    SynthParamNoiseLevel,        // 0 .. 1

    // Wavetable (used when an oscillator's waveform is set to WT). Shared by
    // both oscillators. Wavetable oscillators do not support cross-mod / sync.
    SynthParamWavetable,         // 0..3  Harmonic / FM / Choir / Metallic
    SynthParamWTFrame,           // 0..1  timbre morph
    SynthParamWTLiveness,        // 0..1  phase-drift shimmer depth

    // Wavetable frame modulation.
    SynthParamLFOToWTFrame,      // 0 .. 1  (LFO -> WT frame, animated morph)
    SynthParamWTFrameEnv,        // -1 .. 1 (filter envelope -> WT frame)

    // Second LFO — a modulation-matrix source (global, free-running).
    SynthParamLFO2Waveform,      // 0=sine, 1=square, 2=saw
    SynthParamLFO2Rate,          // Hz

    // Modulation matrix: 6 slots of (source, destination, amount -1..1).
    SynthParamMod1Source, SynthParamMod1Dest, SynthParamMod1Amount,
    SynthParamMod2Source, SynthParamMod2Dest, SynthParamMod2Amount,
    SynthParamMod3Source, SynthParamMod3Dest, SynthParamMod3Amount,
    SynthParamMod4Source, SynthParamMod4Dest, SynthParamMod4Amount,
    SynthParamMod5Source, SynthParamMod5Dest, SynthParamMod5Amount,
    SynthParamMod6Source, SynthParamMod6Dest, SynthParamMod6Amount,

    // Arpeggiator (host-tempo-synchronised clock, latch).
    SynthParamArpOn,        // 0 = off, 1 = on
    SynthParamArpMode,      // 0=Up, 1=Down, 2=Up/Down, 3=Random
    SynthParamArpOctaves,   // 1..4 octave range
    SynthParamArpRate,      // indexed musical division (see SYNTH_SYNC_* below)
    SynthParamArpGate,      // 0.05..1  fraction of the step the note sounds
    SynthParamArpHold,      // 0/1 latch held notes

    // Global stereo effects (chorus -> delay).
    SynthParamChorusMix,       // 0..1 wet/dry
    SynthParamChorusRate,      // indexed musical division
    SynthParamChorusDepth,     // 0..1 modulation depth
    SynthParamDelayMix,        // 0..1 wet/dry
    SynthParamDelayTime,       // indexed musical division
    SynthParamDelayFeedback,   // 0..0.94
    SynthParamDelayTone,       // 0..1 dark..bright feedback filtering
    SynthParamDelayPingPong,   // 0..1 stereo cross-feedback

    // Appended to preserve every existing AU parameter address.
    SynthParamStereoSpread,    // 0..1 scales fixed per-voice stereo positions

    SynthParamCount
} SynthParam;

// Musical division indices shared by arp, chorus and delay. Values are ordered
// slow-to-fast; the DSP table stores their duration in quarter-note beats.
#define SYNTH_SYNC_DIVISION_COUNT 14
#define SYNTH_SYNC_DEFAULT_ARP 11       // 1/16
#define SYNTH_SYNC_DEFAULT_CHORUS 2     // 1/2
#define SYNTH_SYNC_DEFAULT_DELAY 7      // dotted 1/8

// Envelope-time mapping shared by the DSP, the UI display and the presets.
// The stored parameter value is a normalised 0..1; the actual time in seconds
// is  MIN * (MAX/MIN)^(norm^SKEW).  Because the curve lives in the value->time
// mapping (not just the UI), the plug-in knobs, Logic's generic view and
// automation all use the identical response.
#define SYNTH_TIME_MIN  0.0005
#define SYNTH_TIME_MAX  15.0
#define SYNTH_TIME_SKEW 0.7

#endif /* SynthParameters_h */
