//
//  R50ParameterModel.swift
//  Bridges the AUParameterTree to SwiftUI. Publishes a version bump whenever a
//  parameter changes externally (host automation, presets) so bound controls
//  refresh, and hands out two-way Bindings that write back through the tree.
//

import AudioToolbox
import SwiftUI

final class R50ParameterModel: ObservableObject {
    let tree: AUParameterTree

    @Published private(set) var version: Int = 0
    @Published private(set) var outputLevel: Float = 0
    @Published var presetIndex: Int = 0

    private var token: AUParameterObserverToken?
    private var timer: Timer?
    private let meterProvider: (() -> Float)?
    private let presetApplier: ((Int) -> Void)?

    init(tree: AUParameterTree,
         meterProvider: (() -> Float)? = nil,
         presetApplier: ((Int) -> Void)? = nil) {
        self.tree = tree
        self.meterProvider = meterProvider
        self.presetApplier = presetApplier

        token = tree.token(byAddingParameterObserver: { [weak self] _, _ in
            DispatchQueue.main.async { self?.version &+= 1 }
        })

        if meterProvider != nil {
            timer = Timer.scheduledTimer(withTimeInterval: 1.0 / 30.0,
                                         repeats: true) { [weak self] _ in
                guard let self, let provider = self.meterProvider else { return }
                let level = provider()
                // Fast attack, slow decay so the meter stays readable.
                self.outputLevel = level > self.outputLevel
                    ? level
                    : self.outputLevel * 0.82
            }
        }
    }

    deinit {
        timer?.invalidate()
        if let token { tree.removeParameterObserver(token) }
    }

    func parameter(_ address: R50Param) -> AUParameter? {
        tree.parameter(withAddress: AUParameterAddress(address.rawValue))
    }

    func value(_ address: R50Param) -> Float {
        parameter(address)?.value ?? 0
    }

    /// Two-way binding that writes through the tree so DSP, host automation and
    /// every other bound control stay in sync.
    func binding(_ address: R50Param) -> Binding<Float> {
        Binding(
            get: { [weak self] in self?.value(address) ?? 0 },
            set: { [weak self] newValue in
                guard let param = self?.parameter(address) else { return }
                param.setValue(newValue, originator: nil)
                self?.version &+= 1
            })
    }

    func displayString(_ address: R50Param) -> String {
        guard let param = parameter(address) else { return "" }
        return param.string(fromValue: nil)
    }

    func applyPreset(_ index: Int) {
        presetIndex = index
        presetApplier?(index)
    }
}
