import AppKit
import SwiftUI

struct R50ContentView: View {
    @ObservedObject var host: R50AudioUnitHost

    var body: some View {
        GeometryReader { geo in
            // Mirror the AU editor's fixed canvas without importing extension
            // UI sources into the standalone host target.
            let editorAspect: CGFloat = 1180.0 / 470.0
            let editorHeight = min(
                max(380, geo.size.width / editorAspect),
                max(380, geo.size.height - 146))
            VStack(spacing: 0) {
                if let viewController = host.viewController {
                    R50AUViewRepresentable(viewController: viewController)
                        .frame(height: editorHeight)
                } else {
                    Text(host.product.name)
                        .font(.largeTitle.bold())
                        .frame(maxWidth: .infinity, minHeight: editorHeight)
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
                Spacer(minLength: 0)
            }
        }
    }
}

struct R50AUViewRepresentable: NSViewControllerRepresentable {
    let viewController: NSViewController
    func makeNSViewController(context: Context) -> NSViewController { viewController }
    func updateNSViewController(_ nsViewController: NSViewController, context: Context) {}
}
