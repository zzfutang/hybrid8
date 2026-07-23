//
//  SynthTheme.swift
//  Visual language for the panel — a mix of mid-'80s Yamaha (dark charcoal
//  fascia, cream engraved legends, blue-green LCD readout, magenta/blue button
//  groups) and Oberheim (matte black metal, wood end-cheeks, colourful knob
//  caps with bright value arcs). Plus the reusable Knob and Selector controls.
//

import SwiftUI
import AudioToolbox
import Foundation

enum Palette {
    static let panelTop    = Color(red: 0.16, green: 0.16, blue: 0.18)
    static let panelBottom = Color(red: 0.10, green: 0.10, blue: 0.115)
    static let sectionBG   = Color(red: 0.13, green: 0.13, blue: 0.145)
    static let engrave     = Color(red: 0.90, green: 0.87, blue: 0.79)   // cream legend
    static let engraveDim  = Color(red: 0.62, green: 0.60, blue: 0.55)
    static let lcd         = Color(red: 0.56, green: 0.93, blue: 0.74)   // blue-green LCD
    static let capTop      = Color(red: 0.24, green: 0.24, blue: 0.26)
    static let capBottom   = Color(red: 0.09, green: 0.09, blue: 0.10)
    static let track       = Color.white.opacity(0.10)
    static let woodLight   = Color(red: 0.52, green: 0.34, blue: 0.18)
    static let woodDark    = Color(red: 0.30, green: 0.18, blue: 0.09)

    // Oberheim-ish section accents.
    static let oscAccent    = Color(red: 0.93, green: 0.64, blue: 0.24)  // amber
    static let osc2Accent   = Color(red: 0.92, green: 0.45, blue: 0.36)  // coral
    static let filterAccent = Color(red: 0.26, green: 0.78, blue: 0.80)  // cyan
    static let ampAccent    = Color(red: 0.53, green: 0.80, blue: 0.36)  // green
    static let filtEnvAccent = Color(red: 0.34, green: 0.74, blue: 0.62) // teal-green
    static let lfoAccent    = Color(red: 0.80, green: 0.46, blue: 0.85)  // magenta
    static let globalAccent = Color(red: 0.40, green: 0.58, blue: 0.95)  // blue
    static let velAccent    = Color(red: 0.93, green: 0.45, blue: 0.55)  // rose
    static let mixerAccent  = Color(red: 0.72, green: 0.74, blue: 0.80)  // silver
    static let wtAccent     = Color(red: 0.60, green: 0.52, blue: 0.92)  // violet
}

// MARK: - Rotary knob

struct Knob: View {
    let title: String
    let addr: SynthParam
    let model: ParameterModel
    let unit: String
    let log: Bool
    let integer: Bool
    let accent: Color
    // Curve skew for log knobs: 1 = pure log; <1 pushes the midpoint toward the
    // top of the range while keeping fine resolution at the bottom.
    let skew: Float
    // When true the (linear 0..1) value is displayed as a mapped time in
    // seconds — the curve lives in the DSP mapping, so the knob stays linear.
    let timeMapped: Bool

    @EnvironmentObject private var help: HelpModel
    @State private var value: Float
    @State private var startNorm: CGFloat?

    private let lo: Float
    private let hi: Float

    init(_ title: String, _ addr: SynthParam, _ model: ParameterModel,
         accent: Color, unit: String = "", log: Bool = false,
         integer: Bool = false, skew: Float = 1.0, timeMapped: Bool = false) {
        self.title = title; self.addr = addr; self.model = model
        self.unit = unit; self.log = log; self.integer = integer
        self.accent = accent; self.skew = max(0.1, skew); self.timeMapped = timeMapped
        let p = model.param(addr)
        let rawLo = p?.minValue ?? 0
        self.lo = log ? max(rawLo, 0.0001) : rawLo
        self.hi = p?.maxValue ?? 1
        _value = State(initialValue: p?.value ?? 0)
    }

