import AVFoundation
import Foundation
import Observation
import WorshipDAWCore

/// Estado de un proyecto abierto: modelo, transporte, selección y edición.
@Observable
@MainActor
final class Session {
    /// Modificar solo a través de `edit`, `undo` o `redo` (que sincronizan el motor de audio).
    private(set) var project: Project
    let url: URL
    @ObservationIgnored let audio: AudioEngine
    @ObservationIgnored let waveforms = WaveformCache()

    // Transporte
    var isPlaying = false
    var isRecording = false
    /// Cursor de reproducción (se mueve mientras suena).
    var playhead: TimeInterval = 0
    /// Cursor de edición (REAPER): donde arranca la reproducción y adonde vuelve al detener.
    var editCursor: TimeInterval = 0

    // Selección y vista
    var selectedTrackID: UUID?
    var selectedClipID: UUID?
    var pixelsPerSecond: Double = 48
    var snapEnabled = true
    var followPlayhead = true
    var waveformRevision = 0

    // Mensajes y tareas
    var errorMessage: String?
    var statusMessage: String?
    var exportProgress: Double?
    var lastExportURL: URL?
    var isImporting = false

    @ObservationIgnored private var undoStack: [Project] = []
    @ObservationIgnored private var redoStack: [Project] = []
    @ObservationIgnored private var ticker: Timer?
    @ObservationIgnored private var saveTask: Task<Void, Never>?
    @ObservationIgnored private var recording: (url: URL, start: TimeInterval, trackIDs: [UUID])?

    var canUndo: Bool { !undoStack.isEmpty }
    var canRedo: Bool { !redoStack.isEmpty }

    init(project: Project, url: URL, audioSettings: AudioSettings) {
        self.project = project
        self.url = url
        self.audio = AudioEngine(sampleRate: project.sampleRate, settings: audioSettings)
        selectedTrackID = project.tracks.first?.id
        audio.onError = { [weak self] message in self?.errorMessage = message }
        waveforms.onLoaded = { [weak self] in self?.waveformRevision += 1 }
        audio.sync(project: project, projectURL: url)
        do { try audio.start() } catch { errorMessage = error.localizedDescription }
    }

    func close() {
        stop()
        saveNow()
        audio.shutdown()
    }

    // MARK: - Edición con deshacer

    /// Aplica un cambio al proyecto. `undoable = false` para gestos continuos (faders, perillas).
    func edit(undoable: Bool = true, _ change: (inout Project) -> Void) {
        var copy = project
        change(&copy)
        guard copy != project else { return }
        if undoable {
            undoStack.append(project)
            if undoStack.count > 200 { undoStack.removeFirst() }
            redoStack.removeAll()
        }
        copy.modifiedAt = Date()
        setProject(copy)
    }

    private func setProject(_ newValue: Project) {
        project = newValue
        audio.sync(project: newValue, projectURL: url)
        scheduleSave()
    }

    /// Registra un punto de deshacer antes de un gesto continuo.
    func beginGesture() {
        undoStack.append(project)
        redoStack.removeAll()
    }

    func undo() {
        guard let previous = undoStack.popLast() else { return }
        redoStack.append(project)
        setProject(previous)
    }

    func redo() {
        guard let next = redoStack.popLast() else { return }
        undoStack.append(project)
        setProject(next)
    }

    // MARK: - Guardado

    func scheduleSave() {
        saveTask?.cancel()
        saveTask = Task { [weak self] in
            try? await Task.sleep(for: .milliseconds(800))
            guard !Task.isCancelled else { return }
            self?.saveNow()
        }
    }

    func saveNow() {
        var snapshot = project
        let states = audio.pluginStates()
        for t in snapshot.tracks.indices {
            for i in snapshot.tracks[t].inserts.indices {
                if let state = states[snapshot.tracks[t].inserts[i].id] {
                    snapshot.tracks[t].inserts[i].state = state
                }
            }
        }
        do {
            try ProjectStore.save(snapshot, to: url)
        } catch {
            errorMessage = "No se pudo guardar: \(error.localizedDescription)"
        }
    }

    // MARK: - Transporte

    func togglePlay() {
        isPlaying ? stop() : play()
    }

    func play() {
        playhead = editCursor
        audio.play(project: project, from: playhead)
        isPlaying = audio.isPlaying
        if isPlaying { startTicker() }
    }

