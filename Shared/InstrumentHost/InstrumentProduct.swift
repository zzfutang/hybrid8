import AudioToolbox

struct InstrumentProduct {
    let name: String
    let productID: String
    let componentDescription: AudioComponentDescription
    let initialFactoryPreset: Int?

    static func fourCC(_ string: String) -> FourCharCode {
        string.utf8.prefix(4).reduce(0) { ($0 << 8) + FourCharCode($1) }
    }
}

extension InstrumentProduct {
    static let hybrid8 = InstrumentProduct(
        name: "Hybrid 8",
        productID: "com.johangorsjo.Hybrid8",
        componentDescription: AudioComponentDescription(
            componentType: kAudioUnitType_MusicDevice,
            componentSubType: fourCC("Hy8v"),
            componentManufacturer: fourCC("Jhgn"),
            componentFlags: 0,
            componentFlagsMask: 0),
        initialFactoryPreset: 0)
}
