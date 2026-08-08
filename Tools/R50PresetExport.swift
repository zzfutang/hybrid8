#!/usr/bin/env swift
//
// Dump every installed R50 factory preset as a JSON patch document, one file
// per preset in bank order, into Products/R50/factory_presets (or the
// directory given as the first argument).
//
// This is how the repo's factory patch files are (re)normalised: the app
// bundles that directory, and it IS the factory bank — there is no other
// source. The format matches R50PatchJSON.swift: schemaVersion, name, every
// parameter by tree keyPath, and the partial -> persistent sample instrument
// ID map.
//

import AVFoundation
import AudioToolbox
import Foundation

private func fourCC(_ text: String) -> OSType {
    text.utf8.reduce(0) { ($0 << 8) | OSType($1) }
}

private func safeName(_ name: String, _ index: Int) -> String {
    let allowed = CharacterSet.alphanumerics.union(CharacterSet(charactersIn: "-_"))
    let body = name.unicodeScalars.map { allowed.contains($0) ? Character(String($0)) : "_" }
    return String(format: "%03d_", index + 1) + String(body)
}

private func instantiate(
    _ description: AudioComponentDescription
) throws -> AVAudioUnit {
    var result: AVAudioUnit?
    var failure: Error?
    AVAudioUnit.instantiate(with: description, options: []) { unit, error in
        result = unit
        failure = error
    }
    let deadline = Date().addingTimeInterval(20)
    while result == nil && failure == nil && Date() < deadline {
        RunLoop.current.run(mode: .default, before: Date().addingTimeInterval(0.05))
    }
    if let failure { throw failure }
    guard let result else {
        throw NSError(domain: "R50PresetExport", code: 1,
                      userInfo: [NSLocalizedDescriptionKey: "R50 AUv3 was not found"])
    }
    return result
}

let outputURL = URL(fileURLWithPath: CommandLine.arguments.dropFirst().first
    ?? "Products/R50/factory_presets", isDirectory: true)
try FileManager.default.createDirectory(at: outputURL,
                                        withIntermediateDirectories: true)

let description = AudioComponentDescription(
    componentType: kAudioUnitType_MusicDevice,
    componentSubType: fourCC("R50v"),
    componentManufacturer: fourCC("Jhgn"),
    componentFlags: 0,
    componentFlagsMask: 0)
let instrument = try instantiate(description)
let audioUnit = instrument.auAudioUnit
let presets = audioUnit.factoryPresets ?? []
guard !presets.isEmpty else { fatalError("No factory presets reported") }
guard let tree = audioUnit.parameterTree else { fatalError("No parameter tree") }

// Stale documents from a bank that was longer once would silently keep
// shipping; regeneration owns the whole directory.
for stale in (try? FileManager.default.contentsOfDirectory(
    at: outputURL, includingPropertiesForKeys: nil)) ?? []
where stale.pathExtension == "json" {
    try FileManager.default.removeItem(at: stale)
}

for (index, preset) in presets.enumerated() {
    audioUnit.currentPreset = preset

    var values: [String: Double] = [:]
    for parameter in tree.allParameters {
        values[parameter.keyPath] = Double(parameter.value)
    }
    let sampleAssets = (audioUnit.fullState?["R50SampleAssetIDs"]
        as? [String: String]) ?? [:]

    let document: [String: Any] = [
        "schemaVersion": 1,
        "name": preset.name,
        "values": values,
        "sampleAssets": sampleAssets,
    ]
    let data = try JSONSerialization.data(
        withJSONObject: document, options: [.prettyPrinted, .sortedKeys])
    let url = outputURL.appendingPathComponent(
        safeName(preset.name, index) + ".json")
    try data.write(to: url, options: .atomic)
    print(String(format: "[%3d/%d] %@", index + 1, presets.count,
                 url.lastPathComponent))
}

print("\nExported \(presets.count) presets to \(outputURL.path)")