    func stop() {
        if isRecording { finishRecording() }
        audio.stop()
        isPlaying = false
        stopTicker()
        // Como REAPER: al detener, el cursor vuelve al cursor de edición.
        playhead = editCursor
    }

    /// Mueve el cursor de edición (clic en la regla o en una pista). Si está sonando, salta ahí.
    func seek(to time: TimeInterval) {
        editCursor = max(0, time)
        playhead = editCursor
        if isPlaying && !isRecording {
            audio.play(project: project, from: playhead)
        }
    }

    /// Inicio (tecla Inicio).
    func returnToStart() {
        seek(to: 0)
    }

    /// Fin del proyecto (tecla Fin).
    func goToEnd() {
        seek(to: project.contentEnd)
    }

    private func startTicker() {
        stopTicker()
        let timer = Timer(timeInterval: 1.0 / 30.0, repeats: true) { [weak self] _ in
            MainActor.assumeIsolated { self?.tick() }
        }
        RunLoop.main.add(timer, forMode: .common)
        ticker = timer
    }

    private func stopTicker() {
        ticker?.invalidate()
        ticker = nil
    }

    private func tick() {
        guard isPlaying else { return }
        let position = audio.currentPosition
        let loop = project.loop
        if loop.isEnabled, !isRecording, loop.length > 0.1, position >= loop.end, editCursor < loop.end {
            playhead = loop.start
            audio.play(project: project, from: loop.start)
            return
        }
        playhead = position
    }

    // MARK: - Grabación

    func toggleRecord() {
        isRecording ? stop() : startRecording()
    }

    func startRecording() {
        Task {
            guard await AVAudioApplication.requestRecordPermission() else {
                errorMessage = "Activa el permiso de micrófono para grabar."
                return
            }
            if isPlaying { stop() }
            var targets = project.tracks.filter { $0.isArmed && $0.input != .off }.map(\.id)
            if targets.isEmpty {
                if let selected = selectedTrackID, project.trackIndex(selected) != nil {
                    targets = [selected]
                } else {
                    var newID: UUID?
                    edit { newID = $0.addTrack(name: "Grabación").id }
                    if let newID { targets = [newID] }
                }
                edit { p in
                    for id in targets { if let i = p.trackIndex(id) { p.tracks[i].isArmed = true } }
                }
            }
            let tempURL = FileManager.default.temporaryDirectory
                .appendingPathComponent("rec-\(UUID().uuidString).caf")
            do {
                try audio.startRecording(to: tempURL)
            } catch {
                errorMessage = error.localizedDescription
                return
            }
            recording = (tempURL, editCursor, targets)
            isRecording = true
            play()
        }
    }

    private func finishRecording() {
        guard let info = recording else { return }
        recording = nil
        let wrote = audio.stopRecording()
        isRecording = false
        guard wrote else { return }
        let takeNumber = project.tracks.flatMap(\.clips).filter { $0.name.contains("Toma") }.count + 1
        // Cada pista toma sus canales de la grabación multicanal (Entrada 1, Entradas 3/4…).
        var byInput: [[Int]: [UUID]] = [:]
        for id in info.trackIDs {
            guard let i = project.trackIndex(id), let map = project.tracks[i].input.channelMap else { continue }
            byInput[map, default: []].append(id)
        }
        Task {
            for (map, ids) in byInput {
                do {
                    let (fileName, duration) = try await ingest(source: info.url, channelMap: map)
                    edit { p in
                        for id in ids {
                            guard let i = p.trackIndex(id) else { continue }
                            p.addClip(Clip(name: "\(p.tracks[i].name) · Toma \(takeNumber)", fileName: fileName,
                                           start: info.start, duration: duration), toTrack: id)
                        }
                    }
                } catch {
                    errorMessage = error.localizedDescription
                }
            }
            try? FileManager.default.removeItem(at: info.url)
        }
    }

    // MARK: - Importar audio

