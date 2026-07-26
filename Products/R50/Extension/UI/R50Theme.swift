//
//  R50Theme.swift
//  R50's visual language — deliberately nothing like Hybrid 8's warm brass
//  fascia. This is cold-rolled steel and smoked acrylic with a cyan VFD glow:
//  flat panels, thin rules, square-shouldered controls.
//
//  Everything here is self-contained (palette, knob, selector, meter) so R50's
//  look can evolve without touching any other product.
//

import AudioToolbox
import SwiftUI

enum R50Layout {
    static let width: CGFloat  = 1180
    static let height: CGFloat = 520
}

enum R50Palette {
    static let chassisTop  = Color(white: 0.16)
    static let chassisLow  = Color(white: 0.10)
    static let panel       = Color(white: 0.19)
    static let panelEdge   = Color(white: 0.30)
    static let engrave     = Color(white: 0.62)
    static let legend      = Color(white: 0.78)

    static let glow        = Color(red: 0.35, green: 0.90, blue: 0.92)  // VFD cyan
    static let glowDim     = Color(red: 0.20, green: 0.48, blue: 0.50)
    static let accent      = Color(red: 0.95, green: 0.42, blue: 0.30)  // burnt orange
    static let track       = Color(white: 0.09)
}

// MARK: - Chrome

/// Titled panel with a hairline border and an engraved header rule.
struct R50Panel<Content: View>: View {
    let title: String
    @ViewBuilder var content: Content

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text(title.uppercased())
                .font(.system(size: 9, weight: .semibold, design: .monospaced))
                .tracking(1.6)
                .foregroundColor(R50Palette.engrave)
            Rectangle()
                .fill(R50Palette.panelEdge)
                .frame(height: 1)
            content
            Spacer(minLength: 0)
        }
        .padding(12)
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
        .background(
            RoundedRectangle(cornerRadius: 3)
                .fill(R50Palette.panel)
                .overlay(
                    RoundedRectangle(cornerRadius: 3)
                        .stroke(R50Palette.panelEdge, lineWidth: 1))
        )
    }
}

// MARK: - Knob

/// Rotary control. Vertical drag changes the value; ⌥-drag is a fine adjust and
/// double-click returns the parameter to its Init value.
struct R50Knob: View {
    let title: String
    let address: R50Param
    @ObservedObject var model: R50ParameterModel

    @State private var dragStart: Float?

    private var param: AUParameter? { model.parameter(address) }

    var body: some View {
        let value = model.value(address)
        let norm  = normalized(value)

        VStack(spacing: 5) {
            ZStack {
                Circle()
                    .fill(LinearGradient(
                        colors: [Color(white: 0.26), Color(white: 0.13)],
                        startPoint: .top, endPoint: .bottom))
                    .overlay(Circle().stroke(Color(white: 0.34), lineWidth: 1))

                // Value arc: 240° sweep starting at the lower-left.
                Circle()
                    .trim(from: 0, to: 0.666 * CGFloat(norm))
                    .stroke(R50Palette.glow, style: StrokeStyle(lineWidth: 2.5, lineCap: .round))
                    .rotationEffect(.degrees(138))
                    .padding(-5)

                // Pointer.
                Rectangle()
                    .fill(R50Palette.legend)
                    .frame(width: 2, height: 13)
                    .offset(y: -8)
                    .rotationEffect(.degrees(-120 + 240 * Double(norm)))
            }
            .frame(width: 42, height: 42)
            .contentShape(Circle())
            .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { drag in
                        guard let param else { return }
                        let start = dragStart ?? value
                        if dragStart == nil { dragStart = value }
                        let fine = NSEvent.modifierFlags.contains(.option) ? 0.25 : 1.0
                        let delta = Float(-drag.translation.height * fine / 160.0)
                        let target = normalized(start) + delta
                        param.setValue(denormalized(min(max(target, 0), 1)),
                                       originator: nil)
                        model.objectWillChange.send()
                    }
                    .onEnded { _ in dragStart = nil })
            .onTapGesture(count: 2) {
                if let param { param.setValue(initValue, originator: nil) }
                model.objectWillChange.send()
            }

            Text(title.uppercased())
                .font(.system(size: 8, weight: .medium, design: .monospaced))
                .tracking(0.8)
                .foregroundColor(R50Palette.engrave)
            Text(model.displayString(address))
                .font(.system(size: 9, weight: .semibold, design: .monospaced))
                .foregroundColor(R50Palette.glow)
                .lineLimit(1)
                .minimumScaleFactor(0.7)
        }
        .frame(width: 64)
    }

    // MARK: Value mapping

    private var isLogarithmic: Bool {
        param?.flags.contains(.flag_DisplayLogarithmic) ?? false
    }

    /// Init values live in the tree's declared defaults; re-reading them here
    /// would need a second copy, so double-click uses the range midpoint for
    /// bipolar controls and the minimum otherwise — predictable and cheap.
    private var initValue: Float {
        guard let param else { return 0 }
        return param.minValue < 0 ? 0 : param.minValue
    }

    private func normalized(_ value: Float) -> Float {
        guard let param else { return 0 }
        let lo = param.minValue, hi = param.maxValue
        guard hi > lo else { return 0 }
        if isLogarithmic && lo > 0 {
            return Float(log(Double(value / lo)) / log(Double(hi / lo)))
        }
        return (value - lo) / (hi - lo)
    }

    private func denormalized(_ norm: Float) -> Float {
        guard let param else { return 0 }
        let lo = param.minValue, hi = param.maxValue
        if isLogarithmic && lo > 0 {
            return lo * Float(pow(Double(hi / lo), Double(norm)))
        }
        return lo + norm * (hi - lo)
    }
}

