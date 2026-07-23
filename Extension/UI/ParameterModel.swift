//
//  ParameterModel.swift
//  Bridges the AUParameterTree to SwiftUI. Publishes a version bump whenever a
//  parameter changes externally (host automation, presets) so bound controls
//  refresh, and hands out two-way Bindings that write back through the tree.
//

import SwiftUI
import AudioToolbox

final class ParameterModel: ObservableObject {
    let tree: AUParameterTree
    @Published private(set) var version: Int = 0
    @Published private(set) var modulationVersion: Int = 0
    private var token: AUParameterObserverToken?
    private var timer: Timer?
    private let effectiveProvider: ((AUParameterAddress) -> Float)?
    private var effectiveValues: [AUParameterAddress: Float] = [:]

    init(tree: AUParameterTree,
         effectiveProvider: ((AUParameterAddress) -> Float)? = nil) {
        self.tree = tree
        self.effectiveProvider = effectiveProvider
        token = tree.token(byAddingParameterObserver: { [weak self] _, _ in
            DispatchQueue.main.async { self?.version &+= 1 }
        })
        if effectiveProvider != nil {
            timer = Timer.scheduledTimer(withTimeInterval: 1.0 / 30.0,
                                         repeats: true) { [weak self] _ in
                self?.pollEffectiveValues()
            }
        }
    }

    deinit { timer?.invalidate() }

    private func pollEffectiveValues() {
        guard let provider = effectiveProvider else { return }
        for parameter in tree.allParameters {
            effectiveValues[parameter.address] = provider(parameter.address)
        }
        modulationVersion &+= 1
    }

    func effectiveValue(_ address: SynthParam) -> Float {
        let raw = AUParameterAddress(address.rawValue)
        return effectiveValues[raw] ?? param(address)?.value ?? 0
    }

    func param(_ address: SynthParam) -> AUParameter? {
        tree.parameter(withAddress: AUParameterAddress(address.rawValue))
    }

    /// Write a value through the tree, tagged with our observer token so it is
    /// not echoed back to us as an "external" change.
    func set(_ address: SynthParam, _ value: Float) {
        param(address)?.setValue(value, originator: token)
    }

    // MARK: - Whole-state capture / apply (used by the preset system)

    /// Snapshot every parameter value keyed by address.
    func captureState() -> [AUParameterAddress: Float] {
        var d = [AUParameterAddress: Float]()
        for p in tree.allParameters { d[p.address] = p.value }
        return d
    }

    /// Apply a full state, then trigger a single UI refresh. Values are written
    /// with our token so we don't get a storm of per-parameter callbacks; the
    /// DSP is still updated via the AU's implementor observer.
    func applyState(_ state: [AUParameterAddress: Float]) {
        for p in tree.allParameters {
            if let v = state[p.address] { p.setValue(v, originator: token) }
        }
        version &+= 1
    }

    /// Force bound controls to re-read their parameter values.
    func forceRefresh() { version &+= 1 }

    func binding(_ address: SynthParam) -> Binding<Float> {
        let p = param(address)
        return Binding(
            get: { p?.value ?? 0 },
            set: { [weak self] newValue in p?.setValue(newValue, originator: self?.token) }
        )
    }

    /// Two-way Int binding for indexed parameters. `value` stored in the tree
    /// is the real parameter value (e.g. octave -4...4), not a zero-based index.
    func indexBinding(_ address: SynthParam) -> Binding<Int> {
        let p = param(address)
        return Binding(
            get: { Int((p?.value ?? 0).rounded()) },
            set: { [weak self] newValue in
                p?.setValue(AUValue(newValue), originator: self?.token)
            }
        )
    }
}
