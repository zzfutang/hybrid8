//
//  R50Parameters.h
//  Shared parameter addresses for R50, used by both the C++ DSP core and the
//  Swift AUAudioUnit. Plain C header (no Objective-C types) so it can be
//  included from .hpp/.mm as well as through the Swift bridging header.
//

#ifndef R50Parameters_h
#define R50Parameters_h

// Fixed underlying type so the values match AUParameterAddress (UInt64) and
// import cleanly into Swift as a proper enum.
typedef enum R50Param : unsigned long long {
    R50ParamOscWave = 0,         // 0..16, see waveDescriptors() in R50Wave.hpp
    R50ParamPulseWidth,          // 0.02 .. 0.98 (variable-pulse wave only)
    R50ParamOctave,              // -2 .. +2 (integer)

    R50ParamCutoff,              // Hz
    R50ParamResonance,           // 0 .. 1
    R50ParamSlope,               // 0 = 12 dB/oct, 1 = 24 dB/oct
    R50ParamKeyTrack,            // 0 .. 1 (cutoff follows note pitch)
    R50ParamFilterEnvAmount,     // -1 .. 1 (bipolar, scaled to octaves)

    R50ParamAmpAttack,           // seconds
    R50ParamAmpDecay,            // seconds
    R50ParamAmpSustain,          // 0 .. 1
    R50ParamAmpRelease,          // seconds

    R50ParamFilterAttack,        // seconds
    R50ParamFilterDecay,         // seconds
    R50ParamFilterSustain,       // 0 .. 1
    R50ParamFilterRelease,       // seconds

    R50ParamMasterGain,          // 0 .. 1 (linear)
    R50ParamPitchBendRange,      // semitones

    // Appended in phase 2. The enum is append-only: inserting here would
    // silently remap every saved preset and host automation lane.
    R50ParamNoiseMix,            // 0 = oscillator only .. 1 = noise only
    R50ParamNoiseSpectrum,       // 0..6, see NoiseSpectrum in R50Noise.hpp
    R50ParamNoiseTone,           // 0 .. 1 (band centre / step-rate scaling)
    R50ParamNoiseRate,           // Hz (Sample & Hold step rate)
    R50ParamNoisePitchTrack,     // 0 = fixed, 1 = follows the note

    // Appended with the sample engine.
    R50ParamSourceType,          // 0 = wave table, 1 = sample
    R50ParamSampleInstrument,    // index into the SampleLibrary
    R50ParamSampleStart,         // 0 .. 1 scrub into the asset

    // --- Partials -----------------------------------------------------------
    // Everything above describes Partial 1, which is why those addresses keep
    // their meaning: an old preset is simply a one-Partial patch. Partial 1
    // only lacked the mixing controls a second Partial makes necessary, so
    // those are appended here, and Partial 2 gets a contiguous block.
    R50ParamP1Enabled,           // 0 / 1
    R50ParamP1Level,             // 0 .. 1
    R50ParamP1Pan,               // -1 .. +1
    R50ParamP1Semitone,          // -24 .. +24
    R50ParamP1Fine,              // -100 .. +100 cents

    // The workstation EG's extra levels and times, the pitch envelope and the
    // waveshaper — Partial 1's copies, appended like everything else.
    R50ParamP1AmpAttackLevel,
    R50ParamP1AmpBreak,
    R50ParamP1AmpSlope,
    R50ParamP1FilterAttackLevel,
    R50ParamP1FilterBreak,
    R50ParamP1FilterSlope,
    R50ParamP1PitchAmount,
    R50ParamP1PitchAttack,
    R50ParamP1PitchDecay,
    R50ParamP1ShaperType,
    R50ParamP1ShaperDrive,
    R50ParamP1ShaperPosition,
    R50ParamP1PitchKeyFollow,

    // Partial 2: the full field set in R50PartialField order. Address it with
    // r50PartialParam() rather than by name.
    //
    // The block is sized well above R50PartialFieldCount on purpose. It used to
    // end exactly at the last field, which meant every new Partial field shifted
    // the tone, effects, LFO, macro and matrix addresses that follow — the one
    // thing the append-only rule above exists to prevent. Reserving the space
    // costs four bytes of `store_` per unused slot and buys silence.
    R50ParamP2Base,
    R50ParamP2Last = R50ParamP2Base + 63,

    // --- Tone structure -----------------------------------------------------
    R50ParamToneStructure,       // 0..4, see ToneStructure in R50Voice.hpp
    R50ParamToneRingLevel,       // 0 .. 1
    R50ParamToneBlendTime,       // seconds (AttackSustain handover)
    R50ParamToneCrossfadeLow,    // MIDI note
    R50ParamToneCrossfadeHigh,   // MIDI note

    // --- Effects ------------------------------------------------------------
    // A global stage after the voice sum, so none of these are per Partial.
    R50ParamFxCompressor,        // 0 = off .. 1 = heavily compressed

    // --- Modulation ---------------------------------------------------------
    // Per voice, not per Partial, so these are plain appended addresses rather
    // than fields on r50PartialParam.
    R50ParamLfo1Wave,            // 0..4, see synth::LFOWave
    R50ParamLfo1Rate,            // Hz
    R50ParamLfo1Delay,           // seconds before it starts
    R50ParamLfo1Fade,            // seconds to reach full depth
    R50ParamLfo1Retrigger,       // 0 = free-running and shared, 1 = per note
    R50ParamLfo1Phase,           // 0 .. 1
    R50ParamLfo2Wave,
    R50ParamLfo2Rate,
    R50ParamLfo2Delay,
    R50ParamLfo2Fade,
    R50ParamLfo2Retrigger,
    R50ParamLfo2Phase,

    // Six slots of source / destination / target / amount. Address these with
    // r50ModSlotParam() rather than by name.
    R50ParamModSlotBase,
    R50ParamModSlotLast = R50ParamModSlotBase + 23,

    R50ParamMacro1,              // 0 .. 1, usable as a matrix source
    R50ParamMacro2,
    R50ParamMacro3,
    R50ParamMacro4,

    // --- Three-slot global effects rack ------------------------------------
    // Stable address block for routing and three generic effect slots.
    R50ParamP1DryLevel,
    R50ParamP1Send1,
    R50ParamP1Send2,
    R50ParamP1Send3,
    R50ParamFxTopology,          // retired: routing is per slot now (Insert/Send)
    R50ParamFxSlotBase,
    R50ParamFxSlotLast = R50ParamFxSlotBase + 47,

    // --- Patch engine -------------------------------------------------------
    // Two more fixed-capacity Partial blocks complete Tone B. Existing
    // addresses above remain untouched.
    R50ParamP3Base,
    R50ParamP3Last = R50ParamP3Base + 63,
    R50ParamP4Base,
    R50ParamP4Last = R50ParamP4Base + 63,

    R50ParamToneBStructure,
    R50ParamToneBRingLevel,
    R50ParamToneBBlendTime,
    R50ParamToneBCrossfadeLow,
    R50ParamToneBCrossfadeHigh,

    R50ParamPatchStructure,      // 0..4, see PatchStructure
    R50ParamPatchSplitPoint,     // MIDI note
    R50ParamPatchVelocitySplit,  // 0..1
    R50ParamPatchVectorMix,      // 0 = Tone A, 1 = Tone B
    R50ParamToneALevel,          // 0..1
    R50ParamToneBLevel,          // 0..1
    R50ParamVectorLfoWave,       // 0..4, synth::LFOWave
    R50ParamVectorLfoRate,       // Hz
    R50ParamVectorLfoDepth,      // 0..1
    R50ParamVectorLfoRetrigger,  // 0 = free-running, 1 = note retrigger
    R50ParamVectorLfoPhase,      // 0..1

    // Independent Tone-level outputs for the product created by Ring
    // structure. Appended so every earlier address remains stable.
    R50ParamToneRingPan,
    R50ParamToneRingDry,
    R50ParamToneRingSend1,
    R50ParamToneRingSend2,
    R50ParamToneRingSend3,
    R50ParamToneBRingPan,
    R50ParamToneBRingDry,
    R50ParamToneBRingSend1,
    R50ParamToneBRingSend2,
    R50ParamToneBRingSend3,

    // Patch-level voice allocation: 0 = polyphonic, 1 = monophonic with
    // last-note priority (releasing returns to the previous held key).
    R50ParamVoiceMode,
    // Seconds for a new note to settle from the previous pitch; 0 = off.
    R50ParamGlideTime,
    // 0 = always glide; 1 = legato (fingered portamento): only a note played
    // while another key is still held slides, detached notes start on pitch.
    R50ParamGlideMode,
    // Trajectory: 0 = ease-out (RC portamento), 0.5 = linear, 1 = full S.
    R50ParamGlideShape,

    R50ParamCount
} R50Param;

