//
//  AudioUnitViewController.swift
//  Principal class of the AU extension. Implements AUAudioUnitFactory (creates
//  the SynthAudioUnit) and hosts the SwiftUI editor view.
//

import AppKit
import CoreAudioKit
import SwiftUI

public class AudioUnitViewController: AUViewController, AUAudioUnitFactory {

    var audioUnit: AUAudioUnit?

    public override func loadView() {
        self.view = NSView(frame: NSRect(x: 0, y: 0, width: 1360, height: 890))
        self.preferredContentSize = NSSize(width: 1360, height: 890)
    }

    public override func viewDidLoad() {
        super.viewDidLoad()
        if audioUnit != nil { setupUI() }
    }

    public func createAudioUnit(with componentDescription: AudioComponentDescription) throws -> AUAudioUnit {
        let au = try SynthAudioUnit(componentDescription: componentDescription, options: [])
        audioUnit = au
        DispatchQueue.main.async { [weak self] in
            if self?.isViewLoaded == true { self?.setupUI() }
        }
        return au
    }

    private func setupUI() {
        guard let tree = audioUnit?.parameterTree else { return }
        view.subviews.forEach { $0.removeFromSuperview() }
        let host = NSHostingView(rootView: SynthView(model: ParameterModel(tree: tree)))
        host.frame = view.bounds
        host.autoresizingMask = [.width, .height]
        view.addSubview(host)
    }
}
