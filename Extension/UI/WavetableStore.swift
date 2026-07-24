//
//  WavetableStore.swift
//  Persistent user-wavetable catalog and WAV/AIFF importer. Audio decoding and
//  DSP mip construction happen off the main/audio threads.
//

import AppKit
import AVFoundation
import UniformTypeIdentifiers
import SwiftUI

struct WavetableEntry: Identifiable {
    let slot: Int
    let name: String
    let frameCount: Int
    let isFactory: Bool
    let fileName: String?
    let frameLength: Int
    let previews: [[Float]]
    var id: Int { slot }
}

private struct WavetableManifestEntry: Codable {
    let slot: Int
    let name: String
    let fileName: String
    let frameLength: Int
}

final class WavetableStore: ObservableObject {
    @Published private(set) var entries: [WavetableEntry] = []
    @Published private(set) var isImporting = false
    @Published var errorMessage: String?

    private let model: ParameterModel
    private weak var audioUnit: SynthAudioUnit?
    private var manifests: [WavetableManifestEntry] = []
    private var retiredSlots: Set<Int> = []

    init(model: ParameterModel, audioUnit: SynthAudioUnit) {
        self.model = model
        self.audioUnit = audioUnit
        entries = Self.factoryEntries
        loadPersistentTables()
    }

    var selectedSlot: Int {
        Int((model.param(SynthParamWavetable)?.value ?? 0).rounded())
    }

    func entry(slot: Int) -> WavetableEntry {
        entries.first(where: { $0.slot == slot }) ?? Self.factoryEntries[0]
    }

    func select(_ entry: WavetableEntry) {
        model.set(SynthParamWavetable, Float(entry.slot))
        model.forceRefresh()
        objectWillChange.send()
    }

    func importAudioFile(at sourceURL: URL) {
        let access = sourceURL.startAccessingSecurityScopedResource()
        defer { if access { sourceURL.stopAccessingSecurityScopedResource() } }

        do {
            let samples = try Self.decodeMono(sourceURL)
            let choices = Self.frameLengthChoices(sampleCount: samples.count)
            guard let frameLength = chooseFrameLength(sampleCount: samples.count,
                                                      choices: choices) else { return }
            try persistAndInstall(sourceURL: sourceURL, samples: samples,
                                  frameLength: frameLength)
        } catch {
            errorMessage = error.localizedDescription
        }
    }

    func delete(_ entry: WavetableEntry) {
        guard !entry.isFactory, let fileName = entry.fileName else { return }
        retiredSlots.insert(entry.slot)
        manifests.removeAll { $0.slot == entry.slot }
        try? FileManager.default.removeItem(at: tablesDirectory
            .appendingPathComponent(fileName))
        saveManifest()
        entries.removeAll { $0.slot == entry.slot }
        if selectedSlot == entry.slot {
            model.set(SynthParamWavetable, 0)
        }
        // DSP memory is intentionally retained until process exit so an audio
        // callback can never observe freed table data.
    }

    func preview(for entry: WavetableEntry, normalizedFrame: Float) -> [Float] {
        if entry.isFactory {
            let count = 96
            let position = Double(min(1, max(0, normalizedFrame)))
            return (0..<count).map { index in
                let phase = 2.0 * Double.pi * Double(index) / Double(count)
                switch entry.slot {
                case 0:
                    let harmonics = 1 + Int(position * 15)
                    return Float((1...harmonics).reduce(0.0) {
                        $0 + sin(phase * Double($1)) / Double($1)
                    } * 0.72)
                case 1:
                    return Float(sin(phase + position * 8.0 * sin(phase)))
                case 2:
                    return Float(0.72 * sin(phase)
                               + 0.20 * sin(phase * 2)
                               + 0.08 * sin(phase * (3 + position * 3)))
                default:
                    return Float(0.65 * sin(phase)
                               + 0.28 * sin(phase * 3)
                               + 0.18 * sin(phase * (9 + position * 12)))
                }
            }
        }
        guard !entry.previews.isEmpty else { return [] }
        let index = min(entry.previews.count - 1,
                        max(0, Int((normalizedFrame
                            * Float(entry.previews.count - 1)).rounded())))
        return entry.previews[index]
    }