/// Every parameter a Partial owns. The order defines Partial 2's block layout,
/// so it is append-only for exactly the same reason R50Param is.
typedef enum R50PartialField {
    R50FieldSourceType = 0,
    R50FieldSampleInstrument,
    R50FieldSampleStart,
    R50FieldOscWave,
    R50FieldPulseWidth,
    R50FieldOctave,
    R50FieldNoiseMix,
    R50FieldNoiseSpectrum,
    R50FieldNoiseTone,
    R50FieldNoiseRate,
    R50FieldNoisePitchTrack,
    R50FieldCutoff,
    R50FieldResonance,
    R50FieldSlope,
    R50FieldKeyTrack,
    R50FieldFilterEnvAmount,
    R50FieldAmpAttack,
    R50FieldAmpDecay,
    R50FieldAmpSustain,
    R50FieldAmpRelease,
    R50FieldFilterAttack,
    R50FieldFilterDecay,
    R50FieldFilterSustain,
    R50FieldFilterRelease,
    R50FieldEnabled,
    R50FieldLevel,
    R50FieldPan,
    R50FieldSemitone,
    R50FieldFine,

    // Workstation EG extras, pitch envelope and waveshaper.
    R50FieldAmpAttackLevel,
    R50FieldAmpBreak,
    R50FieldAmpSlope,
    R50FieldFilterAttackLevel,
    R50FieldFilterBreak,
    R50FieldFilterSlope,
    R50FieldPitchAmount,
    R50FieldPitchAttack,
    R50FieldPitchDecay,
    R50FieldShaperType,
    R50FieldShaperDrive,
    R50FieldShaperPosition,
    R50FieldPitchKeyFollow,
    R50FieldDryLevel,
    R50FieldSend1,
    R50FieldSend2,
    R50FieldSend3,

    R50PartialFieldCount
} R50PartialField;

