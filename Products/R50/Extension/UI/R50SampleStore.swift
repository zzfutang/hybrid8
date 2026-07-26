//
//  R50SampleStore.swift
//  Instrument catalog and WAV/AIFF importer. Decoding happens off the main and
//  audio threads; the engine owns the decoded audio once it is installed.
//
//  Follows the pipeline proven in Hybrid 8's WavetableStore, including its
//  chunked decoder: the app extension is sandboxed, so a user-selected file is
//  readable only for the lifetime of the grant and must be copied into
//  Application Support to survive a relaunch.
//

import AVFoundation
import SwiftUI

struct SampleEntry: Identifiable {
    let index: Int
    let name: String
    let isFactory: Bool
    let zones: Int
    let lowKey: Int
    let highKey: Int
    let loopMode: Int          // 0 none, 1 forward, 2 ping-pong
    let rootKey: Int
    let tuneCents: Float
    /// Generated multisamples carry a root per zone; one number cannot describe
    /// them, so only single-region imports can be retuned.
    let retunable: Bool
    let seconds: Double        // duration of the zone covering middle C
    let bytes: Int             // total audio held by every zone
    var id: Int { index }

    var loopLabel: String {
        switch loopMode {
        case 1:  return "Loop"
        case 2:  return "P-Pong"
        default: return "1-Shot"
        }
    }

    var keyRange: String { "\(SampleEntry.noteName(lowKey))–\(SampleEntry.noteName(highKey))" }

    var rootLabel: String {
        guard retunable else { return "per zone" }
        return abs(tuneCents) < 0.5
            ? SampleEntry.noteName(rootKey)
            : String(format: "%@%+.0f", SampleEntry.noteName(rootKey), tuneCents)
    }

    var lengthLabel: String {
        seconds >= 1.0 ? String(format: "%.2f s", seconds)
                       : String(format: "%.0f ms", seconds * 1000)
    }

    var sizeLabel: String {
        bytes >= 1_048_576 ? String(format: "%.1f MB", Double(bytes) / 1_048_576)
                           : String(format: "%.0f KB", Double(bytes) / 1024)
    }

    static func noteName(_ midi: Int) -> String {
        let names = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
        let octave = midi / 12 - 1
        return "\(names[((midi % 12) + 12) % 12])\(octave)"
    }
}

private struct SampleManifestEntry: Codable {
    let name: String
    let fileName: String
    var rootKey: Int
    let loopMode: Int
    /// Optional so a library.json written before retuning existed still decodes.
    var tuneCents: Float?
}

final class R50SampleStore: ObservableObject {
    @Published private(set) var entries: [SampleEntry] = []
    @Published private(set) var isImporting = false
    @Published var errorMessage: String?

    /// Instruments generated in C++ and always present, ahead of any import.
    /// Their indices are stable, which is what lets a preset reference one.
    private let factoryCount: Int

    private let model: R50ParameterModel
    private weak var audioUnit: R50AudioUnit?
    private var manifests: [SampleManifestEntry] = []

    init(model: R50ParameterModel, audioUnit: R50AudioUnit) {
        self.model = model
        self.audioUnit = audioUnit
        self.factoryCount = audioUnit.instrumentCount
        refresh()
        loadPersistentSamples()
    }

    /// Which Partial the sample browser edits. The Synth page owns the
    /// selection; this keeps the two pages pointing at the same Partial.
    @Published var partial = 0

    private var instrumentAddress: R50Param {
        r50PartialParam(Int32(partial), R50FieldSampleInstrument)
    }

    var selectedIndex: Int {
        Int(model.value(instrumentAddress).rounded())
    }

    func select(_ entry: SampleEntry) {
        model.parameter(instrumentAddress)?
            .setValue(Float(entry.index), originator: nil)
        model.objectWillChange.send()
    }

    /// Retune an imported entry. Writes through to the engine, which picks it
    /// up on the next note, and to the manifest so it survives a relaunch.
    func setRoot(_ entry: SampleEntry, key: Int, cents: Float) {
        guard !entry.isFactory else { return }
        let manifestIndex = entry.index - factoryCount
        guard manifestIndex >= 0, manifestIndex < manifests.count else { return }
        let clamped = min(127, max(0, key))
        manifests[manifestIndex].rootKey = clamped
        manifests[manifestIndex].tuneCents = cents
        saveManifest()
        audioUnit?.setRoot(instrument: entry.index, key: clamped, cents: cents)
        refresh()
    }

