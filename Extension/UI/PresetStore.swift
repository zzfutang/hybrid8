//
//  PresetStore.swift
//  Preset browser model: factory presets (from FactoryPresets) plus user
//  presets saved as JSON in the extension's Application Support container.
//  Applies presets by writing a full state snapshot into the parameter tree.
//

import SwiftUI
import AudioToolbox

struct StoredPreset: Codable {
    var name: String
    var values: [String: Float]   // keyed by String(AUParameterAddress)
}

final class PresetStore: ObservableObject {
    private let model: ParameterModel
    private let defaults: [AUParameterAddress: Float]

    @Published var userPresets: [StoredPreset] = []
    @Published var currentName: String = "Init"
    @Published var currentIsUser: Bool = false
    @Published var currentIndex: Int = 0   // global index into factory + user

    var factory: [FactoryPreset] { FactoryPresets.all }
    private var total: Int { factory.count + userPresets.count }

    struct PresetGroup: Identifiable {
        let name: String
        let indices: [Int]      // indices into `factory`
        var id: String { name }
    }

    /// Factory presets grouped by category, in the defined display order.
    var categoryGroups: [PresetGroup] {
        FactoryPresets.categoryOrder.compactMap { cat in
            let idx = factory.indices.filter { FactoryPresets.category(for: factory[$0].name) == cat }
            return idx.isEmpty ? nil : PresetGroup(name: cat, indices: Array(idx))
        }
    }

    init(model: ParameterModel) {
        self.model = model
        self.defaults = model.captureState()
        loadUser()
    }

    // MARK: Apply

    func apply(globalIndex i: Int) {
        guard total > 0 else { return }
        let idx = ((i % total) + total) % total
        if idx < factory.count { applyFactory(idx) }
        else { applyUser(idx - factory.count) }
    }

    func applyFactory(_ index: Int) {
        guard factory.indices.contains(index) else { return }
        var s = defaults
        for (k, v) in factory[index].values { s[k] = v }
        model.applyState(s)
        currentName = factory[index].name
        currentIsUser = false
        currentIndex = index
    }

    func applyUser(_ index: Int) {
        guard userPresets.indices.contains(index) else { return }
        let preset = userPresets[index]
        var s = defaults
        for (ks, v) in preset.values { if let a = AUParameterAddress(ks) { s[a] = v } }
        model.applyState(s)
        currentName = preset.name
        currentIsUser = true
        currentIndex = factory.count + index
    }

    func initPatch() {
        model.applyState(defaults)
        currentName = "Init"
        currentIsUser = false
        currentIndex = 0
    }

    func next() { apply(globalIndex: currentIndex + 1) }
    func prev() { apply(globalIndex: currentIndex - 1) }

    // MARK: Save / delete (user presets)

    func saveUser(name: String) {
        let trimmed = name.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return }
        var dict = [String: Float]()
        for (a, v) in model.captureState() { dict[String(a)] = v }
        let preset = StoredPreset(name: trimmed, values: dict)
        if let data = try? JSONEncoder().encode(preset) {
            try? data.write(to: fileURL(for: trimmed))
        }
        loadUser()
        if let idx = userPresets.firstIndex(where: { $0.name == trimmed }) {
            currentIndex = factory.count + idx
            currentName = trimmed
            currentIsUser = true
        }
    }

    func deleteCurrentUser() {
        guard currentIsUser else { return }
        let idx = currentIndex - factory.count
        guard userPresets.indices.contains(idx) else { return }
        try? FileManager.default.removeItem(at: fileURL(for: userPresets[idx].name))
        loadUser()
        initPatch()
    }

    // MARK: Disk

    private var presetsDir: URL {
        let base = FileManager.default.urls(for: .applicationSupportDirectory,
                                            in: .userDomainMask)[0]
        let dir = base.appendingPathComponent("Hybrid8/Presets", isDirectory: true)
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        return dir
    }

    private func fileURL(for name: String) -> URL {
        let safe = name.map { $0.isLetter || $0.isNumber ? $0 : "_" }
        return presetsDir.appendingPathComponent(String(safe) + ".json")
    }

    func loadUser() {
        let files = (try? FileManager.default.contentsOfDirectory(
            at: presetsDir, includingPropertiesForKeys: nil)) ?? []
        var list: [StoredPreset] = []
        for f in files where f.pathExtension == "json" {
            if let d = try? Data(contentsOf: f),
               let p = try? JSONDecoder().decode(StoredPreset.self, from: d) {
                list.append(p)
            }
        }
        userPresets = list.sorted { $0.name.lowercased() < $1.name.lowercased() }
    }
}