/// Common fields stored for each global effect slot. The block layout is
/// append-only and full (3 x 16 fills the reserved address range), so retired
/// fields are repurposed in place rather than removed: Routing took the old
/// input-gain address when the per-slot gain stages went away, and the old
/// output-gain address is held in reserve.
typedef enum R50FxSlotField {
    R50FxFieldAlgorithm = 0,
    R50FxFieldBypass,
    R50FxFieldRouting,           // 0 = insert on the main path, 1 = send
    R50FxFieldReserved,          // retired output gain; not registered
    R50FxFieldMix,               // insert: wet/dry mix; send: return level
    R50FxFieldWidth,
    R50FxFieldControl1,
    R50FxFieldControl2,
    R50FxFieldControl3,
    R50FxFieldControl4,
    R50FxFieldControl5,
    R50FxFieldControl6,
    R50FxFieldControl7,
    R50FxFieldControl8,
    R50FxFieldMode1,
    R50FxFieldMode2,
    R50FxSlotFieldCount
} R50FxSlotField;

static inline R50Param r50FxSlotParam(int slot, R50FxSlotField field) {
    return (R50Param)((unsigned long long)R50ParamFxSlotBase
                    + (unsigned long long)slot * R50FxSlotFieldCount
                    + (unsigned long long)field);
}

/// Fields a matrix slot owns. The order defines the slot block's layout, so it
/// is append-only for the same reason the parameter enum is.
typedef enum R50ModSlotField {
    R50ModFieldSource = 0,
    R50ModFieldDestination,
    R50ModFieldTarget,
    R50ModFieldAmount,
    R50ModSlotFieldCount
} R50ModSlotField;

/// Address of one field of one matrix slot.
static inline R50Param r50ModSlotParam(int slot, R50ModSlotField field) {
    return (R50Param)((unsigned long long)R50ParamModSlotBase
                    + (unsigned long long)slot * R50ModSlotFieldCount
                    + (unsigned long long)field);
}

