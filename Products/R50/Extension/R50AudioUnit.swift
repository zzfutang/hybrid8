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
            kernel.setParameter(param.address, value: value)
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
            if param.unit == .hertz {
                return value >= 1000 ? String(format: "%.2f kHz", value / 1000)
                                     : String(format: "%.0f Hz", value)
            }
            if param.unit == .seconds {
                return value >= 1 ? String(format: "%.2f s", value)
                                  : String(format: "%.0f ms", value * 1000)
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

    public override var currentPreset: AUAudioUnitPreset? {
        get { _currentPreset }
        set {
            guard let preset = newValue else { _currentPreset = nil; return }
            if preset.number >= 0 && preset.number < R50FactoryPresets.all.count {
                applyFactoryPreset(preset.number)
            }
            _currentPreset = preset
        }
    }

    func applyFactoryPreset(_ index: Int) {
        guard let tree = parameterTree,
              index >= 0, index < R50FactoryPresets.all.count else { return }
        let overrides = R50FactoryPresets.all[index].values
        for param in tree.allParameters {
            let v = overrides[param.address] ?? defaultState[param.address] ?? param.value
            param.setValue(v, originator: nil) // nil -> UI + DSP both refresh
        }
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

    // MARK: - Sample library

    /// Offline install — call from a loader queue, never from the render thread.
    func installSample(name: String, samples: [Float], sampleRate: Double,
                       rootKey: Int, loopMode: Int) -> Int {
        let data = samples.withUnsafeBytes { Data($0) }
        return kernel.installSampleNamed(name, samples: data,
                                         sampleRate: sampleRate,
                                         rootKey: rootKey, loopMode: loopMode)
    }

    var instrumentCount: Int { kernel.instrumentCount() }

    func instrumentName(at index: Int) -> String? {
        kernel.instrumentName(at: index)
    }
}