    var body: some View {
        VStack(spacing: 3) {
            Text(title.uppercased())
                .font(.system(size: 10, weight: .semibold, design: .rounded))
                .tracking(0.6)
                .foregroundColor(Palette.engrave)
                .lineLimit(1)
                .minimumScaleFactor(0.7)

            ZStack {
                KnobTicks()
                    .stroke(Color.white.opacity(0.16), lineWidth: 1)
                    .frame(width: 62, height: 62)

                KnobArc(fraction: 1)
                    .stroke(Palette.track, style: StrokeStyle(lineWidth: 4, lineCap: .round))
                    .frame(width: 54, height: 54)
                KnobArc(fraction: normFor(value))
                    .stroke(accent, style: StrokeStyle(lineWidth: 4, lineCap: .round))
                    .frame(width: 54, height: 54)
                    .shadow(color: accent.opacity(0.6), radius: 3)

                // Cap
                Circle()
                    .fill(LinearGradient(colors: [Palette.capTop, Palette.capBottom],
                                         startPoint: .top, endPoint: .bottom))
                    .frame(width: 42, height: 42)
                    .overlay(Circle().stroke(Color.black.opacity(0.6), lineWidth: 1))
                    .shadow(color: .black.opacity(0.5), radius: 2, y: 1)

                KnobPointer(norm: normFor(value))
                    .stroke(accent, style: StrokeStyle(lineWidth: 3, lineCap: .round))
                    .frame(width: 38, height: 38)
            }
            .frame(width: 66, height: 66)
            .contentShape(Rectangle())
            .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { g in
                        if startNorm == nil { startNorm = normFor(value) }
                        let delta = -g.translation.height / 180.0
                        var v = valueFor((startNorm ?? 0) + delta)
                        if integer { v = v.rounded() }
                        value = v
                        model.set(addr, value)
                    }
                    .onEnded { _ in startNorm = nil }
            )

            Text(displayString)
                .font(.system(size: 10, weight: .medium, design: .monospaced))
                .foregroundColor(Palette.lcd)
                .lineLimit(1)
        }
        .frame(width: 72)
        .onHover { hovering in
            let h = SynthHelp.text(for: AUParameterAddress(addr.rawValue))
            if hovering { help.set(h) } else { help.clear(ifMatches: h) }
        }
    }

    private func normFor(_ v: Float) -> CGFloat {
        if log {
            let e = (log2f(max(v, lo)) - log2f(lo)) / (log2f(hi) - log2f(lo))
            return CGFloat(powf(max(0, e), 1.0 / skew))   // inverse of the skew
        }
        return CGFloat((v - lo) / (hi - lo))
    }
    private func valueFor(_ n: CGFloat) -> Float {
        let nn = Float(min(max(n, 0), 1))
        if log {
            let e = powf(nn, skew)                          // skew the log position
            return powf(2, log2f(lo) + e * (log2f(hi) - log2f(lo)))
        }
        return lo + nn * (hi - lo)
    }
    private var displayString: String {
        let v = value
        if timeMapped {
            return SynthTime.displayString(fromNorm: v)
        }
        if integer {
            // Show a signed value only for bipolar controls (e.g. semitones).
            let s = lo < 0 ? String(format: "%+.0f", v) : String(format: "%.0f", v)
            return unit.isEmpty ? s : "\(s)\(unit)"
        }
        let num: String
        if abs(v) >= 1000 { num = String(format: "%.0f", v) }
        else if abs(v) >= 100 { num = String(format: "%.0f", v) }
        else if abs(v) >= 10 { num = String(format: "%.1f", v) }
        else if abs(v) >= 1 { num = String(format: "%.2f", v) }
        else { num = String(format: "%.3f", v) }
        return unit.isEmpty ? num : "\(num)\(unit)"
    }
}

// MARK: - Membrane-style selector (lit buttons)

struct Selector: View {
    let title: String
    let addr: SynthParam
    let options: [String]
    let model: ParameterModel
    let accent: Color
    private let minV: Int
    @EnvironmentObject private var help: HelpModel
    @State private var index: Int