/// The single authority mapping (Partial, field) to an address. Partial 1 uses
/// the original scattered addresses so saved state keeps its meaning; Partial 2
/// reads straight out of its block.
static inline R50Param r50PartialParam(int partial, R50PartialField field) {
    if (partial == 1) {
        return (R50Param)((unsigned long long)R50ParamP2Base
                        + (unsigned long long)field);
    }
    if (partial == 2) {
        return (R50Param)((unsigned long long)R50ParamP3Base
                        + (unsigned long long)field);
    }
    if (partial == 3) {
        return (R50Param)((unsigned long long)R50ParamP4Base
                        + (unsigned long long)field);
    }
    switch (field) {
        case R50FieldSourceType:       return R50ParamSourceType;
        case R50FieldSampleInstrument: return R50ParamSampleInstrument;
        case R50FieldSampleStart:      return R50ParamSampleStart;
        case R50FieldOscWave:          return R50ParamOscWave;
        case R50FieldPulseWidth:       return R50ParamPulseWidth;
        case R50FieldOctave:           return R50ParamOctave;
        case R50FieldNoiseMix:         return R50ParamNoiseMix;
        case R50FieldNoiseSpectrum:    return R50ParamNoiseSpectrum;
        case R50FieldNoiseTone:        return R50ParamNoiseTone;
        case R50FieldNoiseRate:        return R50ParamNoiseRate;
        case R50FieldNoisePitchTrack:  return R50ParamNoisePitchTrack;
        case R50FieldCutoff:           return R50ParamCutoff;
        case R50FieldResonance:        return R50ParamResonance;
        case R50FieldSlope:            return R50ParamSlope;
        case R50FieldKeyTrack:         return R50ParamKeyTrack;
        case R50FieldFilterEnvAmount:  return R50ParamFilterEnvAmount;
        case R50FieldAmpAttack:        return R50ParamAmpAttack;
        case R50FieldAmpDecay:         return R50ParamAmpDecay;
        case R50FieldAmpSustain:       return R50ParamAmpSustain;
        case R50FieldAmpRelease:       return R50ParamAmpRelease;
        case R50FieldFilterAttack:     return R50ParamFilterAttack;
        case R50FieldFilterDecay:      return R50ParamFilterDecay;
        case R50FieldFilterSustain:    return R50ParamFilterSustain;
        case R50FieldFilterRelease:    return R50ParamFilterRelease;
        case R50FieldEnabled:          return R50ParamP1Enabled;
        case R50FieldLevel:            return R50ParamP1Level;
        case R50FieldPan:              return R50ParamP1Pan;
        case R50FieldSemitone:         return R50ParamP1Semitone;
        case R50FieldFine:             return R50ParamP1Fine;
        case R50FieldAmpAttackLevel:   return R50ParamP1AmpAttackLevel;
        case R50FieldAmpBreak:         return R50ParamP1AmpBreak;
        case R50FieldAmpSlope:         return R50ParamP1AmpSlope;
        case R50FieldFilterAttackLevel:return R50ParamP1FilterAttackLevel;
        case R50FieldFilterBreak:      return R50ParamP1FilterBreak;
        case R50FieldFilterSlope:      return R50ParamP1FilterSlope;
        case R50FieldPitchAmount:      return R50ParamP1PitchAmount;
        case R50FieldPitchAttack:      return R50ParamP1PitchAttack;
        case R50FieldPitchDecay:       return R50ParamP1PitchDecay;
        case R50FieldShaperType:       return R50ParamP1ShaperType;
        case R50FieldShaperDrive:      return R50ParamP1ShaperDrive;
        case R50FieldShaperPosition:   return R50ParamP1ShaperPosition;
        case R50FieldPitchKeyFollow:   return R50ParamP1PitchKeyFollow;
        case R50FieldDryLevel:         return R50ParamP1DryLevel;
        case R50FieldSend1:            return R50ParamP1Send1;
        case R50FieldSend2:            return R50ParamP1Send2;
        case R50FieldSend3:            return R50ParamP1Send3;
        default:                       return R50ParamCount;
    }
}

#endif /* R50Parameters_h */
