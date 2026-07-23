//
//  SynthAudioUnit.swift
//  The AUAudioUnit subclass. Builds the parameter tree, wires it to the C++
//  DSP via SynthDSPKernelAdapter, and exposes the render block to the host.
//

import AVFoundation
import AudioToolbox

public final class SynthAudioUnit: AUAudioUnit {

    private let kernel = SynthDSPKernelAdapter()
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

        let tree = SynthParameters.buildTree()

        // Host -> DSP: apply parameter changes made outside the render thread.
        tree.implementorValueObserver = { [kernel] param, value in
            kernel.setParameter(param.address, value: value)
        }
        // DSP -> host: report current value (e.g. for the generic view).
        tree.implementorValueProvider = { [kernel] param in
            return kernel.value(forParameter: param.address)
        }
        // Nice human-readable strings for indexed / continuous parameters.
        tree.implementorStringFromValueCallback = { param, valuePtr in
            let value = valuePtr?.pointee ?? param.value
            if let strings = param.valueStrings {
                let idx = Int(value.rounded())
                if idx >= 0 && idx < strings.count { return strings[idx] }
            }
            // Normalised time parameters display as mapped seconds everywhere.
            if SynthTime.isTime(param.address) {
                return SynthTime.displayString(fromNorm: value)
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

    // MARK: - Presets (also surfaced in Logic's factory-preset menu)

    public override var factoryPresets: [AUAudioUnitPreset]? {
        FactoryPresets.all.enumerated().map { index, preset in
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
            if preset.number >= 0 && preset.number < FactoryPresets.all.count {
                applyFactoryPreset(preset.number)
            }
            _currentPreset = preset
        }
    }

    private func applyFactoryPreset(_ index: Int) {
        guard let tree = parameterTree else { return }
        let overrides = FactoryPresets.all[index].values
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
        kernel.setSampleRate(format.sampleRate,
                             channelCount: format.channelCount)
    }

    public override func deallocateRenderResources() {
        super.deallocateRenderResources()
    }

    public override func reset() {
        kernel.reset()
    }

    public override var internalRenderBlock: AUInternalRenderBlock {
        kernel.internalRenderBlock()
    }

    // Report a modest tail so released voices are not cut off.
    public override var tailTime: TimeInterval { 3.0 }
}