    init(_ title: String, _ addr: SynthParam, _ options: [String],
         _ model: ParameterModel, accent: Color) {
        self.title = title; self.addr = addr; self.options = options
        self.model = model; self.accent = accent
        let lo = Int(model.param(addr)?.minValue ?? 0)
        self.minV = lo
        _index = State(initialValue: Int((model.param(addr)?.value ?? 0).rounded()) - lo)
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 3) {
            Text(title.uppercased())
                .font(.system(size: 9, weight: .semibold, design: .rounded))
                .tracking(0.6)
                .foregroundColor(Palette.engrave)
            HStack(spacing: 3) {
                ForEach(options.indices, id: \.self) { i in
                    button(i)
                }
            }
        }
        .onHover { hovering in
            let h = SynthHelp.text(for: AUParameterAddress(addr.rawValue))
            if hovering { help.set(h) } else { help.clear(ifMatches: h) }
        }
    }

    private func button(_ i: Int) -> some View {
        let on = i == index
        return Button {
            index = i
            model.set(addr, Float(i + minV))
        } label: {
            HStack(spacing: 3) {
                Circle()
                    .fill(on ? accent : Color.black.opacity(0.55))
                    .frame(width: 5, height: 5)
                    .shadow(color: on ? accent.opacity(0.9) : .clear, radius: on ? 2.5 : 0)
                Text(options[i])
                    .font(.system(size: 9, weight: on ? .bold : .regular, design: .rounded))
                    .foregroundColor(on ? Palette.engrave : Palette.engraveDim)
                    .lineLimit(1)
            }
            .padding(.horizontal, 6).padding(.vertical, 4)
            .background(
                RoundedRectangle(cornerRadius: 4)
                    .fill(on ? Color.white.opacity(0.08) : Color.black.opacity(0.25))
            )
            .overlay(
                RoundedRectangle(cornerRadius: 4)
                    .stroke(on ? accent.opacity(0.7) : Color.white.opacity(0.08), lineWidth: 1)
            )
        }
        .buttonStyle(.plain)
    }
}

// MARK: - Compact on/off toggle (single lit button)

struct ToggleButton: View {
    let title: String
    let addr: SynthParam
    let model: ParameterModel
    let accent: Color
    @EnvironmentObject private var help: HelpModel
    @State private var on: Bool

    init(_ title: String, _ addr: SynthParam, _ model: ParameterModel, accent: Color) {
        self.title = title; self.addr = addr; self.model = model; self.accent = accent
        _on = State(initialValue: (model.param(addr)?.value ?? 0) >= 0.5)
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 3) {
            Text(title.uppercased())
                .font(.system(size: 9, weight: .semibold, design: .rounded))
                .tracking(0.6)
                .foregroundColor(Palette.engrave)
            Button {
                on.toggle()
                model.set(addr, on ? 1 : 0)
            } label: {
                HStack(spacing: 3) {
                    Circle()
                        .fill(on ? accent : Color.black.opacity(0.55))
                        .frame(width: 5, height: 5)
                        .shadow(color: on ? accent.opacity(0.9) : .clear, radius: on ? 2.5 : 0)
                    Text(on ? "On" : "Off")
                        .font(.system(size: 9, weight: on ? .bold : .regular, design: .rounded))
                        .foregroundColor(on ? Palette.engrave : Palette.engraveDim)
                }
                .padding(.horizontal, 7).padding(.vertical, 4)
                .background(RoundedRectangle(cornerRadius: 4)
                    .fill(on ? Color.white.opacity(0.08) : Color.black.opacity(0.25)))
                .overlay(RoundedRectangle(cornerRadius: 4)
                    .stroke(on ? accent.opacity(0.7) : Color.white.opacity(0.08), lineWidth: 1))
            }
            .buttonStyle(.plain)
        }
        .onHover { hovering in
            let h = SynthHelp.text(for: AUParameterAddress(addr.rawValue))
            if hovering { help.set(h) } else { help.clear(ifMatches: h) }
        }
    }
}

