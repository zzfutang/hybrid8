//
//  R50AudioUnit.swift
//  The AUAudioUnit subclass. Builds the parameter tree, wires it to the C++
//  DSP via R50DSPKernelAdapter, and exposes the render block to the host.
//

import AVFoundation
import AudioToolbox

public final class R50AudioUnit: AUAudioUnit {

    private let kernel = R50DSPKernelAdapter()
    private var outputBus: AUAudioUnitBus!
    private var _outputBusArray: AUAudioUnitBusArray!
    private var defaultState: [AUParameterAddress: AUValue] = [:]
    private var _currentPreset: AUAudioUnitPreset?

    // MARK: - Init

    public override init(componentDescription: AudioComponentDescription,
                         options: AudioComponentInstantiationOptions = []) throws {
        try super.init(componentDescription: componentDescription, options: options)

        let format = AVAudioFormat(standardFormatWithSampleRate: 44100.0, channels: 2)!
        outputBus = try AUAudioUnitBus(format: format)
        outputBus.maximumChannelCount = 2
        _outputBusArray = AUAudioUnitBusArray(audioUnit: self,
                                              busType: .output,
                                              busses: [outputBus])

        let tree = R50Parameters.buildTree()

        // Host -> DSP: apply parameter changes made outside the render thread.
        tree.implementorValueObserver = { [kernel] param, value in
            let stored = param.unit == .indexed ? value.rounded() : value
            kernel.setParameter(param.address, value: stored)
        }
        // DSP -> host: report current value (e.g. for the generic view).
        tree.implementorValueProvider = { [kernel] param in
            kernel.value(forParameter: param.address)
        }
        // Human-readable strings for indexed / continuous parameters.
        tree.implementorStringFromValueCallback = { param, valuePtr in
            let value = valuePtr?.pointee ?? param.value
            if let strings = param.valueStrings {
                let idx = Int(value.rounded())
                if idx >= 0 && idx < strings.count { return strings[idx] }
            }
            if param.unit == .indexed {
                return String(Int(value.rounded()))
            }
            if param.unit == .hertz {
                return value >= 1000 ? String(format: "%.2f kHz", value / 1000)
                                     : String(format: "%.0f Hz", value)
            }
            if param.unit == .seconds {
                return value >= 1 ? String(format: "%.2f s", value)
                                  : String(format: "%.0f ms", value * 1000)
            }
            if param.unit == .decibels {
                return String(format: "%+.1f dB", value)
            }
            return String(format: "%.2f", value)
        }

        // Assign to the inherited property so the base class wires up
        // address-based automation and full-state save/restore.
        self.parameterTree = tree

        // Push initial values into the DSP and remember them as the "Init"
        // state that factory presets are layered on top of.
        for param in tree.allParameters {
            kernel.setParameter(param.address, value: param.value)
            defaultState[param.address] = param.value
        }
    }

    // MARK: - Presets

    public override var factoryPresets: [AUAudioUnitPreset]? {
        R50FactoryPresets.all.enumerated().map { index, preset in
            let p = AUAudioUnitPreset()
            p.number = index
            p.name = preset.name
            return p
        }
    }

    public override var supportsUserPresets: Bool { true }

    /// AU parameter state still contains the numeric selector for host
    /// automation. This sidecar makes saved documents and user presets robust
    /// when directory discovery changes runtime indices.
    /// Partial -> persistent instrument ID for whatever samples are selected
    /// right now. This is the identity that survives library reordering; the
    /// numeric selector parameters are runtime indices. Only partials whose
    /// source is actually a sample are stated — a wave partial's selector is
    /// an inert bootstrap value, not a selection worth preserving.
    func currentSampleAssets() -> [Int: String] {
        var sampleAssets: [Int: String] = [:]
        for partial in 0..<4 {
            guard let source = parameterTree?.parameter(
                withAddress: r50PartialParam(Int32(partial),
                                             R50FieldSourceType).rawValue),
                  source.value >= 0.5
            else { continue }
            let address = r50PartialParam(Int32(partial),
                                          R50FieldSampleInstrument)
            guard let parameter = parameterTree?.parameter(
                withAddress: address.rawValue)
            else { continue }
            let index = Int(parameter.value.rounded())
            if let assetId = sampleInfo(at: index)?["assetId"] as? String {
                sampleAssets[partial] = assetId
            }
        }
        return sampleAssets
    }

