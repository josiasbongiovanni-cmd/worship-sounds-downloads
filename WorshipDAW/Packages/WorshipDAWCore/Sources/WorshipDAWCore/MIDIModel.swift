import Foundation

/// Nota MIDI dentro de un ítem. Los tiempos son segundos relativos al inicio del ítem.
public struct MIDINote: Identifiable, Codable, Hashable, Sendable {
    public var id: UUID
    public var pitch: Int        // 0…127 (60 = Do central)
    public var velocity: Int     // 1…127
    public var channel: Int      // 1…16
    public var start: TimeInterval
    public var duration: TimeInterval

    public init(id: UUID = UUID(), pitch: Int, velocity: Int, channel: Int = 1, start: TimeInterval, duration: TimeInterval) {
        self.id = id
        self.pitch = pitch
        self.velocity = velocity
        self.channel = channel
        self.start = start
        self.duration = duration
    }

    public var end: TimeInterval { start + duration }

    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        id = try c.decodeIfPresent(UUID.self, forKey: .id) ?? UUID()
        pitch = try c.decode(Int.self, forKey: .pitch)
        velocity = try c.decodeIfPresent(Int.self, forKey: .velocity) ?? 100
        channel = try c.decodeIfPresent(Int.self, forKey: .channel) ?? 1
        start = try c.decode(TimeInterval.self, forKey: .start)
        duration = try c.decode(TimeInterval.self, forKey: .duration)
    }

    /// Nombre de la nota en notación latina: Do4 = 60.
    public static func name(for pitch: Int) -> String {
        let names = ["Do", "Do#", "Re", "Re#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si"]
        return names[((pitch % 12) + 12) % 12] + "\(pitch / 12 - 1)"
    }

    public static func isBlackKey(_ pitch: Int) -> Bool {
        [1, 3, 6, 8, 10].contains(((pitch % 12) + 12) % 12)
    }
}

/// Ítem MIDI en una pista de instrumento.
public struct MIDIClip: Identifiable, Codable, Hashable, Sendable {
    public var id: UUID
    public var name: String
    public var start: TimeInterval
    public var duration: TimeInterval
    public var notes: [MIDINote]

    public init(id: UUID = UUID(), name: String, start: TimeInterval, duration: TimeInterval, notes: [MIDINote] = []) {
        self.id = id
        self.name = name
        self.start = start
        self.duration = duration
        self.notes = notes
    }

    public var end: TimeInterval { start + duration }

    /// Rango de notas usado (para dibujar el mini piano roll).
    public var pitchRange: ClosedRange<Int> {
        guard let lo = notes.map(\.pitch).min(), let hi = notes.map(\.pitch).max() else { return 48...72 }
        return lo...max(hi, lo + 11)
    }
}

/// Un evento MIDI de canal en la línea de tiempo (segundos absolutos).
public struct TimedMIDIEvent: Hashable, Sendable {
    public var time: TimeInterval
    public var status: UInt8
    public var data1: UInt8
    public var data2: UInt8

    public init(time: TimeInterval, status: UInt8, data1: UInt8, data2: UInt8) {
        self.time = time
        self.status = status
        self.data1 = data1
        self.data2 = data2
    }

    public var isNoteOn: Bool { status & 0xF0 == 0x90 && data2 > 0 }
    public var isNoteOff: Bool { status & 0xF0 == 0x80 || (status & 0xF0 == 0x90 && data2 == 0) }
    public var channel: Int { Int(status & 0x0F) + 1 }
}

public enum MIDIRecording {
    /// Convierte eventos grabados (note on / note off) en notas, a partir de `start`.
    /// Las notas que siguen sonando al detener terminan en `end`. Lo anterior a `start` (cuenta previa) se descarta.
    public static func notes(from events: [TimedMIDIEvent], start: TimeInterval, end: TimeInterval) -> [MIDINote] {
        var open: [Int: [(time: TimeInterval, velocity: Int)]] = [:]
        var notes: [MIDINote] = []
        func key(_ e: TimedMIDIEvent) -> Int { e.channel << 8 | Int(e.data1) }
        func close(_ k: Int, at time: TimeInterval) {
            guard var stack = open[k], !stack.isEmpty else { return }
            let first = stack.removeFirst()
            open[k] = stack
            let s = max(first.time, start)
            let e = min(time, end)
            guard e > s else { return }
            notes.append(MIDINote(pitch: k & 0xFF, velocity: first.velocity, channel: k >> 8,
                                  start: s - start, duration: e - s))
        }
        for e in events.sorted(by: { $0.time < $1.time }) {
            if e.isNoteOn {
                open[key(e), default: []].append((e.time, Int(e.data2)))
            } else if e.isNoteOff {
                close(key(e), at: e.time)
            }
        }
        for (k, stack) in open { for _ in stack { close(k, at: end) } }
        return notes.sorted { ($0.start, $0.pitch) < ($1.start, $1.pitch) }
    }

    /// Eventos para reproducir un ítem, en tiempo absoluto, a partir de `from` (las notas ya empezadas se omiten).
    public static func playbackEvents(for clip: MIDIClip, from: TimeInterval) -> [TimedMIDIEvent] {
        var events: [TimedMIDIEvent] = []
        for n in clip.notes {
            let on = clip.start + n.start
            let off = min(clip.start + n.end, clip.end)
            guard on >= from - 1e-6, off > on else { continue }
            let ch = UInt8(max(0, min(15, n.channel - 1)))
            events.append(TimedMIDIEvent(time: on, status: 0x90 | ch, data1: UInt8(clamping: n.pitch), data2: UInt8(clamping: max(1, n.velocity))))
            events.append(TimedMIDIEvent(time: off, status: 0x80 | ch, data1: UInt8(clamping: n.pitch), data2: 0))
        }
        return events
    }
}

public extension Project {
    func midiClipLocation(_ id: UUID) -> (track: Int, clip: Int)? {
        for (t, track) in tracks.enumerated() {
            if let c = track.midiClips.firstIndex(where: { $0.id == id }) { return (t, c) }
        }
        return nil
    }

    mutating func addMIDIClip(_ clip: MIDIClip, toTrack trackID: UUID) {
        guard let t = trackIndex(trackID) else { return }
        tracks[t].midiClips.append(clip)
        tracks[t].midiClips.sort { $0.start < $1.start }
    }

    mutating func removeMIDIClip(_ id: UUID) {
        guard let loc = midiClipLocation(id) else { return }
        tracks[loc.track].midiClips.remove(at: loc.clip)
    }

    mutating func moveMIDIClip(_ id: UUID, to start: TimeInterval, trackID: UUID? = nil) {
        guard let loc = midiClipLocation(id) else { return }
        var clip = tracks[loc.track].midiClips[loc.clip]
        clip.start = max(0, start)
        if let trackID, let dest = trackIndex(trackID), dest != loc.track, tracks[dest].isInstrument {
            tracks[loc.track].midiClips.remove(at: loc.clip)
            tracks[dest].midiClips.append(clip)
            tracks[dest].midiClips.sort { $0.start < $1.start }
        } else {
            tracks[loc.track].midiClips[loc.clip] = clip
            tracks[loc.track].midiClips.sort { $0.start < $1.start }
        }
    }

    @discardableResult
    mutating func duplicateMIDIClip(_ id: UUID) -> UUID? {
        guard let loc = midiClipLocation(id) else { return nil }
        var copy = tracks[loc.track].midiClips[loc.clip]
        copy.id = UUID()
        copy.start = copy.end
        tracks[loc.track].midiClips.append(copy)
        tracks[loc.track].midiClips.sort { $0.start < $1.start }
        return copy.id
    }
}
