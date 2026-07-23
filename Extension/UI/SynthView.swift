//
//  SynthView.swift
//  The instrument fascia: wood end-cheeks, a brushed charcoal panel, a branded
//  header with a preset browser, and six accent-coloured sections of knobs and
//  lit selector buttons. Aesthetic: mid-'80s Yamaha × Oberheim.
//

import SwiftUI
import AudioToolbox

struct SynthView: View {
    @ObservedObject var model: ParameterModel
    @StateObject private var presets: PresetStore
    @StateObject private var help = HelpModel()
    @State private var showingSave = false
    @State private var saveName = ""

    init(model: ParameterModel) {
        _model = ObservedObject(wrappedValue: model)
        _presets = StateObject(wrappedValue: PresetStore(model: model))
    }

    var body: some View {
        HStack(spacing: 0) {
            woodCheek
            panel
            woodCheek
        }
        .background(Palette.panelBottom)
        .environmentObject(help)
    }

    // MARK: Wood side panels

    private var woodCheek: some View {
        LinearGradient(colors: [Palette.woodLight, Palette.woodDark, Palette.woodLight],
                       startPoint: .leading, endPoint: .trailing)
            .frame(width: 18)
            .overlay(Rectangle().stroke(Color.black.opacity(0.5), lineWidth: 1))
    }

    // MARK: Main panel

