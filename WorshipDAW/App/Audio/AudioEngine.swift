import AVFoundation
import Foundation
import Synchronization
import WorshipDAWCore

/// Niveles de picos escritos desde los hilos de audio y leídos por la UI (~30 fps).
final class MeterBank: @unchecked Sendable {
    static let masterID = UUID(uuidString: "00000000-0000-0000-0000-00000000AAAA")!

    private let lock = NSLock()
    private var levels: [UUID: Float] = [:]

    func push(_ value: Float, for id: UUID) {
        lock.lock()
        levels[id] = max(levels[id] ?? 0, value)
        lock.unlock()
    }

    /// Devuelve el nivel actual y aplica una caída suave (release del medidor).
    func read(_ id: UUID) -> Float {
        lock.lock()
        defer { lock.unlock() }
        let v = levels[id] ?? 0
        levels[id] = v * 0.82
        return v
    }

    func reset() {
        lock.lock()
        levels.removeAll()
        lock.unlock()
    }
}

/// Cadena de una pista: Player → EQ → Inserts (AU) → Fader → Master.
@MainActor
final class TrackStrip {
    let player = AVAudioPlayerNode()
    /// Suma lo grabado (player) y la entrada en vivo (monitoreo) antes del EQ y los plugins.
    let inMix = AVAudioMixerNode()
    var monitorNode: AVAudioSourceNode?
    let monitorState = MonitorState()
    /// Instrumento virtual (pistas MIDI): entra al bus 2 del mezclador de entrada.
    var instrument: (ref: PluginRef, unit: AVAudioUnit)?
    var isLoadingInstrument = false
    let eq = AVAudioUnitEQ(numberOfBands: 3)
    let fader = AVAudioMixerNode()
    var inserts: [(ref: PluginRef, unit: AVAudioUnit)] = []
    var isLoadingInserts = false
    var needsInsertResync = false

    /// Nodos en orden de señal, del player al fader.
    var chain: [AVAudioNode] {
        var nodes: [AVAudioNode] = [player, inMix, eq]
        for insert in inserts { nodes.append(insert.unit) }
        nodes.append(fader)
        return nodes
    }

    var allNodes: [AVAudioNode] {
        var nodes = chain
        if let monitorNode { nodes.append(monitorNode) }
        if let instrument { nodes.append(instrument.unit) }
        return nodes
    }

    init() {
        let low = eq.bands[0]
        low.filterType = .lowShelf
        low.frequency = 100
        low.bypass = false
        let mid = eq.bands[1]
        mid.filterType = .parametric
        mid.frequency = 1_000
        mid.bandwidth = 1.2
        mid.bypass = false
        let high = eq.bands[2]
        high.filterType = .highShelf
        high.frequency = 8_000
        high.bypass = false
    }
}

enum EngineError: LocalizedError {
    case noInput
    case exportFailed(String)

    var errorDescription: String? {
        switch self {
        case .noInput: "No se encontró una entrada de audio (micrófono o interfaz)."
        case .exportFailed(let reason): "No se pudo exportar: \(reason)"
        }
    }
}

@MainActor
final class AudioEngine {
    let engine = AVAudioEngine()
    let format: AVAudioFormat
    let meters = MeterBank()

    private(set) var strips: [UUID: TrackStrip] = [:]
    private let clickPlayer = AVAudioPlayerNode()
    private let clickBus = AVAudioMixerNode()
    private var clickBuffer: (key: Timeline, buffer: AVAudioPCMBuffer)?

    private var projectURL: URL?
    private var latestProject: Project?
    private var startPosition: TimeInterval = 0
    private(set) var isPlaying = false

    private var inputPrepared = false
    private var recordingFile: AVAudioFile?
    private var captureClock: CaptureClock?
    private var playStartHostTime: UInt64 = 0
    private(set) var playStartPosition: TimeInterval = 0

    // Monitoreo de entrada
    private let midiScheduler = MIDIPlaybackScheduler()
    /// Pistas de instrumento que reciben lo que tocas en vivo.
    private var liveMIDITrackIDs: [UUID] = []

    private var inputRing: InputRing?
    private var sinkNode: AVAudioSinkNode?
    private var monitorFormat: AVAudioFormat?
    private var configObserver: NSObjectProtocol?

    var onError: ((String) -> Void)?
    let settings: AudioSettings

