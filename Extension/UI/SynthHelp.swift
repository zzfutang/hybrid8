//
//  SynthHelp.swift
//  Hover-help: a shared model that controls update on mouse-over, plus a
//  concise description for every parameter shown in the bottom-left help bar.
//

import SwiftUI
import AudioToolbox

final class HelpModel: ObservableObject {
    @Published var text: String = ""
    func set(_ t: String) { text = t }
    func clear(ifMatches t: String) { if text == t { text = "" } }
}

enum SynthHelp {
    private static let table: [AUParameterAddress: String] = [
        a(SynthParamOscWaveform):   "Oscillator 1 waveform — saw, square, pulse, or WT (wavetable).",
        a(SynthParamWavetable):     "Wavetable to use in WT mode: Harmonic, FM, Choir or Metallic (shared by both oscillators).",
        a(SynthParamWTFrame):       "Wavetable timbre morph — sweeps through the frames of the table.",
        a(SynthParamWTLiveness):    "Wavetable liveness — slow phase drift so held notes shimmer instead of looping.",
        a(SynthParamWTFrameEnv):    "Filter envelope → WT frame (bipolar) — sweeps the wavetable timbre on each note.",
        a(SynthParamOscPulseWidth): "Oscillator 1 pulse width (used by the pulse wave; a PWM target).",
        a(SynthParamOsc2PulseWidth):"Oscillator 2 pulse width (independent from oscillator 1).",
        a(SynthParamOscPhaseSpread):"Per-voice start-phase randomisation — 0 = tight/hard, 1 = loose analog.",
        a(SynthParamOctave):        "Oscillator 1 octave.",
        a(SynthParamOsc1Level):     "Osc 1 level in the mixer (osc 1 is the FM carrier).",
        a(SynthParamOsc2Level):     "Osc 2 level in the mixer (osc 2 is the FM modulator / sync slave).",
        a(SynthParamNoiseLevel):    "White-noise level in the mixer.",

        a(SynthParamOsc2Waveform):  "Oscillator 2 waveform (saw, square, pulse or WT). WT disables cross-mod & sync.",
        a(SynthParamOsc2Octave):    "Oscillator 2 octave.",
        a(SynthParamOsc2Semitone):  "Osc 2 coarse tune. Under cross-mod this sets the FM ratio (no pitch shift); otherwise a layered interval.",
        a(SynthParamOsc2Detune):    "Osc 2 fine detune, in cents — thickens layers, or fine-tunes the FM ratio.",
        a(SynthParamOsc2Sync):      "Hard-sync osc 2 to osc 1 (tearing sync tone; raise Osc 2 level to hear it).",
        a(SynthParamOscCrossMod):   "Cross-mod: osc 2 frequency-modulates osc 1 (FM). Osc 1 keeps the played pitch — turn Osc Mix down to hear it.",
        a(SynthParamOscCrossModTZ): "Cross-mod type — Exp (thick FM) or TZ through-zero (clean, DX-like).",
        a(SynthParamOsc2PitchEnv):  "Filter envelope → oscillator 2 pitch. Great for sync sweeps and drum pitch-drops.",

        a(SynthParamFilterCutoff):    "Low-pass filter cutoff frequency.",
        a(SynthParamFilterResonance): "Filter resonance / emphasis at the cutoff.",
        a(SynthParamFilterDrive):     "Overdrive / distortion driven into the filter.",
        a(SynthParamFilterEnvAmount): "How much the filter envelope moves the cutoff (bipolar).",
        a(SynthParamFilterSlope):     "Filter slope — 12 dB/oct (2-pole) or 24 dB/oct (4-pole).",
        a(SynthParamFilterKeyTrack):  "Cutoff follows the note pitch (key tracking).",

        a(SynthParamAmpAttack):  "Amplitude envelope attack time.",
        a(SynthParamAmpDecay):   "Amplitude envelope decay time.",
        a(SynthParamAmpSustain): "Amplitude envelope sustain level.",
        a(SynthParamAmpRelease): "Amplitude envelope release time.",

        a(SynthParamFilterAttack):  "Filter envelope attack time.",
        a(SynthParamFilterDecay):   "Filter envelope decay time.",
        a(SynthParamFilterSustain): "Filter envelope sustain level.",
        a(SynthParamFilterRelease): "Filter envelope release time.",

        a(SynthParamLFOWaveform):     "LFO shape — sine, square or saw.",
        a(SynthParamLFOKeyTrigger):   "Key Trig: reset the LFO phase on every key press (vs. free-running).",
        a(SynthParamLFORate):         "LFO speed.",
        a(SynthParamLFODelay):        "Delay before the LFO fades in, measured from each key press.",
        a(SynthParamLFOToOscFreq):    "LFO → oscillator pitch (vibrato).",
        a(SynthParamLFOToPulseWidth): "LFO → pulse width (PWM).",
        a(SynthParamLFOToCutoff):     "LFO → filter cutoff.",
        a(SynthParamLFOToResonance):  "LFO → filter resonance.",
        a(SynthParamLFOToCrossMod):   "LFO → cross-mod amount (evolving FM).",
        a(SynthParamLFOToWTFrame):    "LFO → WT frame — animates the wavetable timbre morph.",
        a(SynthParamLFO2Waveform):    "LFO 2 shape (sine, square, saw). LFO 2 is a mod-matrix source only.",
        a(SynthParamLFO2Rate):        "LFO 2 speed. Route it to any destination in the Mod Matrix.",

        a(SynthParamArpOn):           "Arpeggiator on/off. Held notes are played one at a time by the arp clock.",
        a(SynthParamArpMode):         "Arp direction — Up, Down, Up/Down (ping-pong), or Random.",
        a(SynthParamArpOctaves):      "Arp octave range — repeats the held chord across 1–4 octaves.",
        a(SynthParamArpRate):         "Arp step division synced to the host tempo; T means triplet and a dot means dotted.",
        a(SynthParamArpGate):         "Arp note length — fraction of each step the note sounds (staccato ↔ legato).",
        a(SynthParamArpHold):         "Latch: keep arpeggiating after you release the keys. Press new keys to replace.",

        a(SynthParamChorusMix):       "Stereo chorus wet/dry mix.",
        a(SynthParamChorusRate):      "Chorus modulation cycle synced to the host tempo.",
        a(SynthParamChorusDepth):     "Chorus delay-modulation depth and stereo spread.",
        a(SynthParamDelayMix):        "Stereo delay wet/dry mix.",
        a(SynthParamDelayTime):       "Delay division synced to the host tempo. Changes crossfade without pitch jumps.",
        a(SynthParamDelayFeedback):   "Echo regeneration; high values produce long, saturated tails.",
        a(SynthParamDelayTone):       "Brightness of successive echoes.",
        a(SynthParamDelayPingPong):   "Stereo cross-feedback: 0 = straight, 1 = full ping-pong.",

        a(SynthParamAnalogAmount):   "Analog character — per-voice drift, detune and filter saturation.",
        a(SynthParamMasterGain):     "Overall output level.",
        a(SynthParamPitchBendRange): "Pitch-bend wheel range, in semitones.",
        a(SynthParamVoiceCount):     "Maximum voices: 1 = monophonic … 8 = full polyphony.",
        a(SynthParamLegato):         "Legato (mono, Voices = 1): overlapping notes glide without retriggering the envelope.",
        a(SynthParamGlideTime):      "Glide (portamento) time — 0 turns glide off.",
        a(SynthParamGlideStart):     "Glide start: a fixed offset in semitones, or 0 to glide from the previous note.",

        a(SynthParamVelToVolume):    "How much key velocity controls volume.",
        a(SynthParamVelToCutoff):    "How much key velocity opens the filter.",
        a(SynthParamVelToResonance): "How much key velocity adds resonance.",
        a(SynthParamVelToDrive):     "How much key velocity adds filter drive.",
    ]

    private static func a(_ p: SynthParam) -> AUParameterAddress {
        AUParameterAddress(p.rawValue)
    }

    static func text(for address: AUParameterAddress) -> String {
        table[address] ?? ""
    }
}
