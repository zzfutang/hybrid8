//
//  R50View.swift
//  The R50 editor. A fixed-size fascia laid out at R50Layout dimensions and
//  scaled uniformly to fit whatever size the host gives us, so the panel keeps
//  its proportions in Logic, in the standalone app and at any zoom.
//

import SwiftUI

struct R50View: View {
    @ObservedObject var model: R50ParameterModel

    var body: some View {
        GeometryReader { geo in
            let scale = min(geo.size.width / R50Layout.width,
                            geo.size.height / R50Layout.height)
            fascia
                .frame(width: R50Layout.width, height: R50Layout.height)
                .scaleEffect(scale, anchor: .center)
                .frame(width: geo.size.width, height: geo.size.height)
        }
        .background(R50Palette.chassisLow)
    }

    private var fascia: some View {
        VStack(spacing: 12) {
            header
            HStack(alignment: .top, spacing: 10) {
                oscillator.frame(width: 190)
                noise.frame(width: 230)
                filter.frame(width: 300)
                ampEnvelope.frame(width: 190)
                filterEnvelope.frame(width: 190)
            }
            .frame(height: 300)
            footer
        }
        .padding(16)
        .background(
            LinearGradient(colors: [R50Palette.chassisTop, R50Palette.chassisLow],
                           startPoint: .top, endPoint: .bottom))
    }

    // MARK: - Header

    private var header: some View {
        HStack(alignment: .center, spacing: 16) {
            VStack(alignment: .leading, spacing: 0) {
                Text("R50")
                    .font(.system(size: 30, weight: .heavy, design: .monospaced))
                    .tracking(4)
                    .foregroundColor(R50Palette.legend)
                Text("Rytell · monotimbral 8-voice")
                    .font(.system(size: 8, weight: .medium, design: .monospaced))
                    .tracking(1.2)
                    .foregroundColor(R50Palette.engrave)
            }

            Spacer()
            presetBrowser
            Spacer()
            R50Meter(level: model.outputLevel)
        }
        .padding(.horizontal, 4)
    }

    private var presetBrowser: some View {
        HStack(spacing: 8) {
            stepButton("◀") { step(-1) }
            Text(R50FactoryPresets.all[safe: model.presetIndex]?.name ?? "Init")
                .font(.system(size: 11, weight: .semibold, design: .monospaced))
                .foregroundColor(R50Palette.glow)
                .frame(width: 170, height: 24)
                .background(R50Palette.track)
                .overlay(RoundedRectangle(cornerRadius: 2)
                    .stroke(Color(white: 0.30), lineWidth: 1))
            stepButton("▶") { step(1) }
        }
    }

    private func stepButton(_ glyph: String, action: @escaping () -> Void) -> some View {
        Text(glyph)
            .font(.system(size: 10, weight: .bold))
            .foregroundColor(R50Palette.legend)
            .frame(width: 22, height: 24)
            .background(Color(white: 0.15))
            .overlay(RoundedRectangle(cornerRadius: 2)
                .stroke(Color(white: 0.30), lineWidth: 1))
            .contentShape(Rectangle())
            .onTapGesture(perform: action)
    }

    private func step(_ delta: Int) {
        let count = R50FactoryPresets.all.count
        guard count > 0 else { return }
        model.applyPreset((model.presetIndex + delta + count) % count)
    }

    // MARK: - Panels

    private var oscillator: some View {
        R50Panel(title: "Oscillator") {
            VStack(alignment: .leading, spacing: 14) {
                R50WaveGrid(title: "Wave", address: R50ParamOscWave,
                            options: R50Parameters.waveformNames,
                            columns: 3, model: model)
                HStack(spacing: 4) {
                    R50Knob(title: "Width", address: R50ParamPulseWidth, model: model)
                    R50Knob(title: "Octave", address: R50ParamOctave, model: model)
                }
            }
        }
    }

    private var noise: some View {
        R50Panel(title: "Noise") {
            VStack(alignment: .leading, spacing: 12) {
                R50WaveGrid(title: "Spectrum", address: R50ParamNoiseSpectrum,
                            options: R50Parameters.noiseSpectrumNames,
                            columns: 3, model: model)
                HStack(spacing: 4) {
                    R50Knob(title: "Mix", address: R50ParamNoiseMix, model: model)
                    R50Knob(title: "Tone", address: R50ParamNoiseTone, model: model)
                    R50Knob(title: "Rate", address: R50ParamNoiseRate, model: model)
                }
                R50Selector(title: "Tone / Rate Source",
                            address: R50ParamNoisePitchTrack,
                            options: R50Parameters.trackNames, model: model)
            }
        }
    }

    private var filter: some View {
        R50Panel(title: "Filter") {
            VStack(alignment: .leading, spacing: 14) {
                R50Selector(title: "Slope", address: R50ParamSlope,
                            options: R50Parameters.slopeNames, model: model)
                HStack(spacing: 4) {
                    R50Knob(title: "Cutoff", address: R50ParamCutoff, model: model)
                    R50Knob(title: "Reso", address: R50ParamResonance, model: model)
                    R50Knob(title: "Drive", address: R50ParamDrive, model: model)
                    R50Knob(title: "Key Trk", address: R50ParamKeyTrack, model: model)
                }
                HStack(spacing: 4) {
                    R50Knob(title: "Env Amt", address: R50ParamFilterEnvAmount, model: model)
                }
            }
        }
    }

    private var ampEnvelope: some View {
        R50Panel(title: "Amp Envelope") {
            envelopeGrid(attack: R50ParamAmpAttack, decay: R50ParamAmpDecay,
                         sustain: R50ParamAmpSustain, release: R50ParamAmpRelease)
        }
    }

    private var filterEnvelope: some View {
        R50Panel(title: "Filter Envelope") {
            envelopeGrid(attack: R50ParamFilterAttack, decay: R50ParamFilterDecay,
                         sustain: R50ParamFilterSustain, release: R50ParamFilterRelease)
        }
    }

    private func envelopeGrid(attack: R50Param, decay: R50Param,
                              sustain: R50Param, release: R50Param) -> some View {
        VStack(spacing: 12) {
            HStack(spacing: 4) {
                R50Knob(title: "Attack", address: attack, model: model)
                R50Knob(title: "Decay", address: decay, model: model)
            }
            HStack(spacing: 4) {
                R50Knob(title: "Sustain", address: sustain, model: model)
                R50Knob(title: "Release", address: release, model: model)
            }
        }
    }

    // MARK: - Footer

    private var footer: some View {
        HStack(spacing: 18) {
            R50Knob(title: "Master", address: R50ParamMasterGain, model: model)
            R50Knob(title: "Bend", address: R50ParamPitchBendRange, model: model)
            Spacer()
            Text("8-VOICE · BAND-LIMITED PCM + NOISE · ZDF LADDER")
                .font(.system(size: 8, weight: .medium, design: .monospaced))
                .tracking(1.4)
                .foregroundColor(R50Palette.glowDim)
        }
        .padding(.horizontal, 8)
    }
}

private extension Array {
    subscript(safe index: Int) -> Element? {
        indices.contains(index) ? self[index] : nil
    }
}