    init(sampleRate: Double = 48_000, settings: AudioSettings) {
        self.settings = settings
        format = AudioFormats.project(sampleRate: sampleRate)
        engine.attach(clickPlayer)
        engine.attach(clickBus)
        engine.connect(clickPlayer, to: clickBus, format: format)
        engine.connect(clickBus, to: engine.mainMixerNode, format: format)
        engine.mainMixerNode.installTap(onBus: 0, bufferSize: 1024, format: nil,
                                        block: Self.meterTap(meters, id: MeterBank.masterID))
        configObserver = NotificationCenter.default.addObserver(
            forName: .AVAudioEngineConfigurationChange, object: engine, queue: .main
        ) { [weak self] _ in
            MainActor.assumeIsolated {
                // Cambió el dispositivo de salida (auriculares, interfaz…): reiniciar.
                guard let self else { return }
                let wasPlaying = self.isPlaying
                let position = self.currentPosition
                self.isPlaying = false
                try? self.start()
                if wasPlaying, let project = self.latestProject {
                    self.play(project: project, from: position)
                }
            }
        }
    }

    // MARK: - Ciclo de vida

    func start() throws {
        #if os(iOS)
        let session = AVAudioSession.sharedInstance()
        try session.setCategory(.playAndRecord, mode: .default,
                                options: [.defaultToSpeaker, .allowBluetoothA2DP, .mixWithOthers])
        try session.setActive(true)
        #endif
        if !engine.isRunning {
            settings.apply(to: engine)
            if inputPrepared { settings.applyInput(to: engine) }
            engine.prepare()
            try engine.start()
            reportLatency()
        }
    }

    /// Latencia que reporta el dispositivo (para mostrarla y compensar la grabación).
    private func reportLatency() {
        let output = engine.outputNode.presentationLatency
        let input = inputPrepared ? engine.inputNode.presentationLatency : 0
        settings.reportedRoundTrip = input + output
    }

    /// Segundos a compensar al colocar una grabación.
    var recordingLatency: TimeInterval {
        let reported = settings.useReportedLatency
            ? engine.inputNode.presentationLatency + engine.outputNode.presentationLatency : 0
        return reported + settings.manualLatencyOffsetMs / 1000
    }

    /// Vuelve a abrir el dispositivo con la configuración actual (Preferencias › Audio).
    func reconfigure() {
        let wasPlaying = isPlaying
        let position = currentPosition
        stop()
        engine.stop()
        settings.apply(to: engine)
        if inputPrepared { settings.applyInput(to: engine) }
        engine.connect(engine.mainMixerNode, to: engine.outputNode, format: nil)
        do {
            engine.prepare()
            try engine.start()
            reportLatency()
        } catch {
            onError?("No se pudo iniciar el dispositivo de audio: \(error.localizedDescription)")
            return
        }
        if wasPlaying, let project = latestProject { play(project: project, from: position) }
    }

    func shutdown() {
        stop()
        LiveMIDIRouter.shared.setTargets([])
        engine.stop()
        if let configObserver { NotificationCenter.default.removeObserver(configObserver) }
        configObserver = nil
    }

    // MARK: - Sincronizar el grafo con el proyecto

    func sync(project: Project, projectURL: URL) {
        self.projectURL = projectURL
        latestProject = project
        let ids = Set(project.tracks.map(\.id))

        for (id, strip) in strips where !ids.contains(id) {
            strip.player.stop()
            strip.fader.removeTap(onBus: 0)
            for node in strip.allNodes {
                engine.disconnectNodeOutput(node)
                engine.detach(node)
            }
            strips[id] = nil
        }

        for track in project.tracks {
            let strip: TrackStrip
            if let existing = strips[track.id] {
                strip = existing
            } else {
                strip = TrackStrip()
                engine.attach(strip.player)
                engine.attach(strip.inMix)
                engine.attach(strip.eq)
                engine.attach(strip.fader)
                engine.connect(strip.fader, to: engine.mainMixerNode, format: format)
                rebuildChain(strip)
                strip.fader.installTap(onBus: 0, bufferSize: 1024, format: nil,
                                       block: Self.meterTap(meters, id: track.id))
                strips[track.id] = strip
            }
            apply(track: track, audible: project.isAudible(track), to: strip)
            syncInserts(trackID: track.id)
            syncInstrument(track: track, strip: strip)
        }

        clickBus.outputVolume = project.isClickEnabled ? Decibels.toLinear(project.clickVolumeDB) : 0
        engine.mainMixerNode.outputVolume = Decibels.toLinear(project.masterVolumeDB)
        updateMonitoring()
    }

