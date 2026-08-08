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
        a(SynthParamOscWaveform):   "Oscillator 1 waveform — saw, square, pulse, wavetable, sine, or triangle.",
        a(SynthParamWavetable):     "Oscillator 1 wavetable. Oscillator 2 follows it while Table Link is on.",
        a(SynthParamWTTable2):      "Oscillator 2 wavetable when Table Link is off.",
        a(SynthParamWTTable2Link):  "Table Link — Oscillator 2 follows Oscillator 1's wavetable selection when on.",
        a(SynthParamWTFrame):       "Oscillator 1 wavetable timbre morph — sweeps through the frames of the table.",
        a(SynthParamWTFrame2):      "Oscillator 2 wavetable timbre morph when Frame Link is off.",
        a(SynthParamWTFrame2Link):  "Frame Link — Oscillator 2 follows Oscillator 1's frame and frame modulation when on.",
        a(SynthParamWTLiveness):    "Wavetable liveness — slow phase drift so held notes shimmer",
        a(SynthParamWTFrameEnv):    "Filter envelope → WT frame (bipolar) — sweeps the wavetable timbre on each note.",
        a(SynthParamWTResolution):  "Wavetable character — Clean, 12-bit or 8-bit amplitude grain, or Vintage 8-bit with stepped table lookup.",
        a(SynthParamWTSmooth):      "Wavetable phase interpolation — 1 is clean and linear; 0 is a stepped vintage lookup.",
        a(SynthParamOscPulseWidth): "Oscillator 1 pulse width",
        a(SynthParamOsc2PulseWidth):"Oscillator 2 pulse width",
        a(SynthParamOscPhaseSpread):"Per-voice start-phase randomisation — 0 = tight/hard, 1 = loose analog.",
        a(SynthParamStereoSpread):  "Voice-card panning — 0 = mono, 1 = full alternating stereo stage.",
        a(SynthParamUnison):        "Two-card polyphonic unison — pairs two voices per note.",
        a(SynthParamUnisonDetune):  "Symmetrical unison detune, from tight to ±50 cents.",
        a(SynthParamOctave):        "Oscillator 1 octave.",
        a(SynthParamOsc1Level):     "Osc 1 level in the mixer",
        a(SynthParamOsc2Level):     "Osc 2 level in the mixer",
        a(SynthParamNoiseLevel):    "White-noise level in the mixer.",
        a(SynthParamSubOscLevel):   "Analog Osc 1 sub oscillator — a band-limited square wave one octave below Osc 1.",
        a(SynthParamRingModLevel):  "Ring modulation level — the AC-coupled bipolar product of Osc 1 and Osc 2, including wavetables.",
        a(SynthParamRingModCharacter): "Ring-mod character — Clean is a precise multiplier; Diode adds the nonlinear response of a soft diode bridge.",

        a(SynthParamOsc2Waveform):  "Oscillator 2 waveform — saw, square, pulse, WT, sine, or triangle. WT disables cross-mod and sync, but supports ring modulation.",
        a(SynthParamOsc2Octave):    "Oscillator 2 octave.",
        a(SynthParamOsc2Semitone):  "Osc 2 coarse tune. Under cross-mod this sets the FM ratio",
        a(SynthParamOsc2Detune):    "Osc 2 fine detune, in cents — thickens layers, or fine-tunes the FM ratio.",
        a(SynthParamOsc2Sync):      "Hard-sync osc 2 to osc 1 (tearing sync tone; raise Osc 2 level to hear it).",
        a(SynthParamOscCrossMod):   "Cross-mod: osc 2 frequency-modulates osc 1 (FM)",
        a(SynthParamOscCrossModTZ): "Cross-mod type — Exp (thick FM) or TZ through-zero (clean, DX-like).",
        a(SynthParamOsc2PitchEnv):  "Filter envelope → oscillator 2 pitch. Great for sync sweeps and drum pitch-drops.",

        a(SynthParamFilterCutoff):    "Low-pass filter cutoff frequency.",
        a(SynthParamFilterResonance): "Filter resonance / emphasis at the cutoff.",
        a(SynthParamFilterDrive):     "Overdrive / distortion driven into the filter.",
        a(SynthParamFilterEnvAmount): "How much the filter envelope moves the cutoff (bipolar).",
        a(SynthParamFilterSlope):     "Filter slope — 12 dB/oct (2-pole) or 24 dB/oct (4-pole).",
        a(SynthParamFilterMode):      "Filter response — low-pass, band-pass, or high-pass.",
        a(SynthParamFilterKeyTrack):  "Cutoff follows the note pitch (key tracking).",

        a(SynthParamAmpAttack):  "Amplitude envelope attack time.",
        a(SynthParamAmpDecay):   "Amplitude envelope decay time.",
        a(SynthParamAmpSustain): "Amplitude envelope sustain level.",
        a(SynthParamAmpRelease): "Amplitude envelope release time.",

        a(SynthParamFilterAttack):  "Filter envelope attack time.",
        a(SynthParamFilterDecay):   "Filter envelope decay time.",
        a(SynthParamFilterSustain): "Filter envelope sustain level.",
        a(SynthParamFilterRelease): "Filter envelope release time.",

        a(SynthParamLFOWaveform):     "LFO 1 shape: sine, square, rising/falling saw, or sample-and-hold.",
        a(SynthParamLFOKeyTrigger):   "Key Trig: reset the LFO phase on every key press (vs. free-running).",
        a(SynthParamLFORate):         "LFO speed.",
        a(SynthParamLFODelay):        "Delay before the LFO fades in, measured from each key press.",
        a(SynthParamLFOToOscFreq):    "LFO → oscillator pitch (vibrato).",
        a(SynthParamLFOToPulseWidth): "LFO → pulse width (PWM).",
        a(SynthParamLFOToCutoff):     "LFO → filter cutoff.",
        a(SynthParamLFOToResonance):  "LFO → filter resonance.",
        a(SynthParamLFOToCrossMod):   "LFO → cross-mod amount (evolving FM).",
        a(SynthParamLFOToWTFrame):    "LFO → WT frame — animates the wavetable timbre morph.",
        a(SynthParamLFO2Waveform):    "LFO 2 shape: sine, square, rising/falling saw, or sample-and-hold.",
        a(SynthParamLFO2Rate):        "LFO 2 speed. Route it to any destination in the Mod Matrix.",
        a(SynthParamLFO1Polarity):    "LFO 1 output range: bipolar −1…+1 or unipolar 0…1.",
        a(SynthParamLFO1Phase):       "LFO 1 starting phase offset across one cycle.",
        a(SynthParamLFO2Delay):       "Delay before LFO 2 fades in after each note.",
        a(SynthParamLFO2Polarity):    "LFO 2 output range: bipolar −1…+1 or unipolar 0…1.",
        a(SynthParamLFO2Phase):       "LFO 2 phase offset across one cycle.",
        a(SynthParamLFO3Waveform):    "LFO 3 shape: sine, square, rising/falling saw, or sample-and-hold.",
        a(SynthParamLFO3Rate):        "LFO 3 speed. Route it in the Mod Matrix or select it for vibrato.",
        a(SynthParamLFO3Delay):       "Delay before LFO 3 fades in after each note.",
        a(SynthParamLFO3Polarity):    "LFO 3 output range: bipolar −1…+1 or unipolar 0…1.",
        a(SynthParamLFO3Phase):       "LFO 3 phase offset across one cycle.",
        a(SynthParamVibratoLFO):      "Selects which of the three LFOs drives the global Vibrato amount.",
        a(SynthParamLFO1Mode):        "LFO 1 run mode: Loop (free-running), Trig (restart on each key press), or 1-Shot (one cycle on key press, then hold).",
        a(SynthParamLFO2Mode):        "LFO 2 run mode: Loop (free-running), Trig (restart on each key press), or 1-Shot (one cycle on key press, then hold).",
        a(SynthParamLFO3Mode):        "LFO 3 run mode: Loop (free-running), Trig (restart on each key press), or 1-Shot (one cycle on key press, then hold).",

        a(SynthParamArpOn):           "Arpeggiator on/off. Held notes are played one at a time by the arp clock.",
        a(SynthParamArpMode):         "Arp direction — Up, Down, Up/Down (ping-pong), or Random.",
        a(SynthParamArpOctaves):      "Arp octave range — repeats the held chord across 1–4 octaves.",
        a(SynthParamArpRate):         "Arp step division synced to the host tempo; T means triplet and a dot means dotted.",
        a(SynthParamArpGate):         "Arp note length — fraction of each step the note sounds (staccato ↔ legato).",
        a(SynthParamArpHold):         "Latch: keep arpeggiating after you release the keys. Press new keys to replace.",
        a(SynthParamChordOn):          "Expand each incoming key into a chord before it reaches the arpeggiator.",
        a(SynthParamChordType):        "Generated chord quality: triads, sevenths, suspended, diminished, or augmented.",
        a(SynthParamChordInversion):   "Moves the lowest chord tones up an octave; unavailable inversions use the highest valid voicing.",

        a(SynthParamCompressorOn):        "Stereo-linked compressor at the very start of the effects chain.",
        a(SynthParamCompressorThreshold): "Level above which compression begins, with a soft knee around the threshold.",
        a(SynthParamCompressorRatio):     "Compression ratio: 1:1 is neutral; higher values control peaks more strongly.",
        a(SynthParamCompressorAttack):    "How quickly gain reduction responds to a signal above the threshold.",
        a(SynthParamCompressorRelease):   "How quickly gain recovers after the signal drops below the threshold.",
        a(SynthParamCompressorMakeup):    "Output gain after compression, used to restore perceived level.",
        a(SynthParamChorusMix):       "Stereo chorus wet/dry mix.",
        a(SynthParamChorusRate):      "Chorus modulation cycle synced to the host tempo.",
        a(SynthParamChorusDepth):     "Chorus delay-modulation depth and stereo spread.",
        a(SynthParamDelayMix):        "Stereo delay wet/dry mix.",
        a(SynthParamDelayTime):       "Delay division synced to the host tempo. Changes crossfade without pitch jumps.",
        a(SynthParamDelayFeedback):   "Echo regeneration; high values produce long, saturated tails.",
        a(SynthParamDelayTone):       "Brightness of successive echoes.",
        a(SynthParamDelayPingPong):   "Stereo cross-feedback: 0 = straight, 1 = full ping-pong.",
        a(SynthParamReverbMix):       "Reverb wet/dry mix with an equal-power response.",
        a(SynthParamReverbSize):      "Scales the reverb room and its modal spacing.",
        a(SynthParamReverbDecay):     "Reverb RT60 — time for the tail to decay by 60 dB.",
        a(SynthParamReverbTone):      "High-frequency damping in the reverb tail: dark to bright.",
        a(SynthParamReverbPreDelay):  "Delay before the reverb begins, separating the dry attack from the room.",

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
