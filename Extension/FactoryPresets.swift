//
//  FactoryPresets.swift
//  Loads the bundled factory preset catalog. Preset sound data and browser
//  categories live in Resources/FactoryPresets.json rather than source code.
//

import AudioToolbox
import Foundation

struct FactoryPreset {
    let name: String
    let category: String
    let values: [AUParameterAddress: Float]
}

private final class FactoryPresetBundleToken: NSObject {}

private struct FactoryPresetDocument: Decodable {
    let formatVersion: Int
    let categoryOrder: [String]
    let presets: [FactoryPresetRecord]
}

private struct FactoryPresetRecord: Decodable {
    let name: String
    let category: String
    let values: [FactoryPresetValue]
}

private struct FactoryPresetValue: Decodable {
    // `parameter` makes the external file readable and reviewable. Runtime
    // loading intentionally uses the stable numeric address.
    let parameter: String
    let address: UInt64
    let value: Float
}

enum FactoryPresets {
    private struct Catalog {
        let presets: [FactoryPreset]
        let categoryOrder: [String]
    }

    private static let catalog: Catalog = loadCatalog()

    static let all = catalog.presets
    static let categoryOrder = catalog.categoryOrder

    static func category(for name: String) -> String {
        all.first(where: { $0.name == name })?.category ?? "Basic"
    }

    private static func loadCatalog() -> Catalog {
        let bundle = Bundle(for: FactoryPresetBundleToken.self)
        guard let url = bundle.url(forResource: "FactoryPresets",
                                   withExtension: "json"),
              let data = try? Data(contentsOf: url),
              let document = try? JSONDecoder().decode(
                FactoryPresetDocument.self, from: data),
              document.formatVersion == 1 else {
            NSLog("Hybrid8: missing or invalid FactoryPresets.json; using Init")
            return fallbackCatalog()
        }

        var names = Set<String>()
        var valid = true
        let presets = document.presets.compactMap { record -> FactoryPreset? in
            guard !record.name.isEmpty, !record.category.isEmpty,
                  names.insert(record.name).inserted else {
                valid = false
                return nil
            }
            var values: [AUParameterAddress: Float] = [:]
            for item in record.values {
                guard !item.parameter.isEmpty,
                      item.address < UInt64(SynthParamCount.rawValue),
                      item.value.isFinite,
                      values[AUParameterAddress(item.address)] == nil else {
                    valid = false
                    continue
                }
                values[AUParameterAddress(item.address)] = item.value
            }
            return FactoryPreset(name: record.name,
                                 category: record.category,
                                 values: values)
        }

        let declaredCategories = Set(document.categoryOrder)
        guard valid, !presets.isEmpty, presets.first?.name == "Init",
              !declaredCategories.isEmpty,
              presets.allSatisfy({ declaredCategories.contains($0.category) })
        else {
            NSLog("Hybrid8: FactoryPresets.json failed validation; using Init")
            return fallbackCatalog()
        }
        return Catalog(presets: presets,
                       categoryOrder: document.categoryOrder)
    }

    private static func fallbackCatalog() -> Catalog {
        Catalog(presets: [
            FactoryPreset(name: "Init", category: "Basic", values: [:])
        ], categoryOrder: ["Basic"])
    }
}
