//
//  R50Product.swift
//  R50's plug-in identity. Declared here rather than in Shared/InstrumentHost
//  so adding a product never touches another product's build inputs.
//
//  The component subtype must be unique per instrument — 'R50v' vs Hybrid 8's
//  'Hy8v'. The manufacturer code is shared: it identifies the vendor (Rytell),
//  which is what groups both plug-ins under one menu in Logic.
//

import AudioToolbox

extension InstrumentProduct {
    static let r50 = InstrumentProduct(
        name: "R50",
        productID: "com.johangorsjo.R50",
        componentDescription: AudioComponentDescription(
            componentType: kAudioUnitType_MusicDevice,
            componentSubType: fourCC("R50v"),
            componentManufacturer: fourCC("Jhgn"),
            componentFlags: 0,
            componentFlagsMask: 0),
        initialFactoryPreset: 0)
}
