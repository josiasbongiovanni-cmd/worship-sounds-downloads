import Foundation

/// Resolución de la grilla del editor MIDI (como el menú "Grid" de REAPER).
public enum MIDIGrid: String, CaseIterable, Identifiable, Sendable {
    case bar = "1"
    case half = "1/2"
    case quarter = "1/4"
    case eighth = "1/8"
    case sixteenth = "1/16"
    case thirtySecond = "1/32"
    case eighthTriplet = "1/8 T"
    case sixteenthTriplet = "1/16 T"

    public var id: String { rawValue }

    /// Duración de una celda en negras.
    public var beats: Double {
        switch self {
        case .bar: 4
        case .half: 2
        case .quarter: 1
        case .eighth: 0.5
        case .sixteenth: 0.25
        case .thirtySecond: 0.125
        case .eighthTriplet: 1.0 / 3
        case .sixteenthTriplet: 1.0 / 6
        }
    }

    public func seconds(in timeline: Timeline) -> TimeInterval {
        // "1" = un compás real, según el compás de la canción.
        self == .bar ? timeline.secondsPerBar : beats * timeline.secondsPerBeat
    }
}

public extension MIDIClip {
    /// Ajusta a la grilla del proyecto (el inicio del ítem puede no estar en la grilla).
    /// - strength: 0…1 (1 = cuantización total, como "Strength" en REAPER).
    mutating func quantize(_ ids: Set<UUID>? = nil, grid: TimeInterval, strength: Double = 1, lengths: Bool = false) {
        guard grid > 0 else { return }
        let s = max(0, min(1, strength))
        for i in notes.indices where ids?.contains(notes[i].id) ?? true {
            let absolute = start + notes[i].start
            let target = (absolute / grid).rounded() * grid
            notes[i].start = max(0, absolute + (target - absolute) * s - start)
            if lengths {
                let q = max(grid, (notes[i].duration / grid).rounded() * grid)
                notes[i].duration = notes[i].duration + (q - notes[i].duration) * s
            }
        }
        sortNotes()
    }

    mutating func transpose(_ ids: Set<UUID>, semitones: Int) {
        for i in notes.indices where ids.contains(notes[i].id) {
            notes[i].pitch = max(0, min(127, notes[i].pitch + semitones))
        }
    }

    /// Cada nota dura hasta que empieza la siguiente (de cualquier altura).
    mutating func legato(_ ids: Set<UUID>? = nil) {
        sortNotes()
        let starts = Array(Set(notes.map(\.start))).sorted()
        for i in notes.indices where ids?.contains(notes[i].id) ?? true {
            if let next = starts.first(where: { $0 > notes[i].start + 1e-6 }) {
                notes[i].duration = next - notes[i].start
            } else {
                notes[i].duration = max(notes[i].duration, duration - notes[i].start)
            }
        }
    }

    mutating func setVelocity(_ ids: Set<UUID>, to value: Int) {
        for i in notes.indices where ids.contains(notes[i].id) {
            notes[i].velocity = max(1, min(127, value))
        }
    }

    /// Mueve notas en tiempo (segundos) y altura (semitonos).
    mutating func move(_ ids: Set<UUID>, by dt: TimeInterval, semitones: Int) {
        for i in notes.indices where ids.contains(notes[i].id) {
            notes[i].start = max(0, notes[i].start + dt)
            notes[i].pitch = max(0, min(127, notes[i].pitch + semitones))
        }
        sortNotes()
        fitToNotes()
    }

    mutating func resize(_ ids: Set<UUID>, by dt: TimeInterval, minimum: TimeInterval) {
        for i in notes.indices where ids.contains(notes[i].id) {
            notes[i].duration = max(minimum, notes[i].duration + dt)
        }
        fitToNotes()
    }

    mutating func removeNotes(_ ids: Set<UUID>) {
        notes.removeAll { ids.contains($0.id) }
    }

    /// Duplica las notas justo después de la última seleccionada. Devuelve los ids nuevos.
    @discardableResult
    mutating func duplicateNotes(_ ids: Set<UUID>) -> Set<UUID> {
        let chosen = notes.filter { ids.contains($0.id) }
        guard let first = chosen.map(\.start).min(), let last = chosen.map(\.end).max() else { return [] }
        let shift = last - first
        var created = Set<UUID>()
        for n in chosen {
            var copy = n
            copy.id = UUID()
            copy.start += shift
            notes.append(copy)
            created.insert(copy.id)
        }
        sortNotes()
        fitToNotes()
        return created
    }

    @discardableResult
    mutating func addNote(pitch: Int, at time: TimeInterval, length: TimeInterval, velocity: Int = 100) -> UUID {
        let note = MIDINote(pitch: max(0, min(127, pitch)), velocity: velocity, start: max(0, time), duration: max(0.01, length))
        notes.append(note)
        sortNotes()
        fitToNotes()
        return note.id
    }

    /// Alarga el ítem si alguna nota queda fuera (como al dibujar más allá del final en REAPER).
    mutating func fitToNotes() {
        if let end = notes.map(\.end).max(), end > duration { duration = end }
    }

    mutating func sortNotes() {
        notes.sort { ($0.start, $0.pitch) < ($1.start, $1.pitch) }
    }
}