    private func apply(track: Track, audible: Bool, to strip: TrackStrip) {
        strip.fader.outputVolume = audible ? Decibels.toLinear(track.volumeDB) : 0
        strip.fader.pan = Float(max(-1, min(1, track.pan)))
        strip.eq.bypass = !track.eq.isEnabled || track.eq.isFlat
        strip.eq.bands[0].gain = Float(track.eq.lowGain)
        strip.eq.bands[1].gain = Float(track.eq.midGain)
        strip.eq.bands[2].gain = Float(track.eq.highGain)
        for insert in strip.inserts {
            if let ref = track.inserts.first(where: { $0.id == insert.ref.id }) {
                insert.unit.auAudioUnit.shouldBypassEffect = ref.isBypassed
            }
        }
    }

    private func rebuildChain(_ strip: TrackStrip) {
        let nodes = strip.chain
        for node in nodes.dropLast() {
            engine.disconnectNodeOutput(node)
        }
        for (a, b) in zip(nodes, nodes.dropFirst()) {
            if b === strip.inMix {
                // Bus 0 = lo grabado; bus 1 = monitoreo de entrada.
                engine.connect(a, to: b, fromBus: 0, toBus: 0, format: format)
            } else {
                engine.connect(a, to: b, format: format)
            }
        }
    }

    // MARK: - Monitoreo de entrada (escucharte con EQ y plugins mientras grabas)

    /// Activa o desactiva el monitoreo de cada pista según su modo, si está armada y el transporte.
    func updateMonitoring() {
        guard let project = latestProject else { return }
        var wanted: [(TrackStrip, Track)] = []
        for track in project.tracks {
            guard let strip = strips[track.id] else { continue }
            let on = track.input != .off
                && track.monitor.isMonitoring(armed: track.isArmed, playing: isPlaying, recording: isRecording)
            if on { wanted.append((strip, track)) } else { strip.monitorState.isActive.store(false, ordering: .relaxed) }
        }
        guard !wanted.isEmpty else { return }
        if inputRing == nil {
            do { try setUpMonitoringGraph() } catch {
                onError?(error.localizedDescription)
                return
            }
        }
        guard let ring = inputRing, let monitorFormat else { return }
        for (strip, track) in wanted {
            if strip.monitorNode == nil {
                let node = InputMonitor.makeSourceNode(ring: ring, state: strip.monitorState, format: monitorFormat)
                engine.attach(node)
                engine.connect(node, to: strip.inMix, fromBus: 0, toBus: 1, format: monitorFormat)
                strip.monitorNode = node
            }
            let map = track.input.channelMap ?? [0, 0]
            strip.monitorState.leftChannel.store(map[0], ordering: .relaxed)
            strip.monitorState.rightChannel.store(map[1], ordering: .relaxed)
            strip.monitorState.isActive.store(true, ordering: .relaxed)
        }
    }

    private func setUpMonitoringGraph() throws {
        try prepareInput()
        let inFormat = engine.inputNode.outputFormat(forBus: 0)
        guard inFormat.sampleRate > 0, inFormat.channelCount > 0 else { throw EngineError.noInput }
        let ring = InputRing(channels: Int(inFormat.channelCount))
        let sink = InputMonitor.makeSinkNode(ring: ring)
        let wasRunning = engine.isRunning
        engine.stop()
        engine.attach(sink)
        engine.connect(engine.inputNode, to: sink, format: inFormat)
        inputRing = ring
        sinkNode = sink
        monitorFormat = AVAudioFormat(standardFormatWithSampleRate: inFormat.sampleRate, channels: 2)
        if wasRunning {
            engine.prepare()
            try engine.start()
        }
    }

