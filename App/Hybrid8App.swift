//
//  Hybrid8App.swift
//  Host application. Its main job is to *contain* the Audio Unit extension so
//  macOS registers it for Logic and other hosts. It also loads the AU in-process
//  so you can audition the synth here with an on-screen keyboard.
//

import SwiftUI

@main
struct Hybrid8App: App {
    @StateObject private var host = SynthHost()

    var body: some Scene {
        WindowGroup("Hybrid 8") {
            ContentView(host: host)
                .frame(minWidth: 760, minHeight: 620)
        }
        .defaultSize(width: 1700, height: 974)
        .windowResizability(.automatic)
    }
}
