//
//  R50App.swift
//  Host application. Its main job is to *contain* the R50 Audio Unit extension
//  so macOS registers it for Logic and other hosts. It also loads the AU
//  in-process so you can audition the synth here with an on-screen keyboard.
//

import SwiftUI

@main
struct R50App: App {
    @StateObject private var host = R50AudioUnitHost(product: .r50)

    var body: some Scene {
        WindowGroup("R50") {
            R50ContentView(host: host)
                .frame(minWidth: 900, minHeight: 460)
        }
        // Same width as Hybrid8's window. The height is the 680-point fascia
        // plus the status line and performance keyboard beneath it, so the
        // editor opens at 1:1 rather than being scaled down, and no empty
        // chassis band is left between the panels and the keyboard.
        .defaultSize(width: 1700, height: 836)
        .windowResizability(.automatic)
    }
}