    /// Convierte un archivo al formato del proyecto, calcula su forma de onda y devuelve (archivo, duración).
    private func ingest(source: URL, channelMap: [Int]? = nil) async throws -> (String, TimeInterval) {
        let fileName = UUID().uuidString + ".caf"
        let dest = ProjectStore.audioDirectory(of: url).appendingPathComponent(fileName)
        let peaksURL = ProjectStore.peaksDirectory(of: url).appendingPathComponent(fileName + ".peaks")
        try FileManager.default.createDirectory(at: ProjectStore.audioDirectory(of: url), withIntermediateDirectories: true)
        try FileManager.default.createDirectory(at: ProjectStore.peaksDirectory(of: url), withIntermediateDirectories: true)
        let sampleRate = project.sampleRate
        let (duration, peaks) = try await Task.detached(priority: .userInitiated) {
            let duration = try AudioConverterService.convert(source: source, destination: dest,
                                                             format: AudioFormats.project(sampleRate: sampleRate),
                                                             channelMap: channelMap)
            let peaks = try? WaveformPeaks.compute(from: dest)
            try? peaks?.write(to: peaksURL)
            return (duration, peaks)
        }.value
        if let peaks { waveforms.store(peaks, for: fileName) }
        return (fileName, duration)
    }

    /// Importa archivos. Un archivo va a la pista indicada; varios (stems) crean una pista por archivo.
    func importAudio(urls: [URL], to trackID: UUID? = nil, at start: TimeInterval? = nil) {
        guard !urls.isEmpty else { return }
        isImporting = true
        Task {
            defer { isImporting = false }
            let position = start ?? (urls.count > 1 ? 0 : playhead)
            for source in urls {
                let accessing = source.startAccessingSecurityScopedResource()
                defer { if accessing { source.stopAccessingSecurityScopedResource() } }
                do {
                    statusMessage = "Importando \(source.lastPathComponent)…"
                    let (fileName, duration) = try await ingest(source: source)
                    let name = source.deletingPathExtension().lastPathComponent
                    edit { p in
                        let target: UUID
                        if urls.count == 1, let trackID, p.trackIndex(trackID) != nil {
                            target = trackID
                        } else if urls.count == 1, let selected = self.selectedTrackID, p.trackIndex(selected) != nil {
                            target = selected
                        } else {
                            target = p.addTrack(name: name).id
                        }
                        p.addClip(Clip(name: name, fileName: fileName, start: position, duration: duration),
                                  toTrack: target)
                    }
                } catch {
                    errorMessage = error.localizedDescription
                }
            }
            statusMessage = nil
        }
    }

    // MARK: - Exportar

    func exportMix() {
        guard exportProgress == nil else { return }
        stop()
        let exportsDir = url.appendingPathComponent("Exports", isDirectory: true)
        try? FileManager.default.createDirectory(at: exportsDir, withIntermediateDirectories: true)
        let formatter = DateFormatter()
        formatter.dateFormat = "yyyy-MM-dd HHmm"
        let dest = exportsDir.appendingPathComponent("\(project.name) – Mezcla \(formatter.string(from: Date())).wav")
        exportProgress = 0
        Task {
            do {
                try await audio.exportMix(project: project, to: dest) { [weak self] p in
                    self?.exportProgress = p
                }
                lastExportURL = dest
            } catch {
                errorMessage = error.localizedDescription
            }
            exportProgress = nil
        }
    }

    // MARK: - Pistas

    func addTrack(role: TrackRole = .audio) {
        edit { p in
            let track = p.addTrack(name: role == .guide ? "Guía" : "Audio \(p.tracks.count + 1)", role: role)
            self.selectedTrackID = track.id
        }
    }

    func removeTrack(_ id: UUID) {
        edit { $0.removeTrack(id) }
        if selectedTrackID == id { selectedTrackID = project.tracks.first?.id }
    }

    func updateTrack(_ id: UUID, undoable: Bool = true, _ change: (inout Track) -> Void) {
        edit(undoable: undoable) { p in
            guard let i = p.trackIndex(id) else { return }
            change(&p.tracks[i])
        }
    }

    // MARK: - Clips

    func splitAtPlayhead() {
        edit { p in
            if let id = selectedClipID, let loc = p.clipLocation(id),
               p.tracks[loc.track].clips[loc.clip].start < playhead,
               p.tracks[loc.track].clips[loc.clip].end > playhead {
                p.splitClip(id, at: playhead)
            } else {
                p.splitAll(at: playhead)
            }
        }
    }

    func deleteSelection() {
        if let id = selectedClipID {
            edit { $0.removeClip(id) }
            selectedClipID = nil
        }
    }

