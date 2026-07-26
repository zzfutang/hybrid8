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
            addr(R50ParamOscWave): 2,
            addr(R50ParamPulseWidth): 0.22,
            addr(R50ParamCutoff): 1400,
            addr(R50ParamResonance): 0.2,
            addr(R50ParamFilterEnvAmount): 0.3,
            addr(R50ParamAmpAttack): 0.6,
            addr(R50ParamAmpRelease): 1.6,
            addr(R50ParamFilterAttack): 0.9,
            addr(R50ParamFilterSustain): 0.5,
        ]),
    ]

    private static func addr(_ p: R50Param) -> AUParameterAddress {
        AUParameterAddress(p.rawValue)
    }
}