// MARK: - Compact popup menu (for many-option selectors, e.g. mod matrix)

struct Dropdown: View {
    let addr: SynthParam
    let options: [String]
    let model: ParameterModel
    let accent: Color
    let helpText: String
    @EnvironmentObject private var help: HelpModel
    @State private var index: Int

    init(_ addr: SynthParam, _ options: [String], _ model: ParameterModel,
         accent: Color, helpText: String = "") {
        self.addr = addr; self.options = options; self.model = model
        self.accent = accent; self.helpText = helpText
        _index = State(initialValue: Int((model.param(addr)?.value ?? 0).rounded()))
    }

    var body: some View {
        Menu {
            ForEach(options.indices, id: \.self) { i in
                Button(options[i]) { index = i; model.set(addr, Float(i)) }
            }
        } label: {
            HStack(spacing: 3) {
                Circle()
                    .fill(index <= 0 ? Color.black.opacity(0.5) : accent)
                    .frame(width: 5, height: 5)
                    .shadow(color: index <= 0 ? .clear : accent.opacity(0.8), radius: 2)
                Text(index < options.count ? options[index] : "?")
                    .font(.system(size: 10, weight: .medium, design: .rounded))
                    .foregroundColor(index <= 0 ? Palette.engraveDim : Palette.engrave)
                    .lineLimit(1)
                Spacer(minLength: 1)
                Image(systemName: "chevron.down")
                    .font(.system(size: 6, weight: .bold))
                    .foregroundColor(Palette.engraveDim)
            }
            .padding(.horizontal, 6).padding(.vertical, 4)
            .background(RoundedRectangle(cornerRadius: 4).fill(Color.black.opacity(0.28)))
            .overlay(RoundedRectangle(cornerRadius: 4).stroke(Color.white.opacity(0.08), lineWidth: 1))
        }
        .menuStyle(.borderlessButton)
        .menuIndicator(.hidden)
        .onHover { hovering in
            if hovering { help.set(helpText) } else { help.clear(ifMatches: helpText) }
        }
    }
}

// MARK: - Compact bipolar amount slider (centre = 0)

struct AmountSlider: View {
    let addr: SynthParam
    let model: ParameterModel
    let accent: Color
    let helpText: String
    @EnvironmentObject private var help: HelpModel
    @State private var value: Float
    private let lo: Float
    private let hi: Float

    init(_ addr: SynthParam, _ model: ParameterModel, accent: Color, helpText: String = "") {
        self.addr = addr; self.model = model; self.accent = accent; self.helpText = helpText
        let p = model.param(addr)
        lo = p?.minValue ?? -1; hi = p?.maxValue ?? 1
        _value = State(initialValue: p?.value ?? 0)
    }

    var body: some View {
        HStack(spacing: 5) {
            GeometryReader { geo in
                let w = geo.size.width
                let frac = CGFloat((value - lo) / (hi - lo))            // 0..1
                let zero = CGFloat((0 - lo) / (hi - lo))                // centre
                ZStack(alignment: .leading) {
                    Capsule().fill(Color.black.opacity(0.35)).frame(height: 4)
                    Rectangle().fill(Color.white.opacity(0.12)).frame(width: 1, height: 10)
                        .offset(x: zero * w - 0.5)
                    Rectangle().fill(accent)
                        .frame(width: max(1, abs(frac - zero) * w), height: 4)
                        .offset(x: min(frac, zero) * w)
                    Circle().fill(accent).frame(width: 11, height: 11)
                        .shadow(color: accent.opacity(0.7), radius: 2)
                        .offset(x: frac * w - 5.5)
                }
                .frame(maxHeight: .infinity)
                .contentShape(Rectangle())
                .gesture(DragGesture(minimumDistance: 0).onChanged { g in
                    let n = Float(min(max(g.location.x / w, 0), 1))
                    value = lo + n * (hi - lo)
                    if abs(value) < 0.03 { value = 0 }                  // centre detent
                    model.set(addr, value)
                })
            }
            .frame(height: 18)
            Text(String(format: "%+.2f", value))
                .font(.system(size: 9, weight: .medium, design: .monospaced))
                .foregroundColor(value == 0 ? Palette.engraveDim : Palette.lcd)
                .frame(width: 34, alignment: .trailing)
        }
        .onHover { hovering in
            if hovering { help.set(helpText) } else { help.clear(ifMatches: helpText) }
        }
    }
}