    /// Re-run detection on an already-imported entry, for when the automatic
    /// answer was wrong or the file was imported before detection existed.
    func redetect(_ entry: SampleEntry) {
        guard !entry.isFactory else { return }
        let manifestIndex = entry.index - factoryCount
        guard manifestIndex >= 0, manifestIndex < manifests.count else { return }
        let manifest = manifests[manifestIndex]
        let url = samplesDirectory.appendingPathComponent(manifest.fileName)
        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            guard let self, let audio = try? Self.decodeMono(url),
                  let found = Self.detectPitch(audio.samples,
                                               sampleRate: audio.sampleRate,
                                               audioUnit: self.audioUnit),
                  found.confidence > 0.5 else {
                DispatchQueue.main.async {
                    self?.errorMessage = "No clear pitch found in \(manifest.name)."
                }
                return
            }
            DispatchQueue.main.async {
                self.setRoot(entry, key: found.rootKey, cents: -found.centsSharp)
                self.errorMessage = String(
                    format: "%@ detected at %.1f Hz (%@%+.0f cents).",
                    manifest.name, found.hertz,
                    SampleEntry.noteName(found.rootKey), -found.centsSharp)
            }
        }
    }

    /// Preview one entry. Deliberately separate from `select`: auditioning is
    /// how you decide whether you want a sample, and it would be no use if
    /// hearing one meant assigning it to the Partial you are editing.
    func audition(_ entry: SampleEntry, note: Int) {
        audioUnit?.audition(instrument: entry.index, note: note)
    }

    func refresh() {
        guard let audioUnit else { return }
        entries = (0..<audioUnit.instrumentCount).map { index in
            let info = audioUnit.sampleInfo(at: index)
            let frames = info?["frames"] as? Int ?? 0
            let rate = info?["sampleRate"] as? Double ?? 44100
            return SampleEntry(
                index: index,
                name: info?["name"] as? String
                    ?? audioUnit.instrumentName(at: index) ?? "—",
                isFactory: index < factoryCount,
                zones: info?["zones"] as? Int ?? 0,
                lowKey: info?["lowKey"] as? Int ?? 0,
                highKey: info?["highKey"] as? Int ?? 127,
                loopMode: info?["loopMode"] as? Int ?? 0,
                rootKey: info?["rootKey"] as? Int ?? 60,
                tuneCents: info?["tuneCents"] as? Float ?? 0,
                retunable: info?["retunable"] as? Bool ?? false,
                seconds: rate > 0 ? Double(frames) / rate : 0,
                bytes: info?["totalBytes"] as? Int ?? 0)
        }
    }

    // MARK: - Import

    func importAudioFile(at sourceURL: URL) {
        let access = sourceURL.startAccessingSecurityScopedResource()
        defer { if access { sourceURL.stopAccessingSecurityScopedResource() } }

        do {
            let decoded = try Self.decodeMono(sourceURL)
            let name = sourceURL.deletingPathExtension().lastPathComponent
            // Anything under half a second is treated as a one-shot transient;
            // longer material loops over its whole length until the sample page
            // can edit loop points.
            let seconds = Double(decoded.samples.count) / decoded.sampleRate
            let loopMode = seconds < 0.5 ? 0 : 1

            let fileName = UUID().uuidString + "." +
                (sourceURL.pathExtension.isEmpty ? "wav" : sourceURL.pathExtension)
            try FileManager.default.copyItem(
                at: sourceURL, to: samplesDirectory.appendingPathComponent(fileName))

            // Detected rather than assumed. A hardcoded middle C is wrong for
            // every sample that was not recorded at middle C, which is most of
            // them, and it puts the error across the whole keyboard.
            let detected = Self.detectPitch(decoded.samples,
                                            sampleRate: decoded.sampleRate,
                                            audioUnit: audioUnit)
            let confident = (detected?.confidence ?? 0) > 0.5
            let manifest = SampleManifestEntry(
                name: name, fileName: fileName,
                rootKey: confident ? detected!.rootKey : 60,
                loopMode: loopMode,
                // Negated: the detector says how sharp the recording is, and
                // what gets stored is the correction that cancels it.
                tuneCents: confident ? -detected!.centsSharp : 0)
            if !confident {
                errorMessage = "\(name): no clear pitch found — root key left at C4."
            }
            manifests.append(manifest)
            saveManifest()
            install(manifest, samples: decoded.samples,
                    sampleRate: decoded.sampleRate, selectAfterwards: true)
        } catch {
            errorMessage = error.localizedDescription
        }
    }

    /// Removes the file and manifest entry. The decoded audio stays in the
    /// engine until the process exits — the library never frees, so an audio
    /// callback can never observe a freed asset. The instrument therefore
    /// disappears from the list only after a relaunch.
    func delete(_ entry: SampleEntry) {
        guard !entry.isFactory else { return }
        let manifestIndex = entry.index - factoryCount
        guard manifestIndex >= 0, manifestIndex < manifests.count else { return }
        let manifest = manifests.remove(at: manifestIndex)
        try? FileManager.default.removeItem(
            at: samplesDirectory.appendingPathComponent(manifest.fileName))
        saveManifest()
        errorMessage = "\(manifest.name) will disappear from the list after a restart."
    }

    // MARK: - Persistence

    private var samplesDirectory: URL {
        let base = FileManager.default.urls(for: .applicationSupportDirectory,
                                            in: .userDomainMask)[0]
        let directory = base.appendingPathComponent("R50/Samples", isDirectory: true)
        try? FileManager.default.createDirectory(at: directory,
                                                 withIntermediateDirectories: true)
        return directory
    }

    private var manifestURL: URL {
        samplesDirectory.appendingPathComponent("library.json")
    }

    private func saveManifest() {
        if let data = try? JSONEncoder().encode(manifests) {
            try? data.write(to: manifestURL, options: .atomic)
        }
    }

    private func loadPersistentSamples() {
        guard let data = try? Data(contentsOf: manifestURL),
              let decoded = try? JSONDecoder().decode(
                [SampleManifestEntry].self, from: data) else { return }
        manifests = decoded
        for manifest in manifests {
            let url = samplesDirectory.appendingPathComponent(manifest.fileName)
            DispatchQueue.global(qos: .userInitiated).async { [weak self] in
                guard let self else { return }
                do {
                    let audio = try Self.decodeMono(url)
                    DispatchQueue.main.async {
                        self.install(manifest, samples: audio.samples,
                                     sampleRate: audio.sampleRate,
                                     selectAfterwards: false)
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

    private func install(_ manifest: SampleManifestEntry, samples: [Float],
                         sampleRate: Double, selectAfterwards: Bool) {
        isImporting = true
        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            guard let self, let audioUnit = self.audioUnit else { return }
            let index = audioUnit.installSample(
                name: manifest.name, samples: samples, sampleRate: sampleRate,
                rootKey: manifest.rootKey, loopMode: manifest.loopMode)
            if index >= 0 {
                audioUnit.setRoot(instrument: index, key: manifest.rootKey,
                                  cents: manifest.tuneCents ?? 0)
            }
            DispatchQueue.main.async {
                self.isImporting = false
                guard index >= 0 else {
                    self.errorMessage = "The sample library is full."
                    return
                }
                self.refresh()
                if selectAfterwards,
                   let entry = self.entries.first(where: { $0.index == index }) {
                    self.select(entry)
                }
            }
        }
    }

    // MARK: - Decoding

    private static func decodeMono(_ url: URL) throws -> (samples: [Float],
                                                          sampleRate: Double) {
        let file = try AVAudioFile(forReading: url,
                                   commonFormat: .pcmFormatFloat32,
                                   interleaved: false)
        guard file.length > 0 && file.length <= 44_100 * 60 else {
            throw ImportError.unsupportedLength
        }

        // AVAudioFile may return fewer frames than requested — files carrying
        // metadata routinely short-read on the first call — so read in chunks
        // until EOF rather than trusting a single read.
        let chunkCapacity: AVAudioFrameCount = 65_536
        var mono: [Float] = []
        mono.reserveCapacity(Int(file.length))
        while file.framePosition < file.length {
            guard let buffer = AVAudioPCMBuffer(pcmFormat: file.processingFormat,
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
        return (mono, file.processingFormat.sampleRate)
    }

    // MARK: - Pitch detection
    //
    // The algorithm lives in R50PitchDetect.hpp, not here: it is signal
    // processing, and in C++ the offline test suite can actually exercise it
    // against synthetic sines, sawtooths and noise.

    struct DetectedPitch {
        let hertz: Double
        let rootKey: Int
        /// How sharp the recording is. Negate it to get a tuning correction.
        let centsSharp: Float
        let confidence: Float
    }

    private static func detectPitch(_ samples: [Float], sampleRate: Double,
                                    audioUnit: R50AudioUnit?) -> DetectedPitch? {
        guard let info = audioUnit?.detectPitch(samples: samples,
                                                sampleRate: sampleRate),
              let hertz = info["hertz"] as? Double,
              let rootKey = info["rootKey"] as? Int,
              let centsSharp = info["centsSharp"] as? Float,
              let confidence = info["confidence"] as? Float else { return nil }
        return DetectedPitch(hertz: hertz, rootKey: rootKey,
                             centsSharp: centsSharp, confidence: confidence)
    }

    enum ImportError: LocalizedError {
        case unsupportedLength, decodeFailed
        var errorDescription: String? {
            switch self {
            case .unsupportedLength:
                return "The audio file is empty or longer than 60 seconds."
            case .decodeFailed:
                return "The audio file could not be decoded as PCM audio."
            }
        }
    }
}
