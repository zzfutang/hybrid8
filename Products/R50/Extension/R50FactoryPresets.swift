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

    private static let instrumentPresets: [R50FactoryPreset] = [
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
            slot(0, R50FxFieldAlgorithm): 7,
            slot(0, R50FxFieldMix): 0.35,
            slot(2, R50FxFieldAlgorithm): 1,
            slot(2, R50FxFieldMix): 0.18,
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
            slot(0, R50FxFieldAlgorithm): 7,
            slot(0, R50FxFieldMix): 0.45,
            slot(0, R50FxFieldControl1): chorusRate(0.4),
            slot(2, R50FxFieldAlgorithm): 1,
            slot(2, R50FxFieldMix): 0.30,
            slot(2, R50FxFieldControl2): reverbDecay(3.2),
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
            slot(2, R50FxFieldAlgorithm): 1,
            slot(2, R50FxFieldMix): 0.42,
            slot(2, R50FxFieldControl2): reverbDecay(4.0),
            slot(2, R50FxFieldControl3): 0.7,
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
            slot(2, R50FxFieldAlgorithm): 1,
            slot(2, R50FxFieldMix): 0.55,
            slot(2, R50FxFieldControl2): reverbDecay(7.0),
            slot(2, R50FxFieldControl3): 0.85,
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
            addr(R50ParamAmpDecay): 0.4,
            addr(R50ParamAmpSustain): 0.5,
            addr(R50ParamAmpRelease): 0.2,
        ]),

        // ---- Two-Partial patches -------------------------------------------
        // Each of these exists to demonstrate one structure or feature. The
        // patches above are single-Partial and stay that way.

        // The structure the instrument is built around: a sampled transient
        // handing over to a sustaining source.
        R50FactoryPreset(name: "◆ Mallet Choir", values: [
            slot(2, R50FxFieldAlgorithm): 1,
            slot(2, R50FxFieldMix): 0.32,
            slot(2, R50FxFieldControl2): reverbDecay(3.0),
            addr(R50ParamToneStructure): AUValue(Structure.attackSustain),
            addr(R50ParamToneBlendTime): 0.10,

            p1(R50FieldSourceType): 1,
            p1(R50FieldSampleInstrument): AUValue(Instrument.mallet),
            p1(R50FieldCutoff): 9000,
            p1(R50FieldFilterEnvAmount): 0,
            p1(R50FieldAmpDecay): 0.3,
            p1(R50FieldAmpSustain): 0,
            p1(R50FieldAmpRelease): 0.2,

            p2(R50FieldEnabled): 1,
            p2(R50FieldSourceType): 1,
            p2(R50FieldSampleInstrument): AUValue(Instrument.choir),
            p2(R50FieldCutoff): 5000,
            p2(R50FieldKeyTrack): 0.4,
            p2(R50FieldFilterEnvAmount): 0.2,
            p2(R50FieldAmpAttack): 0.06,
            p2(R50FieldAmpSustain): 0.9,
            p2(R50FieldAmpRelease): 0.8,
        ]),

        R50FactoryPreset(name: "◆ Struck Glass", values: [
            slot(1, R50FxFieldAlgorithm): 6,
            slot(1, R50FxFieldMix): 0.24,
            slot(1, R50FxFieldControl1): delayTime(0.28),
            slot(1, R50FxFieldControl3): delayFeedback(0.30),
            slot(2, R50FxFieldAlgorithm): 1,
            slot(2, R50FxFieldMix): 0.35,
            addr(R50ParamToneStructure): AUValue(Structure.attackSustain),
            addr(R50ParamToneBlendTime): 0.06,

            p1(R50FieldSourceType): 1,
            p1(R50FieldSampleInstrument): AUValue(Instrument.tineStrike),
            p1(R50FieldCutoff): 12000,
            p1(R50FieldFilterEnvAmount): 0,
            p1(R50FieldAmpDecay): 0.25,
            p1(R50FieldAmpSustain): 0,

            p2(R50FieldEnabled): 1,
            p2(R50FieldSourceType): 1,
            p2(R50FieldSampleInstrument): AUValue(Instrument.glassPad),
            p2(R50FieldCutoff): 7000,
            p2(R50FieldKeyTrack): 0.7,
            p2(R50FieldAmpAttack): 0.02,
            p2(R50FieldAmpDecay): 3.0,
            p2(R50FieldAmpSustain): 0.35,
            p2(R50FieldAmpRelease): 1.5,
        ]),

        // Ring modulation: a bell against a fifth above it.
        R50FactoryPreset(name: "◆ Ring Bells", values: [
            slot(1, R50FxFieldAlgorithm): 6,
            slot(1, R50FxFieldMix): 0.30,
            slot(1, R50FxFieldControl1): delayTime(0.24),
            slot(1, R50FxFieldControl3): delayFeedback(0.42),
            addr(R50ParamToneStructure): AUValue(Structure.ring),
            addr(R50ParamToneRingLevel): 2.2,

            p1(R50FieldOscWave): 10,          // Bell
            p1(R50FieldCutoff): 9000,
            p1(R50FieldKeyTrack): 0.6,
            p1(R50FieldLevel): 0.18,
            p1(R50FieldAmpDecay): 2.0,
            p1(R50FieldAmpSustain): 0.2,
            p1(R50FieldAmpRelease): 1.2,

            p2(R50FieldEnabled): 1,
            p2(R50FieldOscWave): 0,           // Saw
            p2(R50FieldSemitone): 7,
            p2(R50FieldCutoff): 6000,
            p2(R50FieldLevel): 0.18,
            p2(R50FieldFilterEnvAmount): 0,
            p2(R50FieldAmpSustain): 0.6,
            p2(R50FieldAmpRelease): 1.2,
        ]),

        // Velocity decides which Partial you hear: soft is a tine, hard is reed.
        R50FactoryPreset(name: "◆ Velocity Keys", values: [
            addr(R50ParamToneStructure): AUValue(Structure.velocityCrossfade),

            p1(R50FieldOscWave): 6,           // Tine
            p1(R50FieldCutoff): 3400,
            p1(R50FieldKeyTrack): 0.6,
            p1(R50FieldAmpDecay): 1.6,
            p1(R50FieldAmpSustain): 0.3,
            p1(R50FieldAmpRelease): 0.5,

            p2(R50FieldEnabled): 1,
            p2(R50FieldOscWave): 7,           // Clarinet
            p2(R50FieldCutoff): 7000,
            p2(R50FieldKeyTrack): 0.6,
            p2(R50FieldAmpDecay): 1.6,
            p2(R50FieldAmpSustain): 0.45,
            p2(R50FieldAmpRelease): 0.5,
        ]),

        // The keyboard fades from a pad in the bass to voices up top.
        R50FactoryPreset(name: "◆ Key Split Pad", values: [
            slot(0, R50FxFieldAlgorithm): 7,
            slot(0, R50FxFieldMix): 0.30,
            slot(2, R50FxFieldAlgorithm): 1,
            slot(2, R50FxFieldMix): 0.40,
            slot(2, R50FxFieldControl2): reverbDecay(4.5),
            addr(R50ParamToneStructure): AUValue(Structure.keyCrossfade),
            addr(R50ParamToneCrossfadeLow): 48,
            addr(R50ParamToneCrossfadeHigh): 72,

            p1(R50FieldSourceType): 1,
            p1(R50FieldSampleInstrument): AUValue(Instrument.warmPad),
            p1(R50FieldCutoff): 2600,
            p1(R50FieldAmpAttack): 0.15,
            p1(R50FieldAmpSustain): 1.0,
            p1(R50FieldAmpRelease): 0.9,

            p2(R50FieldEnabled): 1,
            p2(R50FieldOscWave): 9,           // Vocal Ah
            p2(R50FieldCutoff): 6500,
            p2(R50FieldKeyTrack): 0.3,
            p2(R50FieldAmpAttack): 0.2,
            p2(R50FieldAmpSustain): 0.95,
            p2(R50FieldAmpRelease): 0.9,
        ]),

        // Two detuned Partials panned apart — width from the stereo field
        // rather than from an effect.
        R50FactoryPreset(name: "◆ Wide Strings", values: [
            slot(0, R50FxFieldAlgorithm): 7,
            slot(0, R50FxFieldMix): 0.25,
            slot(2, R50FxFieldAlgorithm): 1,
            slot(2, R50FxFieldMix): 0.35,
            addr(R50ParamToneStructure): AUValue(Structure.mix),

            p1(R50FieldSourceType): 1,
            p1(R50FieldSampleInstrument): AUValue(Instrument.strings),
            p1(R50FieldPan): -0.7,
            p1(R50FieldFine): -7,
            p1(R50FieldLevel): 0.55,
            p1(R50FieldCutoff): 4200,
            p1(R50FieldAmpAttack): 0.25,
            p1(R50FieldAmpSustain): 1.0,
            p1(R50FieldAmpRelease): 0.8,

            p2(R50FieldEnabled): 1,
            p2(R50FieldOscWave): 8,           // Strings wave
            p2(R50FieldPan): 0.7,
            p2(R50FieldFine): 7,
            p2(R50FieldLevel): 0.55,
            p2(R50FieldCutoff): 3600,
            p2(R50FieldAmpAttack): 0.3,
            p2(R50FieldAmpSustain): 1.0,
            p2(R50FieldAmpRelease): 0.9,
        ]),

        // Sampled choir with a tracked band of noise breathing underneath it.
        R50FactoryPreset(name: "◆ Breath Choir", values: [
            slot(2, R50FxFieldAlgorithm): 1,
            slot(2, R50FxFieldMix): 0.45,
            slot(2, R50FxFieldControl2): reverbDecay(4.5),
            slot(2, R50FxFieldControl3): 0.75,
            addr(R50ParamToneStructure): AUValue(Structure.mix),

            p1(R50FieldSourceType): 1,
            p1(R50FieldSampleInstrument): AUValue(Instrument.choir),
            p1(R50FieldCutoff): 5200,
            p1(R50FieldLevel): 0.8,
            p1(R50FieldAmpAttack): 0.12,
            p1(R50FieldAmpSustain): 1.0,
            p1(R50FieldAmpRelease): 0.7,

            p2(R50FieldEnabled): 1,
            p2(R50FieldNoiseMix): 1.0,
            p2(R50FieldNoiseSpectrum): 5,     // band-passed
            p2(R50FieldNoiseTone): 0.28,
            p2(R50FieldNoisePitchTrack): 1,
            p2(R50FieldLevel): 0.3,
            p2(R50FieldCutoff): 9000,
            p2(R50FieldFilterEnvAmount): 0,
            p2(R50FieldAmpAttack): 0.2,
            p2(R50FieldAmpSustain): 0.9,
            p2(R50FieldAmpRelease): 0.6,
        ]),

        // Octave stack: a sampled pad under a bright wave an octave up.
        R50FactoryPreset(name: "◆ Hybrid Stack", values: [
            addr(R50ParamToneStructure): AUValue(Structure.mix),

            p1(R50FieldSourceType): 1,
            p1(R50FieldSampleInstrument): AUValue(Instrument.glassPad),
            p1(R50FieldOctave): -1,
            p1(R50FieldLevel): 0.7,
            p1(R50FieldCutoff): 3000,
            p1(R50FieldAmpAttack): 0.02,
            p1(R50FieldAmpSustain): 0.9,
            p1(R50FieldAmpRelease): 0.6,

            p2(R50FieldEnabled): 1,
            p2(R50FieldOscWave): 4,           // variable pulse
            p2(R50FieldPulseWidth): 0.3,
            p2(R50FieldLevel): 0.45,
            p2(R50FieldCutoff): 4800,
            p2(R50FieldResonance): 0.25,
            p2(R50FieldFilterEnvAmount): 0.4,
            p2(R50FieldAmpDecay): 0.8,
            p2(R50FieldAmpSustain): 0.5,
            p2(R50FieldAmpRelease): 0.5,
        ]),

        // ---- Transient-led patches -----------------------------------------
        // The point of an attack library: the same sustain reads as a
        // different instrument depending on what strikes it.

        R50FactoryPreset(name: "◆ Marimba Pad", values: [
            addr(R50ParamToneStructure): AUValue(Structure.attackSustain),
            addr(R50ParamToneBlendTime): 0.09,
            slot(2, R50FxFieldAlgorithm): 1,
            slot(2, R50FxFieldMix): 0.28,

            p1(R50FieldSourceType): 1,
            p1(R50FieldSampleInstrument): AUValue(Instrument.marimba),
            p1(R50FieldCutoff): 9000,
            p1(R50FieldFilterEnvAmount): 0,
            p1(R50FieldAmpDecay): 0.5,
            p1(R50FieldAmpSustain): 0,
            p1(R50FieldAmpRelease): 0.3,

            p2(R50FieldEnabled): 1,
            p2(R50FieldSourceType): 1,
            p2(R50FieldSampleInstrument): AUValue(Instrument.warmPad),
            p2(R50FieldLevel): 0.6,
            p2(R50FieldCutoff): 3200,
            p2(R50FieldAmpAttack): 0.05,
            p2(R50FieldAmpSustain): 0.8,
            p2(R50FieldAmpRelease): 0.7,
        ]),

        R50FactoryPreset(name: "◆ Vibes & Air", values: [
            addr(R50ParamToneStructure): AUValue(Structure.mix),
            slot(2, R50FxFieldAlgorithm): 1,
            slot(2, R50FxFieldMix): 0.40,
            slot(2, R50FxFieldControl2): reverbDecay(3.5),

            p1(R50FieldSourceType): 1,
            p1(R50FieldSampleInstrument): AUValue(Instrument.vibraphone),
            p1(R50FieldCutoff): 8000,
            p1(R50FieldKeyTrack): 0.6,
            p1(R50FieldFilterEnvAmount): 0,
            p1(R50FieldAmpDecay): 1.2,
            p1(R50FieldAmpSustain): 0,
            p1(R50FieldAmpRelease): 0.6,

            p2(R50FieldEnabled): 1,
            p2(R50FieldSourceType): 1,
            p2(R50FieldSampleInstrument): AUValue(Instrument.breath),
            p2(R50FieldLevel): 0.3,
            p2(R50FieldCutoff): 6000,
            p2(R50FieldAmpAttack): 0.02,
            p2(R50FieldAmpDecay): 0.6,
            p2(R50FieldAmpSustain): 0,
            p2(R50FieldAmpRelease): 0.4,
        ]),

        R50FactoryPreset(name: "◆ Slap Stack", values: [
            addr(R50ParamToneStructure): AUValue(Structure.attackSustain),
            addr(R50ParamToneBlendTime): 0.05,

            p1(R50FieldSourceType): 1,
            p1(R50FieldSampleInstrument): AUValue(Instrument.slapBass),
            p1(R50FieldCutoff): 10000,
            p1(R50FieldFilterEnvAmount): 0,
            p1(R50FieldAmpDecay): 0.25,
            p1(R50FieldAmpSustain): 0,

            p2(R50FieldEnabled): 1,
            p2(R50FieldOscWave): 0,
            p2(R50FieldOctave): -1,
            p2(R50FieldCutoff): 700,
            p2(R50FieldResonance): 0.35,
            p2(R50FieldKeyTrack): 0.4,
            p2(R50FieldFilterEnvAmount): 0.5,
            p2(R50FieldAmpDecay): 0.5,
            p2(R50FieldAmpSustain): 0.4,
            p2(R50FieldAmpRelease): 0.15,
            p2(R50FieldFilterDecay): 0.25,
            p2(R50FieldFilterSustain): 0.1,
        ]),

        R50FactoryPreset(name: "◆ Taiko Choir", values: [
            addr(R50ParamToneStructure): AUValue(Structure.attackSustain),
            addr(R50ParamToneBlendTime): 0.14,
            slot(2, R50FxFieldAlgorithm): 1,
            slot(2, R50FxFieldMix): 0.45,
            slot(2, R50FxFieldControl2): reverbDecay(5.0),
            slot(2, R50FxFieldControl3): 0.8,

            p1(R50FieldSourceType): 1,
            p1(R50FieldSampleInstrument): AUValue(Instrument.taikoDrum),
            p1(R50FieldCutoff): 4000,
            p1(R50FieldFilterEnvAmount): 0,
            p1(R50FieldAmpDecay): 0.6,
            p1(R50FieldAmpSustain): 0,
            p1(R50FieldAmpRelease): 0.3,

            p2(R50FieldEnabled): 1,
            p2(R50FieldSourceType): 1,
            p2(R50FieldSampleInstrument): AUValue(Instrument.choir),
            p2(R50FieldLevel): 0.7,
            p2(R50FieldCutoff): 3600,
            p2(R50FieldAmpAttack): 0.1,
            p2(R50FieldAmpSustain): 0.85,
            p2(R50FieldAmpRelease): 1.0,
        ]),

        R50FactoryPreset(name: "◆ Breath Flute", values: [
            addr(R50ParamToneStructure): AUValue(Structure.attackSustain),
            addr(R50ParamToneBlendTime): 0.13,
            slot(2, R50FxFieldAlgorithm): 1,
            slot(2, R50FxFieldMix): 0.30,

            p1(R50FieldSourceType): 1,
            p1(R50FieldSampleInstrument): AUValue(Instrument.breath),
            p1(R50FieldCutoff): 7000,
            p1(R50FieldFilterEnvAmount): 0,
            p1(R50FieldAmpDecay): 0.35,
            p1(R50FieldAmpSustain): 0,

            p2(R50FieldEnabled): 1,
            p2(R50FieldSourceType): 1,
            p2(R50FieldSampleInstrument): AUValue(Instrument.flute),
            p2(R50FieldCutoff): 5200,
            p2(R50FieldKeyTrack): 0.5,
            p2(R50FieldAmpAttack): 0.05,
            p2(R50FieldAmpSustain): 0.9,
            p2(R50FieldAmpRelease): 0.25,
        ]),

        R50FactoryPreset(name: "◆ Brass Section", values: [
            addr(R50ParamToneStructure): AUValue(Structure.attackSustain),
            addr(R50ParamToneBlendTime): 0.08,
            slot(2, R50FxFieldAlgorithm): 1,
            slot(2, R50FxFieldMix): 0.24,

            p1(R50FieldSourceType): 1,
            p1(R50FieldSampleInstrument): AUValue(Instrument.lipBuzz),
            p1(R50FieldCutoff): 6000,
            p1(R50FieldFilterEnvAmount): 0,
            p1(R50FieldAmpDecay): 0.3,
            p1(R50FieldAmpSustain): 0,

            p2(R50FieldEnabled): 1,
            p2(R50FieldSourceType): 1,
            p2(R50FieldSampleInstrument): AUValue(Instrument.trumpet),
            p2(R50FieldCutoff): 4800,
            p2(R50FieldKeyTrack): 0.5,
            p2(R50FieldFilterEnvAmount): 0.3,
            p2(R50FieldAmpAttack): 0.04,
            p2(R50FieldAmpSustain): 0.9,
            p2(R50FieldAmpRelease): 0.2,
            p2(R50FieldFilterDecay): 0.3,
            p2(R50FieldFilterSustain): 0.6,
        ]),

        R50FactoryPreset(name: "◆ Gong Bath", values: [
            addr(R50ParamToneStructure): AUValue(Structure.attackSustain),
            addr(R50ParamToneBlendTime): 0.20,
            slot(2, R50FxFieldAlgorithm): 1,
            slot(2, R50FxFieldMix): 0.55,
            slot(2, R50FxFieldControl2): reverbDecay(8.0),
            slot(2, R50FxFieldControl3): 0.9,

            p1(R50FieldSourceType): 1,
            p1(R50FieldSampleInstrument): AUValue(Instrument.anvil),
            p1(R50FieldCutoff): 9000,
            p1(R50FieldFilterEnvAmount): 0,
            p1(R50FieldAmpDecay): 0.8,
            p1(R50FieldAmpSustain): 0,

            p2(R50FieldEnabled): 1,
            p2(R50FieldSourceType): 1,
            p2(R50FieldSampleInstrument): AUValue(Instrument.gong),
            p2(R50FieldLevel): 0.7,
            p2(R50FieldCutoff): 4000,
            p2(R50FieldKeyTrack): 0.7,
            p2(R50FieldAmpAttack): 0.15,
            p2(R50FieldAmpDecay): 4.0,
            p2(R50FieldAmpSustain): 0.3,
            p2(R50FieldAmpRelease): 2.5,
        ]),

        R50FactoryPreset(name: "◆ Nylon Pluck", values: [
            addr(R50ParamToneStructure): AUValue(Structure.attackSustain),
            addr(R50ParamToneBlendTime): 0.04,
            slot(2, R50FxFieldAlgorithm): 1,
            slot(2, R50FxFieldMix): 0.22,

            p1(R50FieldSourceType): 1,
            p1(R50FieldSampleInstrument): AUValue(Instrument.pick),
            p1(R50FieldCutoff): 11000,
            p1(R50FieldFilterEnvAmount): 0,
            p1(R50FieldAmpDecay): 0.15,
            p1(R50FieldAmpSustain): 0,

            p2(R50FieldEnabled): 1,
            p2(R50FieldSourceType): 1,
            p2(R50FieldSampleInstrument): AUValue(Instrument.nylonGuitar),
            p2(R50FieldCutoff): 4200,
            p2(R50FieldKeyTrack): 0.6,
            p2(R50FieldFilterEnvAmount): 0.35,
            p2(R50FieldAmpDecay): 1.6,
            p2(R50FieldAmpSustain): 0.15,
            p2(R50FieldAmpRelease): 0.5,
            p2(R50FieldFilterDecay): 0.8,
            p2(R50FieldFilterSustain): 0.15,
        ]),

        R50FactoryPreset(name: "◆ Pizzagogo", values: [
            addr(R50ParamToneStructure): AUValue(Structure.attackSustain),
            addr(R50ParamToneBlendTime): 0.05,
            slot(2, R50FxFieldAlgorithm): 1,
            slot(2, R50FxFieldMix): 0.34,
            slot(2, R50FxFieldControl3): 0.7,

            p1(R50FieldSourceType): 1,
            p1(R50FieldSampleInstrument): AUValue(Instrument.pizzicato),
            p1(R50FieldCutoff): 9000,
            p1(R50FieldFilterEnvAmount): 0,
            p1(R50FieldAmpDecay): 0.4,
            p1(R50FieldAmpSustain): 0,

            p2(R50FieldEnabled): 1,
            p2(R50FieldSourceType): 1,
            p2(R50FieldSampleInstrument): AUValue(Instrument.strings),
            p2(R50FieldLevel): 0.55,
            p2(R50FieldCutoff): 3600,
            p2(R50FieldKeyTrack): 0.5,
            p2(R50FieldAmpAttack): 0.02,
            p2(R50FieldAmpDecay): 1.2,
            p2(R50FieldAmpSustain): 0.1,
            p2(R50FieldAmpRelease): 0.4,
        ]),

        R50FactoryPreset(name: "◆ Spectrum Bell", values: [
            addr(R50ParamToneStructure): AUValue(Structure.attackSustain),
            addr(R50ParamToneBlendTime): 0.06,
            slot(2, R50FxFieldAlgorithm): 1,
            slot(2, R50FxFieldMix): 0.40,
            slot(2, R50FxFieldControl2): reverbDecay(5.0),

            p1(R50FieldSourceType): 1,
            p1(R50FieldSampleInstrument): AUValue(Instrument.tineStrike),
            p1(R50FieldCutoff): 12000,
            p1(R50FieldFilterEnvAmount): 0,
            p1(R50FieldAmpDecay): 0.3,
            p1(R50FieldAmpSustain): 0,

            p2(R50FieldEnabled): 1,
            p2(R50FieldSourceType): 1,
            p2(R50FieldSampleInstrument): AUValue(Instrument.spectrum4),
            p2(R50FieldCutoff): 7000,
            p2(R50FieldKeyTrack): 0.6,
            p2(R50FieldAmpDecay): 2.5,
            p2(R50FieldAmpSustain): 0.2,
            p2(R50FieldAmpRelease): 1.2,
        ]),

        R50FactoryPreset(name: "◆ Spectral Glass", values: [
            addr(R50ParamToneStructure): AUValue(Structure.mix),
            slot(0, R50FxFieldAlgorithm): 7,
            slot(0, R50FxFieldMix): 0.30,
            slot(2, R50FxFieldAlgorithm): 1,
            slot(2, R50FxFieldMix): 0.45,
            slot(2, R50FxFieldControl3): 0.8,

            p1(R50FieldSourceType): 1,
            p1(R50FieldSampleInstrument): AUValue(Instrument.spectrum3),
            p1(R50FieldCutoff): 8000,
            p1(R50FieldKeyTrack): 0.5,
            p1(R50FieldAmpAttack): 0.3,
            p1(R50FieldAmpSustain): 0.85,
            p1(R50FieldAmpRelease): 1.4,

            p2(R50FieldEnabled): 1,
            p2(R50FieldSourceType): 1,
            p2(R50FieldSampleInstrument): AUValue(Instrument.spectrum7),
            p2(R50FieldLevel): 0.5,
            p2(R50FieldOctave): -1,
            p2(R50FieldCutoff): 5000,
            p2(R50FieldAmpAttack): 0.5,
            p2(R50FieldAmpSustain): 0.8,
            p2(R50FieldAmpRelease): 1.8,
        ]),

        R50FactoryPreset(name: "Glass Bell", values: [
            slot(1, R50FxFieldAlgorithm): 6,
            slot(1, R50FxFieldMix): 0.22,
            slot(2, R50FxFieldAlgorithm): 1,
            slot(2, R50FxFieldMix): 0.34,
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

    static let all: [R50FactoryPreset] = instrumentPresets + effectShowcases

    /// Small, deliberately obvious examples for the effect families that were
    /// not represented by the original factory patches.
    private static let effectShowcases: [R50FactoryPreset] = [
        rackShowcase("FX • Tremolo", algorithm: 11, mix: 1, controls: [
            0: 0.48, 1: 0.75, 2: 0.5, 3: 0.5, 4: 0.5
        ]),
        rackShowcase("FX • Rotary", algorithm: 12, mix: 1, controls: [
            0: 0.55, 1: 0.48, 2: 0.5, 3: 0.45,
            4: 0.65, 5: 0.7, 6: 0.5
        ], mode: 1),
        rackShowcase("FX • Digital EQ", algorithm: 13, mix: 1, controls: [
            0: 0.42, 1: 0.45, 2: 0.62, 5: 0.58, 6: 0.58
        ]),
        rackShowcase("FX • Overdrive", algorithm: 14, mix: 0.72, controls: [
            0: 0.45, 2: 0.62, 5: 0.68
        ], mode: 1),
        rackShowcase("FX • Exciter", algorithm: 16, mix: 0.55, controls: [
            0: 0.45, 1: 0.38, 2: 0.62, 3: 0.48
        ])
    ]

    private static func rackShowcase(
        _ name: String, algorithm: AUValue, mix: AUValue,
        controls: [Int: AUValue], mode: AUValue = 0
    ) -> R50FactoryPreset {
        var values: [AUParameterAddress: AUValue] = [
            p1(R50FieldDryLevel): 0,
            p1(R50FieldSend1): 1,
            slot(0, R50FxFieldAlgorithm): algorithm,
            slot(0, R50FxFieldMix): mix,
            slot(0, R50FxFieldMode1): mode,
            addr(R50ParamCutoff): 5200,
            addr(R50ParamAmpRelease): 0.55
        ]
        for (index, value) in controls {
            let field = R50FxSlotField(
                rawValue: R50FxFieldControl1.rawValue + UInt32(index))
            values[slot(0, field)] = value
        }
        return R50FactoryPreset(name: name, values: values)
    }

    private static func chorusRate(_ hertz: AUValue) -> AUValue {
        AUValue(log(max(0.05, hertz) / 0.05) / log(160.0))
    }
    private static func delayTime(_ seconds: AUValue) -> AUValue {
        AUValue(log(max(0.001, seconds) / 0.001) / log(2000.0))
    }
    private static func delayFeedback(_ amount: AUValue) -> AUValue {
        (amount + 0.95) / 1.9
    }
    private static func reverbDecay(_ seconds: AUValue) -> AUValue {
        AUValue(log(max(0.4, seconds) / 0.4) / log(30.0))
    }

    private static func addr(_ p: R50Param) -> AUParameterAddress {
        AUParameterAddress(p.rawValue)
    }
    private static func slot(_ index: Int,
                             _ field: R50FxSlotField) -> AUParameterAddress {
        AUParameterAddress(r50FxSlotParam(Int32(index), field).rawValue)
    }

    /// Address of a field on a given Partial. Partial 1's fields resolve to the
    /// original addresses, so the older presets above stay valid unchanged.
    private static func p1(_ field: R50PartialField) -> AUParameterAddress {
        AUParameterAddress(r50PartialParam(0, field).rawValue)
    }
    private static func p2(_ field: R50PartialField) -> AUParameterAddress {
        AUParameterAddress(r50PartialParam(1, field).rawValue)
    }

    /// Factory instrument indices, in the order buildFactoryContent() publishes
    /// them in R50SampleFactory.hpp.
    private enum Instrument {
        static let choir = 0, strings = 1, warmPad = 2, glassPad = 3
        static let voiceOoh = 4, flute = 5, trumpet = 6, organ = 7
        static let nylonGuitar = 8, piano = 9, gong = 10, nasty = 11, fatBlock = 12
        // Attacks follow the thirteen sustains, in buildFactoryContent order.
        static let mallet = 13, pluck = 14, chiff = 15, noiseBurst = 16
        static let tineStrike = 17, marimba = 18, vibraphone = 19, xylophone = 20
        static let kalimba = 21, slapBass = 22, pullBass = 23, pick = 24
        static let pianoHammer = 25, anvil = 26, taikoDrum = 27, lipBuzz = 28
        static let breath = 29, bowScrape = 30
        // Registered after everything above, so these indices are append-only.
        static let spectrum1 = 31, spectrum2 = 32, spectrum3 = 33
        static let spectrum4 = 34, spectrum5 = 35, spectrum6 = 36
        static let spectrum7 = 37, spectrum8 = 38, spectrum9 = 39
        static let pizzicato = 40
    }

    private enum Structure {
        static let mix = 0, ring = 1, attackSustain = 2
        static let velocityCrossfade = 3, keyCrossfade = 4
    }
}
