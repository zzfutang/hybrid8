//
//  R50FactoryPresets.swift
//  The factory bank: JSON patch documents bundled in factory_presets/, one
//  per preset, applied in bytewise filename order. The documents are the
//  single source of truth — the Swift recipes that generated the original
//  bank were deleted once the export landed (git history has them); a sound
//  is changed by editing its document, or by editing in the app and
//  exporting over it. See Products/R50/factory_presets/README.md.
//

import AudioToolbox

struct R50FactoryPreset {
    let name: String
    let values: [AUParameterAddress: AUValue]
    let sampleAssets: [Int: String]

    init(name: String, values: [AUParameterAddress: AUValue],
         sampleAssets: [Int: String] = [:]) {
        self.name = name
        self.values = values
        self.sampleAssets = sampleAssets
    }
}

enum R50FactoryPresets {

    static let all: [R50FactoryPreset] = loadBank()

    private static func loadBank() -> [R50FactoryPreset] {
        guard let root = Bundle(for: R50AudioUnit.self)
            .url(forResource: "factory_presets", withExtension: nil) else {
            return []
        }
        let files = (try? FileManager.default.contentsOfDirectory(
            at: root, includingPropertiesForKeys: nil)) ?? []
        let ordered = files.filter { $0.pathExtension == "json" }
            .sorted { $0.lastPathComponent < $1.lastPathComponent }

        let tree = R50Parameters.buildTree()
        var bank: [R50FactoryPreset] = []
        for url in ordered {
            if let data = try? Data(contentsOf: url),
               let patch = try? R50PatchJSON.decode(data, tree: tree) {
                bank.append(R50FactoryPreset(name: patch.name,
                                             values: patch.values,
                                             sampleAssets: patch.sampleAssets))
            } else {
                // A document that fails to parse must not crash the appex,
                // and must not vanish either: every later preset would slide
                // up one number, and saved songs store those numbers. Hold
                // the slot with the default patch wearing the failure as its
                // name, so the mistake is heard about immediately.
                bank.append(R50FactoryPreset(
                    name: "BROKEN " + url.lastPathComponent, values: [:]))
            }
        }
        return bank
    }
}
