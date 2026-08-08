//
//  R50PatchJSON.swift
//  A patch as a human-editable JSON document: every parameter by its tree
//  keyPath, plus the partial -> persistent sample instrument ID map.
//
//  Keyed by keyPath, never by numeric address: a person editing a factory
//  patch in the repo should read "effects.fxSlot1Mix", and a file survives any
//  future renumbering the same way the sample sidecar does. Export writes the
//  complete parameter set so a document means the same thing regardless of
//  what the defaults later become; import accepts a sparse one and leaves
//  missing parameters at their defaults. Keys the current build does not know
//  are collected rather than failing the import — a hand-typed typo should be
//  reported, not silently shape a different sound.
//

import AudioToolbox
import Foundation

struct R50PatchDocument: Codable {
    var schemaVersion: Int
    var name: String
    var values: [String: Double]
    var sampleAssets: [String: String]
}

enum R50PatchJSONError: LocalizedError {
    case unsupportedSchema(Int)

    var errorDescription: String? {
        switch self {
        case .unsupportedSchema(let version):
            return "Unsupported patch schema version \(version)."
        }
    }
}

enum R50PatchJSON {
    static let schemaVersion = 1

    static func encode(tree: AUParameterTree, name: String,
                       sampleAssets: [Int: String]) throws -> Data {
        var values: [String: Double] = [:]
        for parameter in tree.allParameters {
            values[parameter.keyPath] = Double(parameter.value)
        }
        var assets: [String: String] = [:]
        for (partial, id) in sampleAssets { assets[String(partial)] = id }
        let document = R50PatchDocument(
            schemaVersion: schemaVersion, name: name,
            values: values, sampleAssets: assets)
        let encoder = JSONEncoder()
        // Sorted keys, so a re-export after one edit diffs as one line.
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        return try encoder.encode(document)
    }

    struct DecodedPatch {
        let name: String
        let values: [AUParameterAddress: AUValue]
        let sampleAssets: [Int: String]
        /// keyPaths the document carried that no current parameter answers to.
        let unknownKeys: [String]
    }

    static func decode(_ data: Data, tree: AUParameterTree) throws -> DecodedPatch {
        let document = try JSONDecoder().decode(R50PatchDocument.self, from: data)
        guard document.schemaVersion == schemaVersion else {
            throw R50PatchJSONError.unsupportedSchema(document.schemaVersion)
        }
        var map: [String: AUParameterAddress] = [:]
        for parameter in tree.allParameters {
            map[parameter.keyPath] = parameter.address
        }
        var values: [AUParameterAddress: AUValue] = [:]
        var unknown: [String] = []
        for (key, value) in document.values {
            if let address = map[key] {
                values[address] = AUValue(value)
            } else {
                unknown.append(key)
            }
        }
        var assets: [Int: String] = [:]
        for (key, id) in document.sampleAssets {
            if let partial = Int(key) { assets[partial] = id }
        }
        return DecodedPatch(name: document.name, values: values,
                            sampleAssets: assets, unknownKeys: unknown.sorted())
    }
}
