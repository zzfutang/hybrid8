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
                .frame(minWidth: 720, minHeight: 520)
        }
        .defaultSize(width: 960, height: 720)
        .windowResizability(.automatic)
    }
}