    private var tablesDirectory: URL {
        let base = FileManager.default.urls(for: .applicationSupportDirectory,
                                            in: .userDomainMask)[0]
        let directory = base.appendingPathComponent("Hybrid8/Wavetables",
                                                    isDirectory: true)
        try? FileManager.default.createDirectory(at: directory,
                                                  withIntermediateDirectories: true)
        return directory
    }

    private var manifestURL: URL {
        tablesDirectory.appendingPathComponent("library.json")
    }

    private func loadPersistentTables() {
        guard let data = try? Data(contentsOf: manifestURL),
              let decoded = try? JSONDecoder().decode(
                [WavetableManifestEntry].self, from: data) else { return }
        manifests = decoded.filter { $0.slot >= 4 && $0.slot < 256 }
        for manifest in manifests {
            let url = tablesDirectory.appendingPathComponent(manifest.fileName)
            DispatchQueue.global(qos: .userInitiated).async { [weak self] in
                guard let self else { return }
                do {
                    let samples = try Self.decodeMono(url)
                    guard samples.count % manifest.frameLength == 0,
                          self.audioUnit?.installWavetable(
                            slot: manifest.slot, samples: samples,
                            frameLength: manifest.frameLength) == true else {
                        throw ImportError.invalidFrameLength
                    }
                    let entry = Self.makeEntry(manifest: manifest,
                                               samples: samples)
                    DispatchQueue.main.async {
                        self.entries.removeAll { $0.slot == entry.slot }
                        self.entries.append(entry)
                        self.entries.sort { $0.slot < $1.slot }
                    }
                } catch {
                    DispatchQueue.main.async {
                        self.errorMessage =
                            "Could not load \(manifest.name): \(error.localizedDescription)"
                    }
                }
            }
        }
    }

