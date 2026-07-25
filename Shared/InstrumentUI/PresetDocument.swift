import Foundation

/// Product-neutral on-disk preset representation. Stable string keys allow a
/// product migration layer to translate old parameter addresses when needed.
struct PresetDocument: Codable {
    var name: String
    var values: [String: Float]
    var formatVersion: Int?
}

struct PresetPersistenceConfiguration {
    let productID: String
    let directoryName: String
    let currentFormatVersion: Int

    var applicationSupportPath: String {
        "\(productID)/\(directoryName)"
    }
}