// MARK: - Section panel container

struct Panel<Content: View, Trailing: View>: View {
    let title: String
    let accent: Color
    let trailing: Trailing
    let content: Content

    init(title: String, accent: Color,
         @ViewBuilder trailing: () -> Trailing,
         @ViewBuilder content: () -> Content) {
        self.title = title; self.accent = accent
        self.trailing = trailing(); self.content = content()
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            HStack(spacing: 6) {
                RoundedRectangle(cornerRadius: 2)
                    .fill(accent)
                    .frame(width: 16, height: 4)
                Text(title.uppercased())
                    .font(.system(size: 11, weight: .heavy, design: .rounded))
                    .tracking(1.4)
                    .foregroundColor(Palette.engrave)
                Spacer(minLength: 8)
                trailing
            }
            Spacer(minLength: 8)
            content
            Spacer(minLength: 8)
        }
        .padding(12)
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .top)
        .background(
            RoundedRectangle(cornerRadius: 8)
                .fill(Palette.sectionBG)
                .overlay(RoundedRectangle(cornerRadius: 8)
                    .stroke(Color.black.opacity(0.55), lineWidth: 1))
                .overlay(RoundedRectangle(cornerRadius: 8)
                    .stroke(Color.white.opacity(0.05), lineWidth: 1)
                    .padding(1))
        )
    }
}

// Panels that don't need a header accessory keep the simple two-argument form.
extension Panel where Trailing == EmptyView {
    init(title: String, accent: Color, @ViewBuilder content: () -> Content) {
        self.init(title: title, accent: accent, trailing: { EmptyView() }, content: content)
    }
}

// MARK: - Inline waveform selector (lives in a panel header)

struct InlineWaveSelect: View {
    let addr: SynthParam
    let options: [String]
    let model: ParameterModel
    let accent: Color
    @EnvironmentObject private var help: HelpModel
    @State private var index: Int

    init(_ addr: SynthParam, _ options: [String], _ model: ParameterModel, accent: Color) {
        self.addr = addr; self.options = options; self.model = model; self.accent = accent
        _index = State(initialValue: Int((model.param(addr)?.value ?? 0).rounded()))
    }

    var body: some View {
        HStack(spacing: 3) {
            ForEach(options.indices, id: \.self) { i in
                let on = i == index
                Button {
                    index = i
                    model.set(addr, Float(i))
                    model.forceRefresh()   // re-evaluate which controls are dimmed
                } label: {
                    Text(options[i])
                        .font(.system(size: 9, weight: on ? .bold : .regular, design: .rounded))
                        .foregroundColor(on ? Palette.engrave : Palette.engraveDim)
                        .padding(.horizontal, 6).padding(.vertical, 3)
                        .background(RoundedRectangle(cornerRadius: 4)
                            .fill(on ? Color.white.opacity(0.10) : Color.black.opacity(0.25)))
                        .overlay(RoundedRectangle(cornerRadius: 4)
                            .stroke(on ? accent.opacity(0.8) : Color.white.opacity(0.08), lineWidth: 1))
                }
                .buttonStyle(.plain)
            }
        }
        .onHover { hovering in
            let h = SynthHelp.text(for: AUParameterAddress(addr.rawValue))
            if hovering { help.set(h) } else { help.clear(ifMatches: h) }
        }
    }
}

extension View {
    /// Dim and disable a control that does not apply to the current osc type.
    func dimmed(_ on: Bool) -> some View {
        self.opacity(on ? 0.25 : 1).allowsHitTesting(!on)
    }
}
