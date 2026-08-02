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
    /// 0 = clean, 1 = the output limiter is working (signal over its knee),
    /// 2 = the pre-limiter signal exceeded full scale. Held briefly so a
    /// single hot transient is seen.
    @Published private(set) var clipState: Int = 0
    @Published var presetIndex: Int = 0
    @Published private(set) var userPresets: [AUAudioUnitPreset] = []
    @Published private(set) var presetName: String =
        R50FactoryPresets.all.first?.name ?? "Init"

    private var token: AUParameterObserverToken?
    private var timer: Timer?
    private let meterProvider: (() -> Float)?
    private let headroomProvider: (() -> Float)?
    private let presetApplier: ((Int) -> Void)?
    private weak var audioUnit: AUAudioUnit?
    /// The output limiter's knee: above this the sound is being coloured.
    private static let limiterKnee: Float = 0.75
    private var clipHoldTicks = 0

    init(tree: AUParameterTree,
         meterProvider: (() -> Float)? = nil,
         headroomProvider: (() -> Float)? = nil,
         presetApplier: ((Int) -> Void)? = nil,
         audioUnit: AUAudioUnit? = nil) {
        self.tree = tree
        self.meterProvider = meterProvider
        self.headroomProvider = headroomProvider
        self.presetApplier = presetApplier
        self.audioUnit = audioUnit
        self.userPresets = audioUnit?.userPresets ?? []

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

                // The LED shows the worst recent state and holds it ~1.3 s,
                // so a single hot transient is seen rather than strobed.
                guard let headroom = self.headroomProvider?() else { return }
                let state = headroom > 1.0 ? 2
                          : headroom > Self.limiterKnee ? 1 : 0
                if state > 0 {
                    if state >= self.clipState { self.clipState = state }
                    self.clipHoldTicks = 40
                } else if self.clipHoldTicks > 0 {
                    self.clipHoldTicks -= 1
                    if self.clipHoldTicks == 0 { self.clipState = 0 }
                }
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
        presetName = index >= 0 && index < R50FactoryPresets.all.count
            ? R50FactoryPresets.all[index].name : "Init"
        presetApplier?(index)
    }

    func saveUserPreset(named rawName: String) throws {
        guard let audioUnit else { return }
        let name = rawName.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !name.isEmpty else { return }
        let preset = AUAudioUnitPreset()
        preset.number = -1
        preset.name = name
        try audioUnit.saveUserPreset(preset)
        audioUnit.currentPreset = preset
        userPresets = audioUnit.userPresets
        presetIndex = -1
        presetName = name
    }

    func applyUserPreset(_ preset: AUAudioUnitPreset) {
        audioUnit?.currentPreset = preset
        presetIndex = -1
        presetName = preset.name
        version &+= 1
    }

    // MARK: - JSON patch documents

    func exportPatchJSON() throws -> Data {
        guard let r50 = audioUnit as? R50AudioUnit else {
            throw CocoaError(.fileWriteUnknown)
        }
        return try r50.exportPatchJSON(name: presetName)
    }

    /// Applies the document; returns any keyPaths this build did not know.
    func importPatchJSON(_ data: Data) throws -> [String] {
        guard let r50 = audioUnit as? R50AudioUnit else {
            throw CocoaError(.fileReadUnknown)
        }
        let result = try r50.importPatchJSON(data)
        presetIndex = -1
        presetName = result.name
        version &+= 1
        return result.unknownKeys
    }
}