    /// Carga/descarga Audio Units para que coincidan con `track.inserts`.
    private func syncInserts(trackID: UUID) {
        guard let strip = strips[trackID],
              let track = latestProject?.tracks.first(where: { $0.id == trackID }) else { return }
        if strip.inserts.map(\.ref.id) == track.inserts.map(\.id) { return }
        if strip.isLoadingInserts {
            strip.needsInsertResync = true
            return
        }
        strip.isLoadingInserts = true
        Task { @MainActor in
            var loaded: [(ref: PluginRef, unit: AVAudioUnit)] = []
            for ref in track.inserts {
                if let existing = strip.inserts.first(where: { $0.ref.id == ref.id }) {
                    loaded.append((ref, existing.unit))
                    continue
                }
                do {
                    let unit = try await PluginCatalog.instantiate(ref)
                    loaded.append((ref, unit))
                } catch {
                    onError?(error.localizedDescription)
                }
            }
            // Desconectar la cadena vieja.
            for node in strip.chain.dropLast() {
                engine.disconnectNodeOutput(node)
            }
            for old in strip.inserts where !loaded.contains(where: { $0.ref.id == old.ref.id }) {
                engine.detach(old.unit)
            }
            for new in loaded where new.unit.engine == nil {
                engine.attach(new.unit)
            }
            strip.inserts = loaded
            rebuildChain(strip)
            strip.isLoadingInserts = false
            if strip.needsInsertResync {
                strip.needsInsertResync = false
                syncInserts(trackID: trackID)
            }
        }
    }

    func pluginUnit(trackID: UUID, pluginID: UUID) -> AVAudioUnit? {
        if let instrument = strips[trackID]?.instrument, instrument.ref.id == pluginID { return instrument.unit }
        return strips[trackID]?.inserts.first { $0.ref.id == pluginID }?.unit
    }

    // MARK: - Instrumentos (pistas MIDI)

    private func syncInstrument(track: Track, strip: TrackStrip) {
        let wanted = track.isInstrument ? track.instrument : nil
        if strip.instrument?.ref.id == wanted?.id { return }
        if let old = strip.instrument {
            engine.disconnectNodeOutput(old.unit)
            engine.detach(old.unit)
            strip.instrument = nil
            refreshLiveMIDITargets()
        }
        guard let ref = wanted, !strip.isLoadingInstrument else { return }
        strip.isLoadingInstrument = true
        Task { @MainActor in
            do {
                let unit = try await PluginCatalog.instantiate(ref)
                // ¿Sigue siendo el instrumento elegido después de cargar?
                if let current = latestProject?.tracks.first(where: { $0.id == track.id }),
                   current.instrument?.id == ref.id, strips[track.id] === strip {
                    engine.attach(unit)
                    engine.connect(unit, to: strip.inMix, fromBus: 0, toBus: 2, format: format)
                    strip.instrument = (ref, unit)
                    refreshLiveMIDITargets()
                }
            } catch {
                onError?(error.localizedDescription)
            }
            strip.isLoadingInstrument = false
            guard strips[track.id] === strip else { return }
            // Si mientras cargaba se eligió otro instrumento, cargar ese (sin reintentar uno que falló).
            if let current = latestProject?.tracks.first(where: { $0.id == track.id }),
               current.instrument?.id != ref.id {
                syncInstrument(track: current, strip: strip)
            }
        }
    }

    /// Qué pistas de instrumento suenan con tu teclado: las armadas o, si no hay, la seleccionada.
    func setLiveMIDITracks(_ ids: [UUID]) {
        liveMIDITrackIDs = ids
        refreshLiveMIDITargets()
    }

    private func refreshLiveMIDITargets() {
        let blocks = liveMIDITrackIDs.compactMap { strips[$0]?.instrument?.unit.auAudioUnit.scheduleMIDIEventBlock }
        LiveMIDIRouter.shared.setTargets(blocks)
    }

    private func midiEvents(project: Project, from position: TimeInterval) -> [MIDIPlaybackScheduler.Event] {
        var events: [MIDIPlaybackScheduler.Event] = []
        for track in project.tracks where track.isInstrument {
            guard let block = strips[track.id]?.instrument?.unit.auAudioUnit.scheduleMIDIEventBlock else { continue }
            for clip in track.midiClips where clip.end > position {
                for e in MIDIRecording.playbackEvents(for: clip, from: position) {
                    events.append(.init(time: e.time, message: RawMIDI(status: e.status, data1: e.data1, data2: e.data2),
                                        target: block))
                }
            }
        }
        return events
    }