    public override var fullState: [String: Any]? {
        get {
            var state = super.fullState ?? [:]
            var sampleAssets: [String: String] = [:]
            for (partial, assetId) in currentSampleAssets() {
                sampleAssets[String(partial)] = assetId
            }
            state["R50SampleAssetIDs"] = sampleAssets
            return state
        }
        set {
            super.fullState = newValue
            guard let sampleAssets = newValue?["R50SampleAssetIDs"]
                    as? [String: String] else { return }
            for partial in 0..<4 {
                guard let assetId = sampleAssets[String(partial)] else { continue }
                let index = kernel.instrumentIndex(forAssetId: assetId)
                guard index >= 0 else { continue }
                let address = r50PartialParam(Int32(partial),
                                              R50FieldSampleInstrument)
                parameterTree?.parameter(withAddress: address.rawValue)?
                    .setValue(AUValue(index), originator: nil)
            }
        }
    }

    public override var currentPreset: AUAudioUnitPreset? {
        get { _currentPreset }
        set {
            guard let preset = newValue else { _currentPreset = nil; return }
            if preset.number >= 0 && preset.number < R50FactoryPresets.all.count {
                applyFactoryPreset(preset.number)
            } else if preset.number < 0,
                      let state = try? presetState(for: preset) {
                fullState = state
            }
            _currentPreset = preset
        }
    }

    func applyFactoryPreset(_ index: Int) {
        guard index >= 0, index < R50FactoryPresets.all.count else { return }
        let preset = R50FactoryPresets.all[index]
        applyPatch(preset.values, sampleAssets: preset.sampleAssets)
    }

    /// Reset the whole tree to defaults overlaid with `values`, then resolve
    /// the persistent sample IDs to whatever runtime indices they hold in
    /// this library. Every way of applying a complete sound — factory preset,
    /// imported patch document — funnels through here.
    func applyPatch(_ values: [AUParameterAddress: AUValue],
                    sampleAssets: [Int: String]) {
        guard let tree = parameterTree else { return }
        for param in tree.allParameters {
            let v = values[param.address] ?? defaultState[param.address] ?? param.value
            param.setValue(v, originator: nil) // nil -> UI + DSP both refresh
        }
        for (partial, assetID) in sampleAssets {
            let instrument = kernel.instrumentIndex(forAssetId: assetID)
            let address = r50PartialParam(Int32(partial),
                                          R50FieldSampleInstrument)
            guard instrument >= 0,
                  let parameter = tree.parameter(
                    withAddress: address.rawValue
                  ) else { continue }
            parameter.setValue(AUValue(instrument), originator: nil)
        }
    }

    // MARK: - Patch documents

    /// The complete current sound as a human-editable JSON document.
    func exportPatchJSON(name: String) throws -> Data {
        guard let tree = parameterTree else {
            throw CocoaError(.fileWriteUnknown)
        }
        return try R50PatchJSON.encode(tree: tree, name: name,
                                       sampleAssets: currentSampleAssets())
    }

    /// Apply a JSON patch document. Returns its name and any keyPaths this
    /// build did not recognise (so the UI can report a stale or typoed file).
    func importPatchJSON(_ data: Data) throws -> (name: String, unknownKeys: [String]) {
        guard let tree = parameterTree else {
            throw CocoaError(.fileReadUnknown)
        }
        let patch = try R50PatchJSON.decode(data, tree: tree)
        applyPatch(patch.values, sampleAssets: patch.sampleAssets)
        return (patch.name, patch.unknownKeys)
    }

