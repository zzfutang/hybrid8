//
//  ModMatrix.h
//  Shared modulation-matrix source / destination indices, used by both the
//  C++ DSP core and the Swift parameter tree / UI. Plain C header.
//
//  The synth uses a *hybrid* scheme: a handful of routings stay hardwired
//  (filter env -> cutoff, LFO -> pitch/vibrato, velocity -> volume, amp env ->
//  amplitude) and everything else goes through this assignable matrix.
//

#ifndef ModMatrix_h
#define ModMatrix_h

// Modulation sources. Index order is part of the stored preset/automation
// value, so only ever append new entries.
typedef enum SynthModSource : int {
    ModSrcNone = 0,
    ModSrcLFO1,
    ModSrcLFO2,
    ModSrcFilterEnv,
    ModSrcAmpEnv,
    ModSrcVelocity,
    ModSrcKeyTrack,
    ModSrcModWheel,
    ModSrcAftertouch,
    ModSrcRandom,
    ModSrcLFO3,          // appended: preserve existing preset source indices
    ModSrcCount
} SynthModSource;

// Modulation destinations. Append-only, same reasoning as above.
typedef enum SynthModDest : int {
    ModDstNone = 0,
    ModDstOscPitch,      // both oscillators, semitones
    ModDstOsc2Pitch,     // osc 2 only, semitones
    ModDstPulseWidth,    // both oscillators
    ModDstCutoff,        // filter cutoff, octaves
    ModDstResonance,     // filter resonance
    ModDstDrive,         // filter drive
    ModDstWTFrame,       // wavetable frame morph
    ModDstWTLiveness,    // wavetable liveness / phase drift
    ModDstCrossMod,      // cross-mod (FM) amount
    ModDstAmp,           // amplitude (tremolo / AM)
    ModDstOsc1Pitch,     // oscillator 1 only, semitones
    ModDstOsc1Level,     // oscillator 1 mixer level
    ModDstOsc2Level,     // oscillator 2 mixer level
    ModDstNoiseLevel,    // noise mixer level
    ModDstVoicePan,      // per-voice pan offset
    ModDstFilterSlope,   // continuous 12 -> 24 dB morph
    ModDstFilterMode,    // continuous LP -> BP -> HP morph
    ModDstOsc1PW,        // oscillator 1 pulse width only
    ModDstOsc2PW,        // oscillator 2 pulse width only
    ModDstWTSmooth,      // wavetable phase interpolation (negative = grainier)
    ModDstWTFrame2,      // oscillator 2 wavetable frame when frame link is off
    ModDstSubOscLevel,   // analog Osc 1 sub-oscillator mixer level
    ModDstRingModLevel,  // analog Osc 1 * Osc 2 ring-mod mixer level
    ModDstCount
} SynthModDest;

#define SYNTH_MOD_SLOTS 6

#endif /* ModMatrix_h */