    /// Latencia de salida: se descuenta al grabar MIDI (tocas sobre lo que escuchas).
    var outputLatency: TimeInterval { engine.outputNode.presentationLatency }

    /// Convierte una hora del reloj (host time) en posición de la línea de tiempo de la última reproducción.
    func timelinePosition(forHostTime host: UInt64) -> TimeInterval {
        playStartPosition + AVAudioTime.seconds(forHostTime: host) - AVAudioTime.seconds(forHostTime: playStartHostTime)
    }

    /// Captura el estado actual de todos los plugins cargados para guardarlo en el proyecto.
    func pluginStates() -> [UUID: Data] {
        var states: [UUID: Data] = [:]
        for strip in strips.values {
            for insert in strip.inserts {
                if let data = PluginCatalog.serializedState(of: insert.unit) {
                    states[insert.ref.id] = data
                }
            }
        }
        return states
    }

    // MARK: - Transporte

    var currentPosition: TimeInterval {
        guard isPlaying,
              let nodeTime = clickPlayer.lastRenderTime,
              let playerTime = clickPlayer.playerTime(forNodeTime: nodeTime) else { return startPosition }
        return startPosition + max(0, Double(playerTime.sampleTime) / playerTime.sampleRate)
    }

    func play(project: Project, from position: TimeInterval) {
        latestProject = project
        stopPlayers()
        do { try start() } catch {
            onError?(error.localizedDescription)
            return
        }
        schedule(project: project, from: position)
        let startHost = mach_absolute_time() + AVAudioTime.hostTime(forSeconds: 0.06)
        let startTime = AVAudioTime(hostTime: startHost)
        for strip in strips.values { strip.player.play(at: startTime) }
        clickPlayer.play(at: startTime)
        startPosition = position
        playStartPosition = position
        playStartHostTime = startHost
        isPlaying = true
        midiScheduler.start(events: midiEvents(project: project, from: position), startHostTime: startHost,
                            playFrom: position, sampleRate: format.sampleRate)
        refreshLiveMIDITargets()
        updateMonitoring()
    }

    func stop() {
        if isPlaying { startPosition = currentPosition }
        midiScheduler.stop()
        stopPlayers()
        isPlaying = false
        updateMonitoring()
    }

    private func stopPlayers() {
        for strip in strips.values { strip.player.stop() }
        clickPlayer.stop()
    }

    private func schedule(project: Project, from position: TimeInterval, includeClick: Bool = true) {
        guard let projectURL else { return }
        let sr = format.sampleRate
        let audioDir = ProjectStore.audioDirectory(of: projectURL)
        for track in project.tracks {
            guard let strip = strips[track.id] else { continue }
            for clip in track.clips where clip.end > position {
                // Un AVAudioFile por segmento: los players leen en paralelo.
                guard let file = try? AVAudioFile(forReading: audioDir.appendingPathComponent(clip.fileName)) else {
                    continue
                }
                let into = max(0, position - clip.start)
                let delay = max(0, clip.start - position)
                let startFrame = AVAudioFramePosition(((clip.offset + into) * sr).rounded())
                let wanted = AVAudioFramePosition(((clip.duration - into) * sr).rounded())
                let frames = min(wanted, file.length - startFrame)
                guard frames > 0 else { continue }
                strip.player.scheduleSegment(file, startingFrame: startFrame,
                                             frameCount: AVAudioFrameCount(frames),
                                             at: AVAudioTime(sampleTime: AVAudioFramePosition((delay * sr).rounded()), atRate: sr),
                                             completionHandler: nil)
            }
        }
        guard includeClick else { return }
        let timeline = project.timeline
        if clickBuffer?.key != timeline {
            if let buffer = Metronome.barBuffer(timeline: timeline, format: format) {
                clickBuffer = (timeline, buffer)
            }
        }
        guard let bar = clickBuffer?.buffer else { return }
        let barFrames = Int(bar.frameLength)
        let n = max(1, barFrames)
        let offset = ((Int((position * sr).rounded()) % n) + n) % n
        var at: AVAudioFramePosition = 0
        if offset > 0, let tail = Metronome.tail(of: bar, from: offset) {
            clickPlayer.scheduleBuffer(tail, at: AVAudioTime(sampleTime: 0, atRate: sr), options: [],
                                       completionHandler: nil)
            at = AVAudioFramePosition(tail.frameLength)
        }
        clickPlayer.scheduleBuffer(bar, at: AVAudioTime(sampleTime: at, atRate: sr), options: .loops,
                                   completionHandler: nil)
    }

