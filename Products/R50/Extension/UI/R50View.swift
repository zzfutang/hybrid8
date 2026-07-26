//
//  R50View.swift
//  The R50 editor. A fixed-size fascia laid out at R50Layout dimensions and
//  scaled uniformly to fit whatever size the host gives us, so the panel keeps
//  its proportions in Logic, in the standalone app and at any zoom.
//

import SwiftUI
import UniformTypeIdentifiers

enum R50Page: String, CaseIterable {
    case synth = "Synth"
    case samples = "Samples"
}

struct R50View: View {
    @ObservedObject var model: R50ParameterModel
    @ObservedObject var samples: R50SampleStore
    @State private var page: R50Page = .synth
    @State private var showingImporter = false

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
            switch page {
            case .synth:   synthPage
            case .samples: samplePage
            }
            footer
        }
        .padding(16)
        .background(
            LinearGradient(colors: [R50Palette.chassisTop, R50Palette.chassisLow],
                           startPoint: .top, endPoint: .bottom))
    }

    // MARK: - Pages

    private var synthPage: some View {
        HStack(alignment: .top, spacing: 10) {
            oscillator.frame(width: 190)
            noise.frame(width: 230)
            filter.frame(width: 300)
            ampEnvelope.frame(width: 190)
            filterEnvelope.frame(width: 190)
        }
        .frame(height: 300)
    }

    private var samplePage: some View {
        HStack(alignment: .top, spacing: 10) {
            R50Panel(title: "Instruments") {
                VStack(alignment: .leading, spacing: 8) {
                    instrumentGrid
                    HStack(spacing: 8) {
                        actionButton("IMPORT…") { showingImporter = true }
                        actionButton("DELETE") {
                            if let entry = samples.entries
                                .first(where: { $0.index == samples.selectedIndex }) {
                                samples.delete(entry)
                            }
                        }
                        if samples.isImporting {
                            Text("LOADING…")
                                .font(.system(size: 8, weight: .medium,
                                              design: .monospaced))
                                .foregroundColor(R50Palette.glow)
                        }
                    }
                }
            }
            .frame(width: 620)

            R50Panel(title: "Playback") {
                VStack(alignment: .leading, spacing: 14) {
                    HStack(spacing: 4) {
                        R50Knob(title: "Start", address: R50ParamSampleStart,
                                model: model)
                        R50Knob(title: "Octave", address: R50ParamOctave,
                                model: model)
                    }
                    Text(statusText)
                        .font(.system(size: 8, weight: .medium, design: .monospaced))
                        .foregroundColor(R50Palette.engrave)
                        .lineLimit(4)
                        .fixedSize(horizontal: false, vertical: true)
                }
            }
            .frame(width: 270)
        }
        .frame(height: 300)
        .fileImporter(isPresented: $showingImporter,
                      allowedContentTypes: [.wav, .aiff, .audio],
                      allowsMultipleSelection: false) { result in
            if case let .success(urls) = result, let url = urls.first {
                samples.importAudioFile(at: url)
            }
        }
    }

    private var instrumentGrid: some View {
        let columns = 4
        let rows = max(1, Int(ceil(Double(samples.entries.count) / Double(columns))))
        return VStack(spacing: 1) {
            ForEach(0..<rows, id: \.self) { row in
                HStack(spacing: 1) {
                    ForEach(0..<columns, id: \.self) { column in
                        let index = row * columns + column
                        if index < samples.entries.count {
                            instrumentCell(samples.entries[index])
                        } else {
                            Color.clear.frame(maxWidth: .infinity, minHeight: 22)
                        }
                    }
                }
            }
        }
        .clipShape(RoundedRectangle(cornerRadius: 2))
        .overlay(RoundedRectangle(cornerRadius: 2)
            .stroke(Color(white: 0.32), lineWidth: 1))
    }

    private func instrumentCell(_ entry: SampleEntry) -> some View {
        let selected = entry.index == samples.selectedIndex
        return Text(entry.name)
            .font(.system(size: 9, weight: .semibold, design: .monospaced))
            .foregroundColor(selected ? Color.black
                                      : (entry.isFactory ? R50Palette.legend
                                                         : R50Palette.glow))
            .lineLimit(1)
            .minimumScaleFactor(0.6)
            .frame(maxWidth: .infinity, minHeight: 22)
            .background(selected ? R50Palette.glow : Color(white: 0.13))
            .contentShape(Rectangle())
            .onTapGesture { samples.select(entry) }
    }

    private func actionButton(_ title: String,
                              action: @escaping () -> Void) -> some View {
        Text(title)
            .font(.system(size: 9, weight: .semibold, design: .monospaced))
            .foregroundColor(R50Palette.legend)
            .padding(.horizontal, 10)
            .padding(.vertical, 5)
            .background(Color(white: 0.15))
            .overlay(RoundedRectangle(cornerRadius: 2)
                .stroke(Color(white: 0.32), lineWidth: 1))
            .contentShape(Rectangle())
            .onTapGesture(perform: action)
    }

    private var statusText: String {
        if let message = samples.errorMessage { return message }
        let sourceIsSample = model.value(R50ParamSourceType) >= 0.5
        return sourceIsSample
            ? "Sample source active. Factory instruments are generated at startup."
            : "Set Source to Sample on the Synth page to hear these."
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

            pageTabs
            Spacer()
            presetBrowser
            Spacer()
            R50Meter(level: model.outputLevel)
        }
        .padding(.horizontal, 4)
    }

    private var pageTabs: some View {
        HStack(spacing: 2) {
            ForEach(R50Page.allCases, id: \.self) { candidate in
                let selected = candidate == page
                Text(candidate.rawValue.uppercased())
                    .font(.system(size: 9, weight: .semibold, design: .monospaced))
                    .tracking(1.2)
                    .foregroundColor(selected ? Color.black : R50Palette.legend)
                    .padding(.horizontal, 12)
                    .padding(.vertical, 6)
                    .background(selected ? R50Palette.glow : Color(white: 0.14))
                    .contentShape(Rectangle())
                    .onTapGesture { page = candidate }
            }
        }
        .clipShape(RoundedRectangle(cornerRadius: 2))
        .overlay(RoundedRectangle(cornerRadius: 2)
            .stroke(Color(white: 0.32), lineWidth: 1))
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
            VStack(alignment: .leading, spacing: 12) {
                R50Selector(title: "Source", address: R50ParamSourceType,
                            options: R50Parameters.sourceTypeNames, model: model)
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