    func duplicateSelection() {
        guard let id = selectedClipID else { return }
        var newID: UUID?
        edit { newID = $0.duplicateClip(id) }
        if let newID { selectedClipID = newID }
    }

    // MARK: - Secciones

    func addSection(_ kind: SectionKind, bars: Int = 8) {
        edit { p in
            let count = p.sections.filter { $0.kind == kind }.count
            let section = p.addSection(kind, bars: bars)
            if count > 0, let i = p.sections.firstIndex(where: { $0.id == section.id }) {
                p.sections[i].name = "\(kind.displayName) \(count + 1)"
            }
        }
    }

    func jumpToNextSection() {
        if let s = project.nextSection(after: playhead) { seek(to: s.start) }
    }

    func jumpToPreviousSection() {
        if let s = project.previousSection(before: playhead) { seek(to: s.start) } else { seek(to: 0) }
    }

    // MARK: - Marcadores (M, 1–9)

    func insertMarker() {
        let time = isPlaying ? playhead : editCursor
        edit { $0.addMarker(at: time) }
    }

    func goToMarker(_ number: Int) {
        if let m = project.marker(number: number) { seek(to: m.time) }
    }

    func goToNextMarker() {
        if let t = project.nextStop(after: playhead) { seek(to: t) }
    }

    func goToPreviousMarker() {
        seek(to: project.previousStop(before: playhead) ?? 0)
    }

    // MARK: - Selección de tiempo (arrastrar en la regla; el loop la sigue)

    func setTimeSelection(_ a: TimeInterval, _ b: TimeInterval) {
        let start = max(0, min(a, b)), end = max(a, b)
        guard end - start > 0.01 else { return }
        edit(undoable: false) { p in
            p.loop.start = start
            p.loop.end = end
        }
    }

    func clearTimeSelection() {
        edit { p in
            p.loop = LoopRegion(start: 0, end: 0, isEnabled: false)
        }
    }

    var hasTimeSelection: Bool { project.loop.length > 0.01 }

    // MARK: - MIDI

    func handleMIDI(_ target: MIDITarget, value: Double) {
        func track(_ i: Int, _ change: (inout Track) -> Void) {
            guard project.tracks.indices.contains(i) else { return }
            updateTrack(project.tracks[i].id, undoable: false, change)
        }
        switch target {
        case .trackVolume(let i): track(i) { $0.volumeDB = (Fader.db(for: value) * 10).rounded() / 10 }
        case .trackPan(let i): track(i) { $0.pan = ((value * 2 - 1) * 100).rounded() / 100 }
        case .trackMute(let i): track(i) { $0.isMuted.toggle() }
        case .trackSolo(let i): track(i) { $0.isSolo.toggle() }
        case .trackArm(let i): track(i) { $0.isArmed.toggle() }
        case .masterVolume: edit(undoable: false) { $0.masterVolumeDB = (Fader.db(for: value) * 10).rounded() / 10 }
        case .playStop: togglePlay()
        case .record: toggleRecord()
        case .repeatToggle: toggleLoop()
        case .nextMarker: goToNextMarker()
        case .previousMarker: goToPreviousMarker()
        case .goToStart: returnToStart()
        }
    }

    func loopSection(_ id: UUID) {
        edit { $0.loopSection(id) }
        if let s = project.sections.first(where: { $0.id == id }) { seek(to: s.start) }
    }

    /// Repetir (tecla R en REAPER). Sin selección de tiempo, usa la sección actual.
    func toggleLoop() {
        edit { p in
            if !p.loop.isEnabled, p.loop.length < 0.1, let s = p.section(at: playhead) {
                p.loop = LoopRegion(start: s.start, end: s.end, isEnabled: true)
            } else {
                p.loop.isEnabled.toggle()
            }
        }
    }

    // MARK: - Vista

    func zoom(by factor: Double) {
        pixelsPerSecond = min(600, max(6, pixelsPerSecond * factor))
    }

    func snap(_ time: TimeInterval) -> TimeInterval {
        snapEnabled ? project.timeline.snap(time) : max(0, time)
    }

    /// Duración visible de la línea de tiempo (contenido + margen).
    var timelineLength: TimeInterval {
        max(project.contentEnd + 30, 180, playhead + 20)
    }
}