    // MARK: - Grabación

    /// Prepara la entrada (hay que hacerlo con el motor detenido la primera vez).
    func prepareInput() throws {
        guard !inputPrepared else { return }
        stop()
        engine.stop()
        settings.applyInput(to: engine)
        let input = engine.inputNode
        let fmt = input.outputFormat(forBus: 0)
        guard fmt.sampleRate > 0, fmt.channelCount > 0 else { throw EngineError.noInput }
        inputPrepared = true
        try start()
    }

    func startRecording(to url: URL) throws {
        try prepareInput()
        let input = engine.inputNode
        let inFormat = input.outputFormat(forBus: 0)
        guard inFormat.sampleRate > 0, inFormat.channelCount > 0 else { throw EngineError.noInput }
        let settings: [String: Any] = [
            AVFormatIDKey: kAudioFormatLinearPCM,
            AVSampleRateKey: inFormat.sampleRate,
            AVNumberOfChannelsKey: inFormat.channelCount,
            AVLinearPCMBitDepthKey: 32,
            AVLinearPCMIsFloatKey: true,
            AVLinearPCMIsNonInterleaved: !inFormat.isInterleaved,
        ]
        let file = try AVAudioFile(forWriting: url, settings: settings, commonFormat: .pcmFormatFloat32,
                                   interleaved: inFormat.isInterleaved)
        let clock = CaptureClock()
        recordingFile = file
        captureClock = clock
        input.removeTap(onBus: 0)
        input.installTap(onBus: 0, bufferSize: 1024, format: inFormat,
                         block: Self.recordTap(file, clock: clock, meters: meters))
        updateMonitoring()
    }

    struct RecordingResult {
        let wroteAudio: Bool
        /// Segundos entre el inicio de la reproducción y la primera muestra capturada.
        let captureDelay: TimeInterval
        let latency: TimeInterval
        let playFrom: TimeInterval
    }

    /// Detiene la grabación y devuelve los datos para alinearla en la línea de tiempo.
    func stopRecording() -> RecordingResult {
        guard let file = recordingFile else {
            return RecordingResult(wroteAudio: false, captureDelay: 0, latency: 0, playFrom: playStartPosition)
        }
        engine.inputNode.removeTap(onBus: 0)
        let wrote = file.length > 0
        var delay: TimeInterval = 0
        if let first = captureClock?.firstHostTime.load(ordering: .acquiring), first != 0, playStartHostTime != 0 {
            delay = AVAudioTime.seconds(forHostTime: first) - AVAudioTime.seconds(forHostTime: playStartHostTime)
        }
        let result = RecordingResult(wroteAudio: wrote, captureDelay: delay, latency: recordingLatency,
                                     playFrom: playStartPosition)
        recordingFile = nil
        captureClock = nil
        updateMonitoring()
        return result
    }

    var isRecording: Bool { recordingFile != nil }

    // MARK: - Exportar mezcla (render offline)

