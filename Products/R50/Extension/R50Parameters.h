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
    R50ParamOscWave = 0,         // 0=saw, 1=square, 2=pulse
    R50ParamPulseWidth,          // 0.02 .. 0.98 (pulse only)
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

    R50ParamCount
} R50Param;

#endif /* R50Parameters_h */
