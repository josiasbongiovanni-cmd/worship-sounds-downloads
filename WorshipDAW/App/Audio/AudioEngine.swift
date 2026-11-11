import AVFoundation
import Foundation
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
    let eq = AVAudioUnitEQ(numberOfBands: 3)
    let fader = AVAudioMixerNode()
    var inserts: [(ref: PluginRef, unit: AVAudioUnit)] = []
    var isLoadingInserts = false
    var needsInsertResync = false

    /// Nodos en orden de señal, del player al fader.
    var chain: [AVAudioNode] {
        var nodes: [AVAudioNode] = [player, eq]
        for insert in inserts { nodes.append(insert.unit) }
        nodes.append(fader)
        return nodes
    }

    var allNodes: [AVAudioNode] { chain }

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
    private var configObserver: NSObjectProtocol?

    var onError: ((String) -> Void)?

    init(sampleRate: Double = 48_000) {
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
        try? session.setPreferredSampleRate(format.sampleRate)
        try? session.setPreferredIOBufferDuration(0.005)
        try session.setActive(true)
        #endif
        if !engine.isRunning {
            engine.prepare()
            try engine.start()
        }
    }

    func shutdown() {
        stop()
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
        }

        clickBus.outputVolume = project.isClickEnabled ? Decibels.toLinear(project.clickVolumeDB) : 0
        engine.mainMixerNode.outputVolume = Decibels.toLinear(project.masterVolumeDB)
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
            engine.connect(a, to: b, format: format)
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
        strips[trackID]?.inserts.first { $0.ref.id == pluginID }?.unit
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
        let startTime = AVAudioTime(hostTime: mach_absolute_time() + AVAudioTime.hostTime(forSeconds: 0.06))
        for strip in strips.values { strip.player.play(at: startTime) }
        clickPlayer.play(at: startTime)
        startPosition = position
        isPlaying = true
    }

    func stop() {
        if isPlaying { startPosition = currentPosition }
        stopPlayers()
        isPlaying = false
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
        let offset = Int((position * sr).rounded()) % max(1, barFrames)
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
        recordingFile = file
        input.removeTap(onBus: 0)
        input.installTap(onBus: 0, bufferSize: 4096, format: inFormat, block: Self.recordTap(file, meters: meters))
    }

    /// Detiene la grabación y devuelve si se escribió audio.
    func stopRecording() -> Bool {
        guard let file = recordingFile else { return false }
        engine.inputNode.removeTap(onBus: 0)
        let wrote = file.length > 0
        recordingFile = nil
        return wrote
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
        defer {
            engine.disableManualRenderingMode()
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

    nonisolated private static func recordTap(_ file: AVAudioFile, meters: MeterBank) -> AVAudioNodeTapBlock {
        { buffer, _ in
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
