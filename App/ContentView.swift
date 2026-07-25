import AppKit
import SwiftUI

struct ContentView: View {
    @ObservedObject var host: AudioUnitHost

    var body: some View {
        VStack(spacing: 0) {
            if let viewController = host.viewController {
                AUViewRepresentable(viewController: viewController)
                    .frame(minHeight: 420)
            } else {
                Text(host.product.name)
                    .font(.largeTitle.bold())
                    .frame(maxWidth: .infinity, minHeight: 420)
                    .background(Color(red: 0.11, green: 0.11, blue: 0.13))
            }

            HStack(spacing: 10) {
                Text(host.status)
                Text("·").foregroundColor(.secondary.opacity(0.5))
                Text(host.midiInfo)
                Text("·").foregroundColor(.secondary.opacity(0.5))
                Text(host.typingInfo)
            }
            .font(.caption)
            .foregroundColor(.secondary)
            .padding(.vertical, 4)

            PerformanceKeyboardView(sink: host)
        }
    }
}

struct AUViewRepresentable: NSViewControllerRepresentable {
    let viewController: NSViewController
    func makeNSViewController(context: Context) -> NSViewController { viewController }
    func updateNSViewController(_ nsViewController: NSViewController, context: Context) {}
}
