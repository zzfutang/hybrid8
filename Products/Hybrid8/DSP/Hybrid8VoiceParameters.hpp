//
//  Hybrid8VoiceParameters.hpp
//  Resolved, per-block snapshot of all synth parameters (plain values, already
//  converted from the atomic parameter store). Passed by const-ref into the
//  voice render path so the audio thread never touches atomics per sample.
//

#pragma once
#include "../../../Shared/DSPCore/Oscillator.hpp"
#include "../../../Shared/DSPCore/LFO.hpp"
#include "../../../Extension/ModMatrix.h"

namespace synth {

struct Params {
    OscWave oscWave       = OscWave::Saw;
    bool    osc1IsWT      = false;
    float   pulseWidth    = 0.5f;
    int     octave        = 0;

    // Wavetable (shared by both oscillators)
    int     wtTable       = 0;
    float   wtFrame       = 0.0f;
    float   wtLiveness    = 0.0f;
    float   lfoToWTFrame  = 0.0f;   // LFO -> WT frame (animated morph)
    float   wtFrameEnv    = 0.0f;   // filter env -> WT frame (-1..1)

    // Mixer levels
    float   osc1Level     = 1.0f;
    float   osc2Level     = 0.0f;
    float   noiseLevel    = 0.0f;

    // Oscillator 2
    OscWave osc2Wave      = OscWave::Saw;
    bool    osc2IsWT      = false;
    float   osc2PulseWidth = 0.5f;
    int     osc2Octave    = 0;
    float   osc2Semitone  = 0.0f;   // semitones (coarse)
    float   osc2Detune    = 0.0f;   // cents (fine)
    bool    osc2Sync      = false;  // hard-sync osc2 to osc1
    float   crossMod      = 0.0f;   // osc1 -> osc2 frequency modulation
    float   osc2PitchEnv  = 0.0f;   // filter env -> osc2 pitch (-1..1)
    bool    crossModTZ    = false;  // through-zero linear FM when true

    float   ampA = 0.005f, ampD = 0.1f, ampS = 0.8f, ampR = 0.2f;
    float   filtA = 0.005f, filtD = 0.2f, filtS = 0.4f, filtR = 0.3f;

    float   cutoff        = 8000.0f; // Hz
    float   resonance     = 0.1f;    // 0..1
    float   filterEnvAmt  = 0.5f;    // -1..1
    float   filterSlopeMix = 0.0f;   // 0=12dB .. 1=24dB (smoothed cross-fade)
    float   filterModeMix  = 0.0f;   // 0=LP, 1=BP, 2=HP (smoothed)
    float   filterKeyTrack = 0.0f;   // 0..1
    float   filterDrive   = 0.0f;    // 0..1 overdrive into filter

    LFOWave lfoWave       = LFOWave::Sine;
    float   lfoRate       = 5.0f;
    bool    lfoKeyTrigger = false;  // legacy: reset LFO1 phase on note-on
    float   lfoDelay      = 0.0f;   // seconds before LFO fades in
    bool    lfo1Unipolar  = false;
    float   lfo1Phase     = 0.0f;
    float   lfo2Delay     = 0.0f;
    float   lfo3Delay     = 0.0f;
    int     vibratoLFO    = 0;
    // Per-LFO run mode: 0=Loop, 1=Trig (key reset), 2=One-Shot.
    int     lfo1Mode      = 0;
    int     lfo2Mode      = 0;
    int     lfo3Mode      = 0;
    // Full LFO 2 / 3 config (so a voice can run its own key-triggered copy).
    LFOWave lfo2Wave      = LFOWave::Sine;
    float   lfo2Rate      = 2.0f;
    bool    lfo2Unipolar  = false;
    float   lfo2Phase     = 0.0f;
    LFOWave lfo3Wave      = LFOWave::Sine;
    float   lfo3Rate      = 1.0f;
    bool    lfo3Unipolar  = false;
    float   lfo3Phase     = 0.0f;
    float   lfoToOscFreq  = 0.0f;
    float   lfoToPulseWidth = 0.0f;
    float   lfoToCutoff   = 0.0f;
    float   lfoToResonance = 0.0f;
    float   lfoToCrossMod = 0.0f;

    float   masterGain    = 0.7f;
    float   analogAmount  = 0.3f;
    float   pitchBendSemis = 0.0f;
    float   glideCoef     = 1.0f;   // per-sample glide coefficient (1 = instant)

    // Velocity routing depths
    float   velToVolume   = 1.0f;   // 1 = fully velocity sensitive
    float   velToCutoff   = 0.0f;
    float   velToResonance = 0.0f;
    float   velToDrive    = 0.0f;

    // --- Modulation matrix -------------------------------------------------
    int     modSource[SYNTH_MOD_SLOTS] = {0};   // SynthModSource
    int     modDest[SYNTH_MOD_SLOTS]   = {0};   // SynthModDest
    float   modAmount[SYNTH_MOD_SLOTS] = {0};   // -1 .. 1

    // Global mod sources sampled once per block (slowly varying).
    float   modWheel      = 0.0f;   // CC1, 0 .. 1
    float   aftertouch    = 0.0f;   // channel/poly pressure, 0 .. 1
};

} // namespace synth
