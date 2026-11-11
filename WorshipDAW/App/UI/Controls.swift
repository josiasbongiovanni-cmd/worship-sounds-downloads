import SwiftUI
import WorshipDAWCore

/// Perilla rotativa con arrastre vertical. Doble toque = valor por defecto.
struct Knob: View {
    @Binding var value: Double
    var range: ClosedRange<Double>
    var defaultValue: Double = 0
    var label: String
    var tint: Color = Theme.accent
    var size: CGFloat = 34
    var format: (Double) -> String = { String(format: "%.1f", $0) }
    var onBegin: () -> Void = {}

    @State private var dragStart: Double?

    private var normalized: Double {
        (value - range.lowerBound) / (range.upperBound - range.lowerBound)
    }

    var body: some View {
        VStack(spacing: 3) {
            ZStack {
                Circle()
                    .trim(from: 0.125, to: 0.875)
                    .stroke(Color.white.opacity(0.08), style: StrokeStyle(lineWidth: 3, lineCap: .round))
                    .rotationEffect(.degrees(90))
                Circle()
                    .trim(from: 0.125, to: 0.125 + 0.75 * normalized)
                    .stroke(tint, style: StrokeStyle(lineWidth: 3, lineCap: .round))
                    .rotationEffect(.degrees(90))
                    .shadow(color: tint.opacity(0.5), radius: 3)
                Circle()
                    .fill(Theme.elevated)
                    .padding(6)
                Capsule()
                    .fill(Theme.textPrimary)
                    .frame(width: 2, height: size * 0.22)
                    .offset(y: -size * 0.17)
                    .rotationEffect(.degrees(-135 + 270 * normalized))
            }
            .frame(width: size, height: size)
            .contentShape(Circle())
            .gesture(
                DragGesture(minimumDistance: 1)
                    .onChanged { g in
                        if dragStart == nil {
                            dragStart = value
                            onBegin()
                        }
                        let span = range.upperBound - range.lowerBound
                        let delta = Double(-g.translation.height + g.translation.width * 0.5) / 160 * span
                        value = min(range.upperBound, max(range.lowerBound, (dragStart ?? value) + delta))
                    }
                    .onEnded { _ in dragStart = nil }
            )
            .onTapGesture(count: 2) {
                onBegin()
                value = defaultValue
            }
            Text(dragStart != nil ? format(value) : label)
                .font(.system(size: 9, weight: .semibold, design: .rounded))
                .foregroundStyle(Theme.textSecondary)
                .lineLimit(1)
        }
        .accessibilityElement()
        .accessibilityLabel(label)
        .accessibilityValue(format(value))
        .accessibilityAdjustableAction { direction in
            let step = (range.upperBound - range.lowerBound) / 40
            switch direction {
            case .increment: value = min(range.upperBound, value + step)
            case .decrement: value = max(range.lowerBound, value - step)
            @unknown default: break
            }
        }
    }
}

/// Fader vertical en dB con escala no lineal (más resolución cerca de 0 dB).
struct Fader: View {
    @Binding var db: Double
    var meterLevel: Float = 0
    var tint: Color = Theme.accent
    var onBegin: () -> Void = {}

    @State private var dragStartPos: Double?

    static let maxDB = 6.0

    static func position(for db: Double) -> Double {
        if db <= Decibels.silence { return 0 }
        // Curva: 0 dB ≈ 0.78 del recorrido.
        let x = (db - Decibels.silence) / (maxDB - Decibels.silence)
        return pow(x, 2.2)
    }

    static func db(for position: Double) -> Double {
        let p = min(1, max(0, position))
        if p <= 0.0005 { return Decibels.silence }
        return Decibels.silence + pow(p, 1 / 2.2) * (maxDB - Decibels.silence)
    }

    var body: some View {
        GeometryReader { geo in
            let h = geo.size.height
            let pos = Fader.position(for: db)
            let meterDB = Decibels.fromLinear(meterLevel)
            let meterPos = Fader.position(for: meterDB)
            ZStack(alignment: .bottom) {
                // Riel
                RoundedRectangle(cornerRadius: 3)
                    .fill(Color.black.opacity(0.45))
                    .frame(width: 6)
                // Medidor
                RoundedRectangle(cornerRadius: 3)
                    .fill(
                        LinearGradient(colors: [Color(red: 0.3, green: 0.9, blue: 0.6), Color(red: 0.95, green: 0.85, blue: 0.3), Theme.record],
                                       startPoint: .bottom, endPoint: .top)
                    )
                    .frame(width: 6, height: max(0, h * meterPos))
                    .mask(alignment: .bottom) { Rectangle().frame(height: h) }
                // Marca de 0 dB
                Rectangle()
                    .fill(Theme.textTertiary)
                    .frame(width: 18, height: 1)
                    .offset(y: -h * Fader.position(for: 0))
                // Perilla del fader
                RoundedRectangle(cornerRadius: 5, style: .continuous)
                    .fill(LinearGradient(colors: [Color.white.opacity(0.95), Color.white.opacity(0.7)], startPoint: .top, endPoint: .bottom))
                    .frame(width: 30, height: 16)
                    .overlay(Rectangle().fill(tint).frame(height: 2))
                    .shadow(color: .black.opacity(0.5), radius: 3, y: 2)
                    .offset(y: -(h - 16) * pos)
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .bottom)
            .contentShape(Rectangle())
            .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { g in
                        if dragStartPos == nil {
                            dragStartPos = pos
                            onBegin()
                        }
                        let newPos = (dragStartPos ?? pos) - Double(g.translation.height / max(1, h - 16))
                        db = (Fader.db(for: newPos) * 10).rounded() / 10
                    }
                    .onEnded { _ in dragStartPos = nil }
            )
            .onTapGesture(count: 2) {
                onBegin()
                db = 0
            }
        }
        .accessibilityElement()
        .accessibilityLabel("Volumen")
        .accessibilityValue(String(format: "%.1f dB", db))
        .accessibilityAdjustableAction { direction in
            switch direction {
            case .increment: db = min(Fader.maxDB, db + 1)
            case .decrement: db = max(Decibels.silence, db - 1)
            @unknown default: break
            }
        }
    }
}

/// Medidor horizontal compacto (cabecera de pista).
struct MiniMeter: View {
    let level: Float

    var body: some View {
        GeometryReader { geo in
            let pos = Fader.position(for: Decibels.fromLinear(level))
            ZStack(alignment: .leading) {
                Capsule().fill(Color.black.opacity(0.4))
                Capsule()
                    .fill(LinearGradient(colors: [Color(red: 0.3, green: 0.9, blue: 0.6), Color(red: 0.95, green: 0.85, blue: 0.3), Theme.record],
                                         startPoint: .leading, endPoint: .trailing))
                    .frame(width: geo.size.width * pos)
            }
        }
        .frame(height: 3)
    }
}

func formatDB(_ db: Double) -> String {
    db <= Decibels.silence ? "-∞" : String(format: "%+.1f", db)
}

func formatPan(_ pan: Double) -> String {
    if abs(pan) < 0.01 { return "C" }
    return pan < 0 ? "L\(Int(-pan * 100))" : "R\(Int(pan * 100))"
}
