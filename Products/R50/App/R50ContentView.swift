import AppKit
import SwiftUI

struct R50ContentView: View {
    @ObservedObject var host: R50AudioUnitHost

    var body: some View {
        ZStack(alignment: .topLeading) {
            // Invisible key sink: consumes the plain keys musical typing uses
            // so the window's own keyDown never answers them with the beep.
            MusicalTypingKeyCatcher()
                .frame(width: 0, height: 0)
            // Hand the editor the room and let it fit its own fascia to it,
            // exactly as Hybrid8's host does. Restating the fascia's aspect
            // here and clamping the height to it made the canvas wider than
            // the space it was drawn into, so the panel sat off-centre and
            // clipped on the right edge.
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
}

struct R50AUViewRepresentable: NSViewControllerRepresentable {
    let viewController: NSViewController
    func makeNSViewController(context: Context) -> NSViewController { viewController }
    func updateNSViewController(_ nsViewController: NSViewController, context: Context) {}
}