    private var panel: some View {
        VStack(spacing: 10) {
            header
            helpBar               // kept near the top so it stays on-screen as the panel grows
            VStack(spacing: 12) {
                HStack(alignment: .top, spacing: 12) {
                    // Column 1 — Oscillators + Mixer
                    VStack(spacing: 10) {
                        oscillatorPanel.frame(height: 186)
                        osc2Panel.frame(height: 192)
                        mixerPanel.frame(height: 118)
                        Spacer(minLength: 0)
                    }
                    // Column 2 — Envelopes + LFO
                    VStack(spacing: 10) {
                        ampEnvPanel.frame(height: 118)
                        filterEnvPanel.frame(height: 118)
                        lfoPanel.frame(height: 244)
                        Spacer(minLength: 0)
                    }
                    // Column 3 — Filter + Global (Vel Vol lives in Global)
                    VStack(spacing: 10) {
                        filterPanel.frame(height: 226)
                        globalPanel.frame(height: 250)
                        Spacer(minLength: 0)
                    }
                }
                .frame(height: 516)
                HStack(alignment: .top, spacing: 12) {
                    arpPanel.frame(width: 420)
                    effectsPanel.frame(maxWidth: .infinity)
                }
                .frame(height: 112)
                modMatrixPanel.frame(height: 104)
            }
            .id(model.version) // rebuild only the controls on value changes
        }
        .padding(EdgeInsets(top: 22, leading: 16, bottom: 14, trailing: 16))
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .top)
        .background(
            LinearGradient(colors: [Palette.panelTop, Palette.panelBottom],
                           startPoint: .top, endPoint: .bottom)
        )
    }

    // Hover-help readout, bottom-left.
    private var helpBar: some View {
        HStack(spacing: 8) {
            Circle()
                .fill(help.text.isEmpty ? Palette.engraveDim.opacity(0.5) : Palette.lcd)
                .frame(width: 6, height: 6)
            Text(help.text.isEmpty ? "Hover a control for a description." : help.text)
                .font(.system(size: 11, weight: .medium, design: .rounded))
                .foregroundColor(help.text.isEmpty ? Palette.engraveDim : Palette.engrave)
                .lineLimit(1)
                .minimumScaleFactor(0.8)
            Spacer(minLength: 0)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(.horizontal, 12).padding(.vertical, 8)
        .background(
            RoundedRectangle(cornerRadius: 6)
                .fill(Color.black.opacity(0.28))
                .overlay(RoundedRectangle(cornerRadius: 6)
                    .stroke(Color.white.opacity(0.06), lineWidth: 1))
        )
    }

    // MARK: Header + preset browser

    private var header: some View {
        HStack(alignment: .center, spacing: 14) {
            VStack(alignment: .leading, spacing: 2) {
                Text("HYBRID 8")
                    .font(.system(size: 26, weight: .black, design: .rounded))
                    .tracking(2)
                    .foregroundColor(Palette.engrave)
                    .fixedSize()
                Text("ANALOG · WAVETABLE POLYSYNTH")
                    .font(.system(size: 9, weight: .semibold, design: .rounded))
                    .tracking(3)
                    .foregroundColor(Palette.engraveDim)
                    .fixedSize()
            }
            HStack(spacing: 3) {
                ForEach([Palette.lfoAccent, Palette.globalAccent,
                         Palette.filterAccent, Palette.ampAccent], id: \.self) { c in
                    RoundedRectangle(cornerRadius: 1.5).fill(c).frame(width: 16, height: 6)
                }
            }
            Spacer()
            presetBar
        }
        .padding(.bottom, 6)
    }

    private var presetBar: some View {
        HStack(spacing: 8) {
            Text("PATCH")
                .font(.system(size: 9, weight: .semibold, design: .rounded))
                .tracking(1.5)
                .foregroundColor(Palette.engraveDim)

            arrow("chevron.left") { presets.prev() }

            Menu {
                ForEach(presets.categoryGroups) { group in
                    Menu(group.name) {
                        ForEach(group.indices, id: \.self) { i in
                            Button(presets.factory[i].name) { presets.applyFactory(i) }
                        }
                    }
                }
                if !presets.userPresets.isEmpty {
                    Menu("User") {
                        ForEach(presets.userPresets.indices, id: \.self) { i in
                            Button(presets.userPresets[i].name) { presets.applyUser(i) }
                        }
                    }
                }
                Divider()
                Button("Init Patch") { presets.initPatch() }
                if presets.currentIsUser {
                    Button("Delete \u{201C}\(presets.currentName)\u{201D}", role: .destructive) {
                        presets.deleteCurrentUser()
                    }
                }
            } label: {
                HStack(spacing: 6) {
                    Text(presets.currentName)
                        .font(.system(size: 12, weight: .medium, design: .monospaced))
                        .foregroundColor(Palette.lcd)
                        .lineLimit(1)
                    Spacer(minLength: 0)
                    Image(systemName: "chevron.down")
                        .font(.system(size: 8, weight: .bold))
                        .foregroundColor(Palette.lcd.opacity(0.7))
                }
                .padding(.horizontal, 10).padding(.vertical, 7)
                .frame(width: 180)
                .background(lcdBackground)
            }
            .menuStyle(.borderlessButton)
            .fixedSize()

            arrow("chevron.right") { presets.next() }

            pill("SAVE") { saveName = presets.currentName; showingSave = true }
                .popover(isPresented: $showingSave, arrowEdge: .bottom) { savePanel }
        }
    }

    private var savePanel: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text("SAVE PRESET")
                .font(.system(size: 11, weight: .heavy, design: .rounded)).tracking(1)
                .foregroundColor(Palette.engrave)
            TextField("Preset name", text: $saveName)
                .textFieldStyle(.roundedBorder)
                .frame(width: 220)
                .onSubmit { commitSave() }
            HStack {
                Spacer()
                Button("Cancel") { showingSave = false }
                Button("Save") { commitSave() }
                    .keyboardShortcut(.defaultAction)
            }
        }
        .padding(16)
        .frame(width: 260)
    }

    private func commitSave() {
        presets.saveUser(name: saveName)
        showingSave = false
    }

    private var lcdBackground: some View {
        RoundedRectangle(cornerRadius: 5)
            .fill(Color(red: 0.05, green: 0.10, blue: 0.08))
            .overlay(RoundedRectangle(cornerRadius: 5)
                .stroke(Color.black.opacity(0.7), lineWidth: 1))
    }

    private func arrow(_ symbol: String, _ action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Image(systemName: symbol)
                .font(.system(size: 11, weight: .bold))
                .foregroundColor(Palette.engrave)
                .frame(width: 26, height: 30)
                .background(RoundedRectangle(cornerRadius: 5).fill(Color.white.opacity(0.06)))
                .overlay(RoundedRectangle(cornerRadius: 5).stroke(Color.white.opacity(0.12), lineWidth: 1))
        }
        .buttonStyle(.plain)
    }

    private func pill(_ title: String, _ action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Text(title)
                .font(.system(size: 10, weight: .bold, design: .rounded)).tracking(0.5)
                .foregroundColor(Palette.engrave)
                .padding(.horizontal, 12).frame(height: 30)
                .background(RoundedRectangle(cornerRadius: 5).fill(Color.white.opacity(0.06)))
                .overlay(RoundedRectangle(cornerRadius: 5).stroke(Palette.oscAccent.opacity(0.5), lineWidth: 1))
        }
        .buttonStyle(.plain)
    }

    // MARK: Sections

    private var oscillatorPanel: some View {
        let w1 = Int((model.param(SynthParamOscWaveform)?.value ?? 0).rounded())
        let w2 = Int((model.param(SynthParamOsc2Waveform)?.value ?? 0).rounded())
        let isWT1 = w1 == 3
        let anyWT = w1 == 3 || w2 == 3      // WT table/frame/live are shared
        return Panel(title: "Osc 1", accent: Palette.oscAccent, trailing: {
            InlineWaveSelect(SynthParamOscWaveform, ["Saw", "Squ", "Pls", "WT"],
                             model, accent: Palette.oscAccent)
        }) {
            VStack(alignment: .leading, spacing: 8) {
                Selector("Octave", SynthParamOctave,
                         ["-2", "-1", "0", "+1", "+2"], model, accent: Palette.oscAccent)
                Selector("WT Table", SynthParamWavetable, ["Harm", "FM", "Choir", "Metal"],
                         model, accent: Palette.wtAccent)
                    .dimmed(!anyWT)
                HStack(spacing: 0) {
                    Knob("P.Width", SynthParamOscPulseWidth, model, accent: Palette.oscAccent)
                        .dimmed(isWT1).frame(maxWidth: .infinity)
                    Knob("Frame", SynthParamWTFrame, model, accent: Palette.wtAccent)
                        .dimmed(!anyWT).frame(maxWidth: .infinity)
                    Knob("Live", SynthParamWTLiveness, model, accent: Palette.wtAccent)
                        .dimmed(!anyWT).frame(maxWidth: .infinity)
                }
            }
        }
    }

    private var mixerPanel: some View {
        Panel(title: "Mixer", accent: Palette.mixerAccent) {
            HStack(spacing: 0) {
                Knob("Osc 1", SynthParamOsc1Level, model, accent: Palette.mixerAccent).frame(maxWidth: .infinity)
                Knob("Osc 2", SynthParamOsc2Level, model, accent: Palette.mixerAccent).frame(maxWidth: .infinity)
                Knob("Noise", SynthParamNoiseLevel, model, accent: Palette.mixerAccent).frame(maxWidth: .infinity)
            }
        }
    }

    private var osc2Panel: some View {
        let w1 = Int((model.param(SynthParamOscWaveform)?.value ?? 0).rounded())
        let w2 = Int((model.param(SynthParamOsc2Waveform)?.value ?? 0).rounded())
        let isWT2 = w2 == 3
        let anyWT = w1 == 3 || w2 == 3      // cross-mod & sync need both analog
        return Panel(title: "Osc 2", accent: Palette.osc2Accent, trailing: {
            InlineWaveSelect(SynthParamOsc2Waveform, ["Saw", "Squ", "Pls", "WT"],
                             model, accent: Palette.osc2Accent)
        }) {
            VStack(alignment: .leading, spacing: 8) {
                HStack(alignment: .top, spacing: 10) {
                    ToggleButton("Sync", SynthParamOsc2Sync, model, accent: Palette.osc2Accent)
                        .dimmed(anyWT)
                    ToggleButton("TZ", SynthParamOscCrossModTZ, model, accent: Palette.osc2Accent)
                        .dimmed(anyWT)
                    Spacer(minLength: 0)
                }
                Selector("Octave", SynthParamOsc2Octave,
                         ["-2", "-1", "0", "+1", "+2"], model, accent: Palette.osc2Accent)
                HStack(spacing: 0) {
                    Knob("Semi", SynthParamOsc2Semitone, model,
                         accent: Palette.osc2Accent, unit: "", integer: true).frame(maxWidth: .infinity)
                    Knob("Detune", SynthParamOsc2Detune, model, accent: Palette.osc2Accent, unit: "c").frame(maxWidth: .infinity)
                    Knob("P.Width", SynthParamOsc2PulseWidth, model, accent: Palette.osc2Accent)
                        .dimmed(isWT2).frame(maxWidth: .infinity)
                    Knob("X-Mod", SynthParamOscCrossMod, model, accent: Palette.osc2Accent)
                        .dimmed(anyWT).frame(maxWidth: .infinity)
                }
            }
        }
    }

    private var filterPanel: some View {
        Panel(title: "Filter", accent: Palette.filterAccent) {
            VStack(alignment: .leading, spacing: 8) {
                Selector("Slope", SynthParamFilterSlope, ["12dB", "24dB"],
                         model, accent: Palette.filterAccent)
                HStack(spacing: 0) {
                    Knob("Cutoff", SynthParamFilterCutoff, model,
                         accent: Palette.filterAccent, unit: "", log: true).frame(maxWidth: .infinity)
                    Knob("Reso", SynthParamFilterResonance, model, accent: Palette.filterAccent).frame(maxWidth: .infinity)
                    Knob("Drive", SynthParamFilterDrive, model, accent: Palette.filterAccent).frame(maxWidth: .infinity)
                }
                HStack(spacing: 0) {
                    Knob("Env", SynthParamFilterEnvAmount, model, accent: Palette.filterAccent).frame(maxWidth: .infinity)
                    Knob("KeyTrk", SynthParamFilterKeyTrack, model, accent: Palette.filterAccent).frame(maxWidth: .infinity)
                    Color.clear.frame(maxWidth: .infinity)   // keep column alignment
                }
            }
        }
    }

    private var lfoPanel: some View {
        Panel(title: "LFO", accent: Palette.lfoAccent) {
            VStack(alignment: .leading, spacing: 10) {
                // LFO 1 — its Pitch route (vibrato) is hardwired; every other
                // LFO 1 destination is assigned in the Mod Matrix below.
                HStack(alignment: .top, spacing: 10) {
                    Selector("LFO 1 Wave", SynthParamLFOWaveform, ["Sin", "Squ", "Saw"],
                             model, accent: Palette.lfoAccent)
                    ToggleButton("Key Trig", SynthParamLFOKeyTrigger, model, accent: Palette.lfoAccent)
                }
                HStack(spacing: 0) {
                    Knob("Rate", SynthParamLFORate, model,
                         accent: Palette.lfoAccent, unit: "", log: true).frame(maxWidth: .infinity)
                    Knob("Delay", SynthParamLFODelay, model, accent: Palette.lfoAccent, unit: "s").frame(maxWidth: .infinity)
                    Knob("Vibrato", SynthParamLFOToOscFreq, model, accent: Palette.lfoAccent).frame(maxWidth: .infinity)
                }
                Rectangle().fill(Color.white.opacity(0.08)).frame(height: 1)
                // LFO 2 — a matrix-only source.
                HStack(alignment: .top, spacing: 10) {
                    Selector("LFO 2 Wave", SynthParamLFO2Waveform, ["Sin", "Squ", "Saw"],
                             model, accent: Palette.lfoAccent)
                    Knob("LFO 2 Rate", SynthParamLFO2Rate, model,
                         accent: Palette.lfoAccent, unit: "", log: true)
                }
            }
        }
    }

    private var ampEnvPanel: some View {
        Panel(title: "Amp Envelope", accent: Palette.ampAccent) {
            HStack(spacing: 0) {
                Knob("Attack", SynthParamAmpAttack, model, accent: Palette.ampAccent, timeMapped: true).frame(maxWidth: .infinity)
                Knob("Decay", SynthParamAmpDecay, model, accent: Palette.ampAccent, timeMapped: true).frame(maxWidth: .infinity)
                Knob("Sustain", SynthParamAmpSustain, model, accent: Palette.ampAccent).frame(maxWidth: .infinity)
                Knob("Release", SynthParamAmpRelease, model, accent: Palette.ampAccent, timeMapped: true).frame(maxWidth: .infinity)
            }
        }
    }

    private var filterEnvPanel: some View {
        Panel(title: "Filter Envelope", accent: Palette.filtEnvAccent) {
            HStack(spacing: 0) {
                Knob("Attack", SynthParamFilterAttack, model, accent: Palette.filtEnvAccent, timeMapped: true).frame(maxWidth: .infinity)
                Knob("Decay", SynthParamFilterDecay, model, accent: Palette.filtEnvAccent, timeMapped: true).frame(maxWidth: .infinity)
                Knob("Sustain", SynthParamFilterSustain, model, accent: Palette.filtEnvAccent).frame(maxWidth: .infinity)
                Knob("Release", SynthParamFilterRelease, model, accent: Palette.filtEnvAccent, timeMapped: true).frame(maxWidth: .infinity)
            }
        }
    }

    private var globalPanel: some View {
        Panel(title: "Global", accent: Palette.globalAccent) {
            VStack(spacing: 8) {
                HStack(alignment: .top, spacing: 0) {
                    Knob("Voices", SynthParamVoiceCount, model, accent: Palette.globalAccent, integer: true).frame(maxWidth: .infinity)
                    ToggleButton("Legato", SynthParamLegato, model, accent: Palette.globalAccent).frame(maxWidth: .infinity)
                    Knob("Glide", SynthParamGlideTime, model, accent: Palette.globalAccent, unit: "s").frame(maxWidth: .infinity)
                    Knob("Start", SynthParamGlideStart, model, accent: Palette.globalAccent, unit: "", integer: true).frame(maxWidth: .infinity)
                }
                HStack(spacing: 0) {
                    Knob("Spread", SynthParamOscPhaseSpread, model, accent: Palette.globalAccent).frame(maxWidth: .infinity)
                    Knob("Analog", SynthParamAnalogAmount, model, accent: Palette.globalAccent).frame(maxWidth: .infinity)
                    Knob("Master", SynthParamMasterGain, model, accent: Palette.globalAccent).frame(maxWidth: .infinity)
                    Knob("Bend", SynthParamPitchBendRange, model, accent: Palette.globalAccent, unit: "st").frame(maxWidth: .infinity)
                }
                HStack(spacing: 0) {
                    // Vel -> Volume stays hardwired; Vel -> Cutoff/Reso/Drive
                    // are assigned in the Mod Matrix.
                    Knob("Vel Vol", SynthParamVelToVolume, model, accent: Palette.velAccent).frame(maxWidth: .infinity)
                    Color.clear.frame(maxWidth: .infinity)
                    Color.clear.frame(maxWidth: .infinity)
                    Color.clear.frame(maxWidth: .infinity)
                }
            }
        }
    }

    private var arpPanel: some View {
        Panel(title: "Arpeggiator", accent: Palette.arpAccent) {
            HStack(alignment: .top, spacing: 8) {
                ToggleButton("Arp", SynthParamArpOn, model, accent: Palette.arpAccent)
                ToggleButton("Hold", SynthParamArpHold, model, accent: Palette.arpAccent)
                arpDropdown("Mode", SynthParamArpMode,
                            ["Up", "Down", "Up-Dn", "Rnd"], width: 72)
                arpDropdown("Octaves", SynthParamArpOctaves,
                            ["1", "2", "3", "4"], width: 64)
                arpDropdown("Division", SynthParamArpRate,
                            SynthParameters.syncDivisionStrings, width: 64)
                Knob("Gate", SynthParamArpGate, model, accent: Palette.arpAccent)
                Spacer(minLength: 0)
            }
        }
    }

    private func arpDropdown(_ title: String, _ address: SynthParam,
                             _ options: [String], width: CGFloat) -> some View {
        VStack(alignment: .leading, spacing: 3) {
            Text(title.uppercased())
                .font(.system(size: 9, weight: .semibold, design: .rounded))
                .tracking(0.6)
                .foregroundColor(Palette.engrave)
            Dropdown(address, options, model, accent: Palette.arpAccent,
                     helpText: SynthHelp.text(for: AUParameterAddress(address.rawValue)))
                .frame(width: width)
        }
    }

    private var effectsPanel: some View {
        Panel(title: "Effects  ·  Chorus → Delay", accent: Palette.fxAccent) {
            HStack(alignment: .top, spacing: 0) {
                Knob("Cho Mix", SynthParamChorusMix, model,
                     accent: Palette.fxAccent).frame(maxWidth: .infinity)
                fxDivision("Cho Sync", SynthParamChorusRate)
                    .frame(maxWidth: .infinity)
                Knob("Cho Depth", SynthParamChorusDepth, model,
                     accent: Palette.fxAccent).frame(maxWidth: .infinity)
                Rectangle().fill(Color.white.opacity(0.08)).frame(width: 1, height: 70)
                    .padding(.horizontal, 8)
                Knob("Dly Mix", SynthParamDelayMix, model,
                     accent: Palette.fxAccent).frame(maxWidth: .infinity)
                fxDivision("Dly Sync", SynthParamDelayTime)
                    .frame(maxWidth: .infinity)
                Knob("Feedback", SynthParamDelayFeedback, model,
                     accent: Palette.fxAccent).frame(maxWidth: .infinity)
                Knob("Tone", SynthParamDelayTone, model,
                     accent: Palette.fxAccent).frame(maxWidth: .infinity)
                Knob("PingPong", SynthParamDelayPingPong, model,
                     accent: Palette.fxAccent).frame(maxWidth: .infinity)
            }
        }
    }

    private func fxDivision(_ title: String, _ address: SynthParam) -> some View {
        VStack(alignment: .center, spacing: 3) {
            Text(title.uppercased())
                .font(.system(size: 9, weight: .semibold, design: .rounded))
                .tracking(0.5)
                .foregroundColor(Palette.engrave)
                .lineLimit(1)
            Dropdown(address, SynthParameters.syncDivisionStrings, model,
                     accent: Palette.fxAccent,
                     helpText: SynthHelp.text(for: AUParameterAddress(address.rawValue)))
                .frame(width: 58)
        }
    }

    // MARK: Mod matrix

    static let modSources = ["None", "LFO 1", "LFO 2", "Filt Env", "Amp Env",
                             "Velocity", "Key Trk", "Mod Whl", "Aftertch", "Random"]
    static let modDests   = ["None", "Osc Pitch", "Osc2 Pitch", "Pulse W", "Cutoff",
                             "Reso", "Drive", "WT Frame", "WT Live", "X-Mod", "Amp"]

    private func modSlot(_ n: Int, _ s: SynthParam, _ d: SynthParam, _ a: SynthParam) -> some View {
        HStack(spacing: 5) {
            Text("\(n)")
                .font(.system(size: 10, weight: .bold, design: .monospaced))
                .foregroundColor(Palette.engraveDim)
                .frame(width: 10)
            Dropdown(s, Self.modSources, model, accent: Palette.lfoAccent,
                     helpText: "Modulation source feeding slot \(n).")
                .frame(width: 90)
            Image(systemName: "arrow.right")
                .font(.system(size: 7, weight: .bold))
                .foregroundColor(Palette.engraveDim)
            Dropdown(d, Self.modDests, model, accent: Palette.filterAccent,
                     helpText: "Destination that slot \(n) modulates.")
                .frame(width: 88)
            AmountSlider(a, model, accent: Palette.lfoAccent,
                         helpText: "Bipolar modulation amount for slot \(n).")
                .frame(width: 100)
        }
    }

    private var modMatrixPanel: some View {
        Panel(title: "Mod Matrix", accent: Palette.lfoAccent) {
            VStack(spacing: 9) {
                HStack(spacing: 18) {
                    modSlot(1, SynthParamMod1Source, SynthParamMod1Dest, SynthParamMod1Amount)
                    modSlot(2, SynthParamMod2Source, SynthParamMod2Dest, SynthParamMod2Amount)
                    modSlot(3, SynthParamMod3Source, SynthParamMod3Dest, SynthParamMod3Amount)
                }
                HStack(spacing: 18) {
                    modSlot(4, SynthParamMod4Source, SynthParamMod4Dest, SynthParamMod4Amount)
                    modSlot(5, SynthParamMod5Source, SynthParamMod5Dest, SynthParamMod5Amount)
                    modSlot(6, SynthParamMod6Source, SynthParamMod6Dest, SynthParamMod6Amount)
                }
            }
        }
    }
}