    private func persistAndInstall(sourceURL: URL, samples: [Float],
                                   frameLength: Int) throws {
        guard let slot = (4..<256).first(where: { candidate in
            !manifests.contains(where: { $0.slot == candidate })
                && !retiredSlots.contains(candidate)
        }) else { throw ImportError.libraryFull }

        isImporting = true
        let name = sourceURL.deletingPathExtension().lastPathComponent
        let fileName = UUID().uuidString + "." +
            (sourceURL.pathExtension.isEmpty ? "wav" : sourceURL.pathExtension)
        let destination = tablesDirectory.appendingPathComponent(fileName)
        try FileManager.default.copyItem(at: sourceURL, to: destination)
        let manifest = WavetableManifestEntry(slot: slot, name: name,
                                              fileName: fileName,
                                              frameLength: frameLength)
        manifests.append(manifest)
        saveManifest()

        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            guard let self else { return }
            let installed = self.audioUnit?.installWavetable(
                slot: slot, samples: samples, frameLength: frameLength) == true
            let entry = Self.makeEntry(manifest: manifest, samples: samples)
            DispatchQueue.main.async {
                self.isImporting = false
                if installed {
                    self.entries.append(entry)
                    self.entries.sort { $0.slot < $1.slot }
                    self.select(entry)
                } else {
                    self.manifests.removeAll { $0.slot == slot }
                    try? FileManager.default.removeItem(at: destination)
                    self.saveManifest()
                    self.errorMessage = "The wavetable could not be processed."
                }
            }
        }
    }

    private func saveManifest() {
        if let data = try? JSONEncoder().encode(manifests) {
            try? data.write(to: manifestURL, options: .atomic)
        }
    }

    private func chooseFrameLength(sampleCount: Int,
                                   choices: [Int]) -> Int? {
        let alert = NSAlert()
        alert.messageText = "Import Wavetable"
        alert.informativeText =
            "Confirm the cycle length. The file contains \(sampleCount) mono samples."
        alert.addButton(withTitle: "Import")
        alert.addButton(withTitle: "Cancel")
        let popup = NSPopUpButton(frame: NSRect(x: 0, y: 0,
                                                width: 250, height: 26))
        let available = choices.isEmpty ? [sampleCount] : choices
        for length in available {
            popup.addItem(withTitle:
                "\(length) samples/frame  ·  \(sampleCount / length) frames")
        }
        if let preferred = available.firstIndex(of: 2048) {
            popup.selectItem(at: preferred)
        }
        alert.accessoryView = popup
        guard alert.runModal() == .alertFirstButtonReturn else { return nil }
        return available[popup.indexOfSelectedItem]
    }

    private static func decodeMono(_ url: URL) throws -> [Float] {
        let file = try AVAudioFile(forReading: url,
                                   commonFormat: .pcmFormatFloat32,
                                   interleaved: false)
        guard file.length > 0 && file.length <= 33_554_432 else {
            throw ImportError.unsupportedLength
        }

        // AVAudioFile is allowed to return fewer frames than requested. Some
        // wavetable WAVs with metadata do exactly that on the first read (for
        // example 262116 of 262144 samples), so a single-read decoder breaks
        // the cycle-length divisibility and misidentifies the table as one
        // giant frame. Read in chunks until EOF instead.
        let chunkCapacity: AVAudioFrameCount = 65_536
        var mono: [Float] = []
        mono.reserveCapacity(Int(file.length))
        while file.framePosition < file.length {
            guard let buffer = AVAudioPCMBuffer(
                pcmFormat: file.processingFormat,
                frameCapacity: chunkCapacity) else {
                throw ImportError.decodeFailed
            }
            try file.read(into: buffer)
            let count = Int(buffer.frameLength)
            if count == 0 { throw ImportError.decodeFailed }
            guard let channels = buffer.floatChannelData else {
                throw ImportError.decodeFailed
            }
            let channelCount = Int(buffer.format.channelCount)
            let start = mono.count
            mono.append(contentsOf: repeatElement(0, count: count))
            for channel in 0..<channelCount {
                for sample in 0..<count {
                    mono[start + sample] +=
                        channels[channel][sample] / Float(channelCount)
                }
            }
        }
        guard !mono.isEmpty else { throw ImportError.decodeFailed }
        return mono
    }

    private static func frameLengthChoices(sampleCount: Int) -> [Int] {
        [2048, 1024, 4096, 512, 256, 128, 64].filter {
            sampleCount % $0 == 0 && sampleCount / $0 <= 512
        }
    }

    private static func makeEntry(manifest: WavetableManifestEntry,
                                  samples: [Float]) -> WavetableEntry {
        let count = samples.count / manifest.frameLength
        var previews: [[Float]] = []
        previews.reserveCapacity(count)
        for frame in 0..<count {
            let base = frame * manifest.frameLength
            var preview = [Float](repeating: 0, count: 96)
            for x in preview.indices {
                let index = base + x * manifest.frameLength / preview.count
                preview[x] = samples[index]
            }
            let peak = max(0.000001, preview.reduce(0) {
                max($0, abs($1))
            })
            previews.append(preview.map { $0 / peak })
        }
        return WavetableEntry(slot: manifest.slot, name: manifest.name,
                              frameCount: count, isFactory: false,
                              fileName: manifest.fileName,
                              frameLength: manifest.frameLength,
                              previews: previews)
    }

    private static let factoryEntries: [WavetableEntry] = [
        WavetableEntry(slot: 0, name: "Harmonic", frameCount: 32,
                       isFactory: true, fileName: nil, frameLength: 1024,
                       previews: []),
        WavetableEntry(slot: 1, name: "FM", frameCount: 32,
                       isFactory: true, fileName: nil, frameLength: 1024,
                       previews: []),
        WavetableEntry(slot: 2, name: "Choir", frameCount: 32,
                       isFactory: true, fileName: nil, frameLength: 1024,
                       previews: []),
        WavetableEntry(slot: 3, name: "Metallic", frameCount: 32,
                       isFactory: true, fileName: nil, frameLength: 1024,
                       previews: [])
    ]

    enum ImportError: LocalizedError {
        case invalidFrameLength, unsupportedLength, decodeFailed, libraryFull
        var errorDescription: String? {
            switch self {
            case .invalidFrameLength:
                return "The selected frame length does not divide the audio file."
            case .unsupportedLength:
                return "The audio file is empty or too large."
            case .decodeFailed:
                return "The audio file could not be decoded as PCM audio."
            case .libraryFull:
                return "All 252 user wavetable slots are in use."
            }
        }
    }
}
