import SwiftUI
import WorshipDAWCore

/// Sistema visual: oscuro, cálido y con acentos luminosos (pensado para salas con poca luz).
enum Theme {
    static let background = Color(red: 0.043, green: 0.043, blue: 0.067)    // #0B0B11
    static let panel = Color(red: 0.075, green: 0.075, blue: 0.11)          // #13131C
    static let elevated = Color(red: 0.11, green: 0.11, blue: 0.16)         // #1C1C29
    static let lane = Color(red: 0.06, green: 0.06, blue: 0.09)
    static let laneAlt = Color(red: 0.067, green: 0.067, blue: 0.1)
    static let stroke = Color.white.opacity(0.07)
    static let strokeStrong = Color.white.opacity(0.14)
    static let textPrimary = Color.white.opacity(0.94)
    static let textSecondary = Color.white.opacity(0.58)
    static let textTertiary = Color.white.opacity(0.34)

    static let accent = Color(red: 0.60, green: 0.47, blue: 1.0)            // violeta
    static let accent2 = Color(red: 0.33, green: 0.78, blue: 1.0)           // celeste
    static let record = Color(red: 1.0, green: 0.27, blue: 0.36)
    static let solo = Color(red: 1.0, green: 0.78, blue: 0.25)
    static let mute = Color(red: 0.98, green: 0.45, blue: 0.29)
    static let playhead = Color.white
    static let loop = Color(red: 0.33, green: 0.78, blue: 1.0)

    static let accentGradient = LinearGradient(colors: [accent, accent2], startPoint: .topLeading, endPoint: .bottomTrailing)

    static let corner: CGFloat = 12
    static let smallCorner: CGFloat = 7

    static func display(_ size: CGFloat, weight: Font.Weight = .semibold) -> Font {
        .system(size: size, weight: weight, design: .rounded)
    }

    static func mono(_ size: CGFloat, weight: Font.Weight = .medium) -> Font {
        .system(size: size, weight: weight, design: .monospaced)
    }
}

extension TrackColor {
    var color: Color {
        switch self {
        case .violet: Color(red: 0.62, green: 0.48, blue: 1.0)
        case .indigo: Color(red: 0.42, green: 0.47, blue: 1.0)
        case .sky: Color(red: 0.33, green: 0.72, blue: 1.0)
        case .teal: Color(red: 0.2, green: 0.83, blue: 0.8)
        case .mint: Color(red: 0.38, green: 0.9, blue: 0.62)
        case .lime: Color(red: 0.7, green: 0.9, blue: 0.3)
        case .amber: Color(red: 1.0, green: 0.74, blue: 0.27)
        case .coral: Color(red: 1.0, green: 0.5, blue: 0.4)
        case .rose: Color(red: 1.0, green: 0.42, blue: 0.62)
        case .slate: Color(red: 0.6, green: 0.64, blue: 0.74)
        }
    }

    var displayName: String {
        switch self {
        case .violet: "Violeta"
        case .indigo: "Índigo"
        case .sky: "Cielo"
        case .teal: "Turquesa"
        case .mint: "Menta"
        case .lime: "Lima"
        case .amber: "Ámbar"
        case .coral: "Coral"
        case .rose: "Rosa"
        case .slate: "Pizarra"
        }
    }
}

extension SectionKind {
    var color: Color { defaultColor.color }
}

// MARK: - Modificadores

struct PanelBackground: ViewModifier {
    var corner: CGFloat = Theme.corner
    func body(content: Content) -> some View {
        content
            .background(
                RoundedRectangle(cornerRadius: corner, style: .continuous)
                    .fill(Theme.panel)
            )
            .overlay(
                RoundedRectangle(cornerRadius: corner, style: .continuous)
                    .strokeBorder(Theme.stroke, lineWidth: 1)
            )
    }
}

extension View {
    func panel(corner: CGFloat = Theme.corner) -> some View {
        modifier(PanelBackground(corner: corner))
    }
}

/// Botón de pastilla (M / S / R, etc.).
struct PillToggle: View {
    let label: String
    let isOn: Bool
    let tint: Color
    var size: CGFloat = 24
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            Text(label)
                .font(.system(size: size * 0.46, weight: .heavy, design: .rounded))
                .frame(width: size, height: size)
                .foregroundStyle(isOn ? Color.black.opacity(0.85) : Theme.textSecondary)
                .background(
                    RoundedRectangle(cornerRadius: 6, style: .continuous)
                        .fill(isOn ? tint : Color.white.opacity(0.06))
                )
                .overlay(
                    RoundedRectangle(cornerRadius: 6, style: .continuous)
                        .strokeBorder(isOn ? tint.opacity(0.0) : Theme.stroke, lineWidth: 1)
                )
                .shadow(color: isOn ? tint.opacity(0.55) : .clear, radius: 6)
        }
        .buttonStyle(.plain)
        .contentShape(Rectangle())
    }
}

/// Botón de icono del transporte.
struct TransportButton: View {
    let systemName: String
    var isActive = false
    var tint: Color = Theme.textPrimary
    var size: CGFloat = 34
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            Image(systemName: systemName)
                .font(.system(size: size * 0.42, weight: .semibold))
                .foregroundStyle(isActive ? tint : Theme.textPrimary.opacity(0.85))
                .frame(width: size, height: size)
                .background(
                    Circle().fill(isActive ? tint.opacity(0.18) : Color.white.opacity(0.05))
                )
                .overlay(Circle().strokeBorder(isActive ? tint.opacity(0.6) : Theme.stroke, lineWidth: 1))
        }
        .buttonStyle(.plain)
    }
}

extension View {
    /// Tamaño mínimo para hojas en Mac (en iPhone la hoja ocupa la pantalla).
    @ViewBuilder
    func macSheetFrame(width: CGFloat, height: CGFloat) -> some View {
        #if os(macOS)
        frame(minWidth: width, minHeight: height)
        #else
        self
        #endif
    }
}
