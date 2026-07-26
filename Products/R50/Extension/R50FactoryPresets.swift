//
//  R50FactoryPresets.swift
//  A small set of built-in patches, defined in code. Each preset lists only the
//  parameters it overrides; everything else falls back to the tree's Init value.
//

import AudioToolbox

struct R50FactoryPreset {
    let name: String
    let values: [AUParameterAddress: AUValue]
}

enum R50FactoryPresets {

    static let all: [R50FactoryPreset] = [
        R50FactoryPreset(name: "Init Saw", values: [:]),

        R50FactoryPreset(name: "Bright Poly", values: [
            addr(R50ParamOscWave): 0,
            addr(R50ParamCutoff): 6500,
            addr(R50ParamResonance): 0.25,
            addr(R50ParamFilterEnvAmount): 0.35,
            addr(R50ParamAmpAttack): 0.01,
            addr(R50ParamAmpRelease): 0.6,
            addr(R50ParamFilterDecay): 0.9,
        ]),

        R50FactoryPreset(name: "Rubber Bass", values: [
            addr(R50ParamOscWave): 0,
            addr(R50ParamOctave): -1,
            addr(R50ParamCutoff): 320,
            addr(R50ParamResonance): 0.55,
            addr(R50ParamDrive): 0.35,
            addr(R50ParamSlope): 1,
            addr(R50ParamKeyTrack): 0.35,
            addr(R50ParamFilterEnvAmount): 0.6,
            addr(R50ParamAmpSustain): 0.55,
            addr(R50ParamAmpRelease): 0.12,
            addr(R50ParamFilterDecay): 0.22,
            addr(R50ParamFilterSustain): 0.05,
        ]),

        R50FactoryPreset(name: "Hollow Pulse Pad", values: [
            addr(R50ParamOscWave): 4,        // variable pulse
            addr(R50ParamPulseWidth): 0.22,
            addr(R50ParamCutoff): 1400,
            addr(R50ParamResonance): 0.2,
            addr(R50ParamFilterEnvAmount): 0.3,
            addr(R50ParamAmpAttack): 0.6,
            addr(R50ParamAmpRelease): 1.6,
            addr(R50ParamFilterAttack): 0.9,
            addr(R50ParamFilterSustain): 0.5,
        ]),

        R50FactoryPreset(name: "Drawbar Organ", values: [
            addr(R50ParamOscWave): 5,
            addr(R50ParamCutoff): 9000,
            addr(R50ParamKeyTrack): 0.25,
            addr(R50ParamFilterEnvAmount): 0.0,
            addr(R50ParamAmpAttack): 0.002,
            addr(R50ParamAmpDecay): 0.05,
            addr(R50ParamAmpSustain): 1.0,
            addr(R50ParamAmpRelease): 0.05,
        ]),

        R50FactoryPreset(name: "Tine Electric", values: [
            addr(R50ParamOscWave): 6,
            addr(R50ParamCutoff): 4200,
            addr(R50ParamKeyTrack): 0.7,
            addr(R50ParamFilterEnvAmount): 0.5,
            addr(R50ParamAmpDecay): 1.8,
            addr(R50ParamAmpSustain): 0.25,
            addr(R50ParamAmpRelease): 0.5,
            addr(R50ParamFilterDecay): 0.7,
            addr(R50ParamFilterSustain): 0.1,
        ]),

        R50FactoryPreset(name: "Reed Solo", values: [
            addr(R50ParamOscWave): 7,        // clarinet
            addr(R50ParamCutoff): 5200,
            addr(R50ParamResonance): 0.1,
            addr(R50ParamKeyTrack): 0.5,
            addr(R50ParamAmpAttack): 0.05,
            addr(R50ParamAmpSustain): 0.9,
            addr(R50ParamAmpRelease): 0.18,
        ]),

        R50FactoryPreset(name: "String Machine", values: [
            addr(R50ParamOscWave): 8,
            addr(R50ParamCutoff): 3400,
            addr(R50ParamResonance): 0.12,
            addr(R50ParamFilterEnvAmount): 0.25,
            addr(R50ParamAmpAttack): 0.35,
            addr(R50ParamAmpRelease): 0.9,
            addr(R50ParamFilterAttack): 0.5,
            addr(R50ParamFilterSustain): 0.55,
        ]),

        R50FactoryPreset(name: "Choir Ah", values: [
            addr(R50ParamOscWave): 9,
            addr(R50ParamCutoff): 6000,
            addr(R50ParamKeyTrack): 0.3,
            addr(R50ParamAmpAttack): 0.28,
            addr(R50ParamAmpSustain): 0.9,
            addr(R50ParamAmpRelease): 0.7,
        ]),

        R50FactoryPreset(name: "Breathy Flute", values: [
            addr(R50ParamOscWave): 7,        // clarinet
            addr(R50ParamNoiseMix): 0.35,
            addr(R50ParamNoiseSpectrum): 5,  // band-passed
            addr(R50ParamNoiseTone): 0.3,
            addr(R50ParamNoisePitchTrack): 1,
            addr(R50ParamCutoff): 4800,
            addr(R50ParamKeyTrack): 0.6,
            addr(R50ParamAmpAttack): 0.09,
            addr(R50ParamAmpSustain): 0.85,
            addr(R50ParamAmpRelease): 0.2,
        ]),

        R50FactoryPreset(name: "Wind Bed", values: [
            addr(R50ParamNoiseMix): 1.0,     // noise only
            addr(R50ParamNoiseSpectrum): 1,  // pink
            addr(R50ParamCutoff): 900,
            addr(R50ParamResonance): 0.35,
            addr(R50ParamKeyTrack): 1.0,
            addr(R50ParamFilterEnvAmount): 0.3,
            addr(R50ParamAmpAttack): 1.2,
            addr(R50ParamAmpSustain): 1.0,
            addr(R50ParamAmpRelease): 1.8,
            addr(R50ParamFilterAttack): 1.5,
            addr(R50ParamFilterSustain): 0.7,
        ]),

        R50FactoryPreset(name: "Digital Grit", values: [
            addr(R50ParamOscWave): 3,        // 10% pulse
            addr(R50ParamNoiseMix): 0.45,
            addr(R50ParamNoiseSpectrum): 6,  // sample & hold
            addr(R50ParamNoiseTone): 0.35,
            addr(R50ParamNoisePitchTrack): 1,
            addr(R50ParamCutoff): 3000,
            addr(R50ParamResonance): 0.3,
            addr(R50ParamDrive): 0.25,
            addr(R50ParamAmpDecay): 0.4,
            addr(R50ParamAmpSustain): 0.5,
            addr(R50ParamAmpRelease): 0.2,
        ]),

        R50FactoryPreset(name: "Glass Bell", values: [
            addr(R50ParamOscWave): 10,
            addr(R50ParamCutoff): 8500,
            addr(R50ParamKeyTrack): 0.8,
            addr(R50ParamFilterEnvAmount): 0.35,
            addr(R50ParamAmpAttack): 0.001,
            addr(R50ParamAmpDecay): 2.2,
            addr(R50ParamAmpSustain): 0.0,
            addr(R50ParamAmpRelease): 1.4,
            addr(R50ParamFilterDecay): 1.2,
            addr(R50ParamFilterSustain): 0.15,
        ]),
    ]

    private static func addr(_ p: R50Param) -> AUParameterAddress {
        AUParameterAddress(p.rawValue)
    }
}
