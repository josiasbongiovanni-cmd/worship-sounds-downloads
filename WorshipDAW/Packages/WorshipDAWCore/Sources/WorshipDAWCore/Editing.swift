import Foundation

/// Operaciones de edición puras sobre el modelo (testeables sin audio).
public extension Project {

    // MARK: Pistas

    @discardableResult
    mutating func addTrack(name: String, color: TrackColor? = nil, role: TrackRole = .audio) -> Track {
        let palette = TrackColor.allCases
        let track = Track(name: name, color: color ?? palette[tracks.count % palette.count], role: role)
        tracks.append(track)
        return track
    }

    mutating func removeTrack(_ id: UUID) {
        tracks.removeAll { $0.id == id }
    }

    mutating func moveTracks(from source: IndexSet, to destination: Int) {
        let moving = source.sorted().map { tracks[$0] }
        var remaining = tracks.enumerated().filter { !source.contains($0.offset) }.map(\.element)
        let insertAt = destination - source.filter { $0 < destination }.count
        remaining.insert(contentsOf: moving, at: max(0, min(insertAt, remaining.count)))
        tracks = remaining
    }

    // MARK: Clips

    mutating func addClip(_ clip: Clip, toTrack trackID: UUID) {
        guard let t = trackIndex(trackID) else { return }
        tracks[t].clips.append(clip)
        tracks[t].clips.sort { $0.start < $1.start }
    }

    mutating func removeClip(_ clipID: UUID) {
        guard let loc = clipLocation(clipID) else { return }
        tracks[loc.track].clips.remove(at: loc.clip)
    }

    mutating func moveClip(_ clipID: UUID, to start: TimeInterval, trackID: UUID? = nil) {
        guard let loc = clipLocation(clipID) else { return }
        var clip = tracks[loc.track].clips[loc.clip]
        clip.start = max(0, start)
        if let trackID, let dest = trackIndex(trackID), dest != loc.track {
            tracks[loc.track].clips.remove(at: loc.clip)
            tracks[dest].clips.append(clip)
            tracks[dest].clips.sort { $0.start < $1.start }
        } else {
            tracks[loc.track].clips[loc.clip] = clip
            tracks[loc.track].clips.sort { $0.start < $1.start }
        }
    }

    /// Recorta el borde izquierdo del clip a una nueva posición de inicio (no mueve el audio).
    mutating func trimClipStart(_ clipID: UUID, to newStart: TimeInterval) {
        guard let loc = clipLocation(clipID) else { return }
        var clip = tracks[loc.track].clips[loc.clip]
        let earliest = clip.start - clip.offset
        let latest = clip.end - 0.01
        let s = min(max(newStart, earliest, 0), latest)
        let delta = s - clip.start
        clip.start = s
        clip.offset += delta
        clip.duration -= delta
        tracks[loc.track].clips[loc.clip] = clip
    }

    /// Recorta el borde derecho del clip.
    mutating func trimClipEnd(_ clipID: UUID, to newEnd: TimeInterval) {
        guard let loc = clipLocation(clipID) else { return }
        var clip = tracks[loc.track].clips[loc.clip]
        let maxEnd = clip.start + (clip.sourceDuration - clip.offset)
        let e = min(max(newEnd, clip.start + 0.01), maxEnd)
        clip.duration = e - clip.start
        tracks[loc.track].clips[loc.clip] = clip
    }

    /// Divide un clip en `time`. Devuelve el id del clip derecho creado.
    @discardableResult
    mutating func splitClip(_ clipID: UUID, at time: TimeInterval) -> UUID? {
        guard let loc = clipLocation(clipID) else { return nil }
        let clip = tracks[loc.track].clips[loc.clip]
        guard time > clip.start + 0.001, time < clip.end - 0.001 else { return nil }
        var left = clip
        left.duration = time - clip.start
        var right = clip
        right.id = UUID()
        right.start = time
        right.offset = clip.offset + left.duration
        right.duration = clip.duration - left.duration
        tracks[loc.track].clips[loc.clip] = left
        tracks[loc.track].clips.insert(right, at: loc.clip + 1)
        return right.id
    }

