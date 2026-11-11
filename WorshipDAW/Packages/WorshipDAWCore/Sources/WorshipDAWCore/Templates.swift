import Foundation

/// Plantillas de inicio pensadas para equipos de alabanza.
public enum ProjectTemplate: String, CaseIterable, Identifiable, Sendable {
    case empty
    case fullBand
    case acoustic
    case vocals
    case multitracks

    public var id: String { rawValue }

    public var displayName: String {
        switch self {
        case .empty: "Vacío"
        case .fullBand: "Banda completa"
        case .acoustic: "Acústico"
        case .vocals: "Voces"
        case .multitracks: "Multitracks en vivo"
        }
    }

    public var subtitle: String {
        switch self {
        case .empty: "Empieza desde cero"
        case .fullBand: "Batería, bajo, guitarras, keys, pad y voces"
        case .acoustic: "Guitarra acústica, piano y voces"
        case .vocals: "Voz líder, coros y pad"
        case .multitracks: "Click, guía y stems para domingo"
        }
    }

    public var symbolName: String {
        switch self {
        case .empty: "square.dashed"
        case .fullBand: "music.mic"
        case .acoustic: "guitars"
        case .vocals: "waveform"
        case .multitracks: "square.stack.3d.up"
        }
    }

    var trackSpecs: [(String, TrackColor, TrackRole)] {
        switch self {
        case .empty:
            return [("Audio 1", .violet, .audio)]
        case .fullBand:
            return [
                ("Batería", .coral, .audio),
                ("Bajo", .amber, .audio),
                ("Guitarra Eléctrica", .rose, .audio),
                ("Guitarra Acústica", .lime, .audio),
                ("Keys", .sky, .audio),
                ("Pad", .indigo, .audio),
                ("Voz Líder", .violet, .audio),
                ("Coros", .teal, .audio),
            ]
        case .acoustic:
            return [
                ("Guitarra Acústica", .lime, .audio),
                ("Piano", .sky, .audio),
                ("Pad", .indigo, .audio),
                ("Voz Líder", .violet, .audio),
                ("Coros", .teal, .audio),
            ]
        case .vocals:
            return [
                ("Voz Líder", .violet, .audio),
                ("Coro 1", .teal, .audio),
                ("Coro 2", .mint, .audio),
                ("Pad", .indigo, .audio),
            ]
        case .multitracks:
            return [
                ("Guía", .slate, .guide),
                ("Batería", .coral, .audio),
                ("Percusión", .amber, .audio),
                ("Bajo", .amber, .audio),
                ("Guitarras", .rose, .audio),
                ("Keys", .sky, .audio),
                ("Pad", .indigo, .audio),
                ("Coros", .teal, .audio),
            ]
        }
    }

    /// Estructura típica de canción worship.
    var sectionSpecs: [(SectionKind, Int)] {
        switch self {
        case .empty, .vocals: []
        default: [(.intro, 4), (.verse, 8), (.preChorus, 4), (.chorus, 8), (.verse, 8), (.preChorus, 4),
                  (.chorus, 8), (.bridge, 8), (.chorus, 8), (.outro, 4)]
        }
    }

    public func makeProject(name: String, bpm: Double = 72, key: String = "G", beatsPerBar: Int = 4) -> Project {
        var project = Project(name: name, bpm: bpm, beatsPerBar: beatsPerBar, key: key)
        for (trackName, color, role) in trackSpecs {
            project.addTrack(name: trackName, color: color, role: role)
        }
        var verse = 0
        for (kind, bars) in sectionSpecs {
            let section = project.addSection(kind, bars: bars)
            if kind == .verse, let i = project.sections.firstIndex(where: { $0.id == section.id }) {
                verse += 1
                project.sections[i].name = "Verso \(verse)"
            }
        }
        return project
    }
}

public enum MusicalKey {
    public static let all: [String] = ["C", "C#", "Db", "D", "Eb", "E", "F", "F#", "Gb", "G", "Ab", "A", "Bb", "B"]
        .flatMap { [$0, $0 + "m"] }
}
