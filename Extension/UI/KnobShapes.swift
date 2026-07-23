//
//  KnobShapes.swift
//  Geometry for the rotary knobs: the value/track arc, the pointer line, and
//  the decorative tick ring. All share one angle convention so the pointer sits
//  exactly at the end of the value arc.
//
//  Angle convention: 0° = 3 o'clock, angle increases clockwise on screen
//  (SwiftUI's y-down space). Knobs sweep 270°, from 135° (7:30, lower-left)
//  clockwise to 405° (4:30, lower-right), leaving a gap at the bottom.
//

import SwiftUI

private let kKnobStart: Double = 135.0
private let kKnobSweep: Double = 270.0

/// Arc from the minimum position to `fraction` of the sweep (0...1).
struct KnobArc: Shape {
    var fraction: CGFloat
    var animatableData: CGFloat {
        get { fraction }
        set { fraction = newValue }
    }
    func path(in rect: CGRect) -> Path {
        var p = Path()
        let c = CGPoint(x: rect.midX, y: rect.midY)
        let r = min(rect.width, rect.height) / 2
        p.addArc(center: c, radius: r,
                 startAngle: .degrees(kKnobStart),
                 endAngle: .degrees(kKnobStart + kKnobSweep * Double(max(0, min(1, fraction)))),
                 clockwise: false)
        return p
    }
}

/// The indicator line pointing from the cap centre outward.
struct KnobPointer: Shape {
    var norm: CGFloat
    func path(in rect: CGRect) -> Path {
        var p = Path()
        let c = CGPoint(x: rect.midX, y: rect.midY)
        let r = min(rect.width, rect.height) / 2
        let ang = CGFloat((kKnobStart + kKnobSweep * Double(max(0, min(1, norm)))) * .pi / 180)
        p.move(to: CGPoint(x: c.x + r * 0.30 * cos(ang), y: c.y + r * 0.30 * sin(ang)))
        p.addLine(to: CGPoint(x: c.x + r * 0.96 * cos(ang), y: c.y + r * 0.96 * sin(ang)))
        return p
    }
}

/// Decorative tick marks around the knob (11 evenly across the 270° sweep).
struct KnobTicks: Shape {
    var count: Int = 11
    func path(in rect: CGRect) -> Path {
        var p = Path()
        let c = CGPoint(x: rect.midX, y: rect.midY)
        let r = min(rect.width, rect.height) / 2
        for i in 0..<count {
            let f = Double(i) / Double(count - 1)
            let ang = CGFloat((kKnobStart + kKnobSweep * f) * .pi / 180)
            p.move(to: CGPoint(x: c.x + r * 0.86 * cos(ang), y: c.y + r * 0.86 * sin(ang)))
            p.addLine(to: CGPoint(x: c.x + r * cos(ang), y: c.y + r * sin(ang)))
        }
        return p
    }
}