    /// Divide todos los clips que cruzan `time` (como la tecla S de REAPER sin selección).
    mutating func splitAll(at time: TimeInterval) {
        let ids = tracks.flatMap { $0.clips }.filter { $0.start < time && $0.end > time }.map(\.id)
        for id in ids { splitClip(id, at: time) }
    }

    @discardableResult
    mutating func duplicateClip(_ clipID: UUID) -> UUID? {
        guard let loc = clipLocation(clipID) else { return nil }
        var copy = tracks[loc.track].clips[loc.clip]
        copy.id = UUID()
        copy.start = copy.end
        tracks[loc.track].clips.append(copy)
        tracks[loc.track].clips.sort { $0.start < $1.start }
        return copy.id
    }

    // MARK: Marcadores

    /// Inserta un marcador con el primer número libre (como M en REAPER).
    @discardableResult
    mutating func addMarker(at time: TimeInterval, name: String = "") -> Marker {
        let used = Set(markers.map(\.number))
        var n = 1
        while used.contains(n) { n += 1 }
        let marker = Marker(number: n, name: name, time: max(0, time))
        markers.append(marker)
        markers.sort { $0.time < $1.time }
        return marker
    }

    mutating func removeMarker(_ id: UUID) {
        markers.removeAll { $0.id == id }
    }

    func marker(number: Int) -> Marker? {
        markers.first { $0.number == number }
    }

    /// Siguiente punto de navegación (marcador o inicio de sección) después de `time`.
    func nextStop(after time: TimeInterval) -> TimeInterval? {
        (markers.map(\.time) + sections.map(\.start)).filter { $0 > time + 0.05 }.min()
    }

    func previousStop(before time: TimeInterval) -> TimeInterval? {
        (markers.map(\.time) + sections.map(\.start)).filter { $0 < time - 0.05 }.max()
    }

    /// Mayor canal de entrada usado por las pistas armadas (para saber cuántos canales grabar).
    var highestArmedInputChannel: Int {
        tracks.filter(\.isArmed).map(\.input.highestChannel).max() ?? -1
    }

    // MARK: Secciones

    func section(at time: TimeInterval) -> SongSection? {
        sections.last { $0.start <= time + 1e-6 && time < $0.end - 1e-6 }
    }

    /// Agrega una sección al final de la última (o en `start`), con largo en compases.
    @discardableResult
    mutating func addSection(_ kind: SectionKind, bars: Int = 8, at start: TimeInterval? = nil) -> SongSection {
        let begin = start ?? (sections.map(\.end).max() ?? 0)
        let section = SongSection(kind: kind, start: begin, length: timeline.seconds(atBar: Double(max(1, bars))))
        sections.append(section)
        sections.sort { $0.start < $1.start }
        return section
    }

    mutating func removeSection(_ id: UUID) {
        sections.removeAll { $0.id == id }
    }

    /// Configura el loop para que repita la sección indicada.
    mutating func loopSection(_ id: UUID) {
        guard let s = sections.first(where: { $0.id == id }) else { return }
        loop = LoopRegion(start: s.start, end: s.end, isEnabled: true)
    }

    /// Siguiente sección después de `time` (para saltar en vivo).
    func nextSection(after time: TimeInterval) -> SongSection? {
        sections.first { $0.start > time + 0.05 }
    }

    func previousSection(before time: TimeInterval) -> SongSection? {
        // Si estamos a más de 1 s del inicio de la sección actual, volvemos a su inicio.
        if let current = section(at: time), time - current.start > 1 { return current }
        let reference = section(at: time)?.start ?? time
        return sections.last { $0.start < reference - 0.05 }
    }
}