    // MARK: - Buses

    public override var outputBusses: AUAudioUnitBusArray { _outputBusArray }

    // Instrument: no audio input, stereo output.
    public override var channelCapabilities: [NSNumber]? { [0, 2] }

    // MARK: - Resource allocation

    public override func allocateRenderResources() throws {
        try super.allocateRenderResources()
        let format = outputBus.format
        kernel.setSampleRate(format.sampleRate, channelCount: format.channelCount)
    }

    public override func reset() {
        kernel.reset()
    }

    public override var internalRenderBlock: AUInternalRenderBlock {
        kernel.internalRenderBlock()
    }

    func outputMeter() -> Float { kernel.outputMeter() }
    func headroomPeak() -> Float { kernel.headroomPeak() }

    // MARK: - Sample library

    /// Offline install — call from a loader queue, never from the render thread.
    /// `loopStart`/`loopEnd` nil means the whole file loops, which is what an
    /// import with nothing stated in its `smpl` chunk gets.
    func installSample(name: String, samples: [Float], sampleRate: Double,
                       rootKey: Int, loopMode: Int,
                       loopStart: Int? = nil, loopEnd: Int? = nil) -> Int {
        let data = samples.withUnsafeBytes { Data($0) }
        return kernel.installSampleNamed(name, samples: data,
                                         sampleRate: sampleRate,
                                         rootKey: rootKey, loopMode: loopMode,
                                         loopStart: loopStart ?? 0,
                                         loopEnd: loopEnd ?? 0)
    }

    var instrumentCount: Int { kernel.instrumentCount() }

    func instrumentName(at index: Int) -> String? {
        kernel.instrumentName(at: index)
    }

    func sampleInfo(at index: Int) -> [String: Any]? {
        kernel.sampleInfo(at: index)
    }

    /// Where the factory WAV files live.
    var factoryDirectory: String { kernel.factoryDirectory() }

    /// One zone's audio and metadata, for writing out.
    func zone(instrument: Int, zone: Int) -> [String: Any]? {
        kernel.zone(ofInstrument: instrument, zone: zone)
    }

    /// Root key and loop as stated by a file's own `smpl` chunk. Nil when the
    /// file is not a WAV or carries no chunk, which is the caller's cue to fall
    /// back to detection.
    struct StatedMetadata {
        let rootKey: Int
        let hasLoop: Bool
        let pingPong: Bool
        let loopStart: Int
        let loopEnd: Int
    }

    func metadata(ofFileAtPath path: String) -> StatedMetadata? {
        guard let info = kernel.metadataOfFile(atPath: path),
              let rootKey = info["rootKey"] as? Int else { return nil }
        return StatedMetadata(
            rootKey: rootKey,
            hasLoop: info["hasLoop"] as? Bool ?? false,
            pingPong: info["pingPong"] as? Bool ?? false,
            loopStart: info["loopStart"] as? Int ?? 0,
            loopEnd: info["loopEnd"] as? Int ?? 0)
    }

    /// Estimate a buffer's pitch. Nil when the material is not pitched.
    func detectPitch(samples: [Float], sampleRate: Double) -> [String: Any]? {
        samples.withUnsafeBufferPointer { buffer in
            guard let base = buffer.baseAddress else { return nil }
            let data = Data(bytes: base, count: buffer.count * MemoryLayout<Float>.size)
            return kernel.detectPitch(of: data, sampleRate: sampleRate)
        }
    }

    /// Retune an imported instrument, effective from the next note on.
    func setRoot(instrument: Int, key: Int, cents: Float) {
        kernel.setRootKey(key, tuneCents: cents, forInstrument: instrument)
    }

    /// Preview a browser entry, independent of the patch.
    func audition(instrument: Int, note: Int = 60, velocity: Int = 100) {
        kernel.auditionInstrument(at: instrument,
                                  note: UInt8(clamping: note),
                                  velocity: UInt8(clamping: velocity))
    }
}