    func exportMix(project: Project, to url: URL, tail: TimeInterval = 2,
                   progress: @escaping (Double) -> Void) async throws {
        let length = project.contentEnd + tail
        guard length > tail else { throw EngineError.exportFailed("el proyecto está vacío") }
        stop()
        engine.stop()
        let clickVolume = clickBus.outputVolume
        clickBus.outputVolume = 0
        // El render offline no tiene entrada: desconectar el monitoreo mientras tanto.
        for strip in strips.values { strip.monitorState.isActive.store(false, ordering: .relaxed) }
        let inputFormat = sinkNode != nil ? engine.inputNode.outputFormat(forBus: 0) : nil
        if sinkNode != nil { engine.disconnectNodeOutput(engine.inputNode) }
        defer {
            engine.disableManualRenderingMode()
            if let sinkNode, let inputFormat { engine.connect(engine.inputNode, to: sinkNode, format: inputFormat) }
            clickBus.outputVolume = clickVolume
            try? start()
        }
        do {
            try engine.enableManualRenderingMode(.offline, format: format, maximumFrameCount: 4096)
            try engine.start()
        } catch {
            throw EngineError.exportFailed(error.localizedDescription)
        }
        schedule(project: project, from: 0, includeClick: false)
        for strip in strips.values { strip.player.play() }
        let midi = midiEvents(project: project, from: 0).sorted { $0.time < $1.time }
        var midiIndex = 0

        let settings: [String: Any] = [
            AVFormatIDKey: kAudioFormatLinearPCM,
            AVSampleRateKey: format.sampleRate,
            AVNumberOfChannelsKey: 2,
            AVLinearPCMBitDepthKey: 24,
            AVLinearPCMIsFloatKey: false,
            AVLinearPCMIsBigEndianKey: false,
            AVLinearPCMIsNonInterleaved: false,
        ]
        let output = try AVAudioFile(forWriting: url, settings: settings,
                                     commonFormat: .pcmFormatFloat32, interleaved: false)
        guard let buffer = AVAudioPCMBuffer(pcmFormat: engine.manualRenderingFormat,
                                            frameCapacity: engine.manualRenderingMaximumFrameCount) else {
            throw EngineError.exportFailed("sin memoria")
        }
        let totalFrames = AVAudioFramePosition(length * format.sampleRate)
        var lastReport = 0.0
        while engine.manualRenderingSampleTime < totalFrames {
            let remaining = totalFrames - engine.manualRenderingSampleTime
            let frames = AVAudioFrameCount(min(AVAudioFramePosition(buffer.frameCapacity), remaining))
            // Notas MIDI que caen en este bloque, con su desplazamiento en muestras.
            let blockStart = Double(engine.manualRenderingSampleTime) / format.sampleRate
            let blockEnd = blockStart + Double(frames) / format.sampleRate
            while midiIndex < midi.count, midi[midiIndex].time < blockEnd {
                let e = midi[midiIndex]
                let offset = AUEventSampleTime(max(0, e.time - blockStart) * format.sampleRate)
                e.message.send(to: e.target, at: AUEventSampleTimeImmediate + offset)
                midiIndex += 1
            }
            let status = try engine.renderOffline(frames, to: buffer)
            switch status {
            case .success:
                try output.write(from: buffer)
            case .insufficientDataFromInputNode, .cannotDoInCurrentContext:
                continue
            case .error:
                throw EngineError.exportFailed("error de render")
            @unknown default:
                throw EngineError.exportFailed("estado desconocido")
            }
            let p = Double(engine.manualRenderingSampleTime) / Double(totalFrames)
            if p - lastReport > 0.02 {
                lastReport = p
                progress(p)
                await Task.yield()
            }
        }
        for strip in strips.values { strip.player.stop() }
        engine.stop()
        progress(1)
    }

    // MARK: - Bloques de tap (no aislados al MainActor: corren en hilos de audio)

    nonisolated private static func meterTap(_ meters: MeterBank, id: UUID) -> AVAudioNodeTapBlock {
        { buffer, _ in
            meters.push(peak(of: buffer), for: id)
        }
    }

    nonisolated private static func recordTap(_ file: AVAudioFile, clock: CaptureClock, meters: MeterBank) -> AVAudioNodeTapBlock {
        { buffer, when in
            if when.isHostTimeValid, clock.firstHostTime.load(ordering: .relaxed) == 0 {
                clock.firstHostTime.store(when.hostTime, ordering: .releasing)
            }
            try? file.write(from: buffer)
            meters.push(peak(of: buffer), for: MeterBank.inputID)
        }
    }

    nonisolated private static func peak(of buffer: AVAudioPCMBuffer) -> Float {
        guard let data = buffer.floatChannelData else { return 0 }
        let frames = Int(buffer.frameLength)
        let channels = buffer.format.isInterleaved ? 1 : Int(buffer.format.channelCount)
        let stride = buffer.format.isInterleaved ? Int(buffer.format.channelCount) * frames : frames
        var m: Float = 0
        for c in 0..<channels {
            let ch = data[c]
            for i in 0..<stride {
                let v = abs(ch[i])
                if v > m { m = v }
            }
        }
        return m
    }
}

extension MeterBank {
    static let inputID = UUID(uuidString: "00000000-0000-0000-0000-00000000BBBB")!
}

/// Hora (host time) de la primera muestra grabada; la escribe el hilo de audio.
final class CaptureClock: @unchecked Sendable {
    let firstHostTime = Atomic<UInt64>(0)
}
