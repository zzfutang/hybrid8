import AppKit
import SwiftUI

struct R50ContentView: View {
    @ObservedObject var host: R50AudioUnitHost

    var body: some View {
        VStack(spacing: 0) {
            if let viewController = host.viewController {
                R50AUViewRepresentable(viewController: viewController)
                    .frame(minHeight: 380)
            } else {
                Text(host.product.name)
                    .font(.largeTitle.bold())
                    .frame(maxWidth: .infinity, minHeight: 380)
                    .background(Color(white: 0.10))
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

struct R50AUViewRepresentable: NSViewControllerRepresentable {
    let viewController: NSViewController
    func makeNSViewController(context: Context) -> NSViewController { viewController }
    func updateNSViewController(_ nsViewController: NSViewController, context: Context) {}
}
