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
    R50ParamOscWave = 0,         // 0..10, see waveDescriptors() in R50Wave.hpp
    R50ParamPulseWidth,          // 0.02 .. 0.98 (variable-pulse wave only)
    R50ParamOctave,              // -2 .. +2 (integer)

    R50ParamCutoff,              // Hz
    R50ParamResonance,           // 0 .. 1
    R50ParamDrive,               // 0 .. 1
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

    // Partial 2: the full field set in R50PartialField order. Address it with
    // r50PartialParam() rather than by name.
    R50ParamP2Base,
    R50ParamP2Last = R50ParamP2Base + 29,

    // --- Tone structure -----------------------------------------------------
    R50ParamToneStructure,       // 0..4, see ToneStructure in R50Voice.hpp
    R50ParamToneRingLevel,       // 0 .. 1
    R50ParamToneBlendTime,       // seconds (AttackSustain handover)
    R50ParamToneCrossfadeLow,    // MIDI note
    R50ParamToneCrossfadeHigh,   // MIDI note

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
    R50FieldDrive,
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
    R50PartialFieldCount
} R50PartialField;

/// The single authority mapping (Partial, field) to an address. Partial 1 uses
/// the original scattered addresses so saved state keeps its meaning; Partial 2
/// reads straight out of its block.
static inline R50Param r50PartialParam(int partial, R50PartialField field) {
    if (partial > 0) {
        return (R50Param)((unsigned long long)R50ParamP2Base
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
        case R50FieldDrive:            return R50ParamDrive;
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
        default:                       return R50ParamCount;
    }
}

#endif /* R50Parameters_h */