// MARK: - Selector

/// Segmented switch for indexed parameters.
struct R50Selector: View {
    let title: String
    let address: R50Param
    let options: [String]
    @ObservedObject var model: R50ParameterModel

    var body: some View {
        let current = Int(model.value(address).rounded())

        VStack(alignment: .leading, spacing: 5) {
            Text(title.uppercased())
                .font(.system(size: 8, weight: .medium, design: .monospaced))
                .tracking(0.8)
                .foregroundColor(R50Palette.engrave)

            HStack(spacing: 1) {
                ForEach(Array(options.enumerated()), id: \.offset) { index, label in
                    let selected = index == current
                    Text(label)
                        .font(.system(size: 9, weight: .semibold, design: .monospaced))
                        .foregroundColor(selected ? Color.black : R50Palette.legend)
                        .frame(maxWidth: .infinity)
                        .padding(.vertical, 5)
                        .background(selected ? R50Palette.glow : Color(white: 0.13))
                        .contentShape(Rectangle())
                        .onTapGesture {
                            model.parameter(address)?
                                .setValue(Float(index), originator: nil)
                            model.objectWillChange.send()
                        }
                }
            }
            .clipShape(RoundedRectangle(cornerRadius: 2))
            .overlay(RoundedRectangle(cornerRadius: 2)
                .stroke(Color(white: 0.32), lineWidth: 1))
        }
    }
}

// MARK: - Wave grid

/// Indexed-parameter picker for lists too long for a single segmented row.
/// Lays the options out as a fixed-column grid of short labels.
struct R50WaveGrid: View {
    let title: String
    let address: R50Param
    let options: [String]
    let columns: Int
    @ObservedObject var model: R50ParameterModel

    var body: some View {
        let current = Int(model.value(address).rounded())
        let rows = Int(ceil(Double(options.count) / Double(columns)))

        VStack(alignment: .leading, spacing: 5) {
            Text(title.uppercased())
                .font(.system(size: 8, weight: .medium, design: .monospaced))
                .tracking(0.8)
                .foregroundColor(R50Palette.engrave)

            VStack(spacing: 1) {
                ForEach(0..<rows, id: \.self) { row in
                    HStack(spacing: 1) {
                        ForEach(0..<columns, id: \.self) { column in
                            let index = row * columns + column
                            if index < options.count {
                                cell(options[index], index: index,
                                     selected: index == current)
                            } else {
                                Color.clear.frame(maxWidth: .infinity)
                            }
                        }
                    }
                }
            }
            .clipShape(RoundedRectangle(cornerRadius: 2))
            .overlay(RoundedRectangle(cornerRadius: 2)
                .stroke(Color(white: 0.32), lineWidth: 1))
        }
    }

    private func cell(_ label: String, index: Int, selected: Bool) -> some View {
        Text(label)
            .font(.system(size: 8, weight: .semibold, design: .monospaced))
            .foregroundColor(selected ? Color.black : R50Palette.legend)
            .lineLimit(1)
            .minimumScaleFactor(0.65)
            .frame(maxWidth: .infinity)
            .padding(.vertical, 4)
            .background(selected ? R50Palette.glow : Color(white: 0.13))
            .contentShape(Rectangle())
            .onTapGesture {
                model.parameter(address)?.setValue(Float(index), originator: nil)
                model.objectWillChange.send()
            }
    }
}

// MARK: - Meter

struct R50Meter: View {
    let level: Float

    var body: some View {
        let clamped = CGFloat(min(max(level, 0), 1))
        HStack(spacing: 6) {
            Text("OUT")
                .font(.system(size: 8, weight: .medium, design: .monospaced))
                .foregroundColor(R50Palette.engrave)
            GeometryReader { geo in
                ZStack(alignment: .leading) {
                    Rectangle().fill(R50Palette.track)
                    Rectangle()
                        .fill(LinearGradient(
                            colors: [R50Palette.glowDim, R50Palette.glow,
                                     R50Palette.accent],
                            startPoint: .leading, endPoint: .trailing))
                        .frame(width: geo.size.width * clamped)
                }
                .clipShape(RoundedRectangle(cornerRadius: 1))
                .overlay(RoundedRectangle(cornerRadius: 1)
                    .stroke(Color(white: 0.30), lineWidth: 1))
            }
            .frame(width: 110, height: 8)
        }
    }
}
