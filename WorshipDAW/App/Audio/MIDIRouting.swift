import AVFoundation
import AudioToolbox
import Foundation

/// Bytes de un mensaje MIDI de canal.
struct RawMIDI: Sendable {
    var status: UInt8
    var data1: UInt8
    var data2: UInt8

    var length: Int {
        switch status & 0xF0 {
        case 0xC0, 0xD0: 2
        default: 3
        }
    }

    func send(to block: AUScheduleMIDIEventBlock, at sampleTime: AUEventSampleTime) {
        var bytes = (status, data1, data2)
        let length = self.length
        withUnsafeBytes(of: &bytes) { raw in
            guard let base = raw.baseAddress?.assumingMemoryBound(to: UInt8.self) else { return }
            block(sampleTime, 0, length, base)
        }
    }

    /// Clave para saber si un control está asignado con "Aprender MIDI" (y no debe sonar en el instrumento).
    var mappingKey: Int? {
        let ch = Int(status & 0x0F) + 1
        switch status & 0xF0 {
        case 0xB0: return 0 << 16 | ch << 8 | Int(data1)
        case 0x80, 0x90: return 1 << 16 | ch << 8 | Int(data1)
        case 0xE0: return 2 << 16 | ch << 8
        default: return nil
        }
    }

    static func mappingKey(kind: MIDIMessage.Kind, channel: Int, number: Int) -> Int {
        switch kind {
        case .controlChange: 0 << 16 | channel << 8 | number
        case .noteOn: 1 << 16 | channel << 8 | number
        case .pitchBend: 2 << 16 | channel << 8
        }
    }
}

/// Lleva lo que tocas en el controlador directo al instrumento de la pista armada,
/// desde el hilo de CoreMIDI (sin pasar por la interfaz) para que la latencia sea mínima.
final class LiveMIDIRouter: @unchecked Sendable {
    static let shared = LiveMIDIRouter()

    private let lock = NSLock()
    private var targets: [AUScheduleMIDIEventBlock] = []
    private var mappedKeys: Set<Int> = []
    private var learning = false
    private var recording = false
    private var recorded: [(host: UInt64, message: RawMIDI)] = []

    func setTargets(_ blocks: [AUScheduleMIDIEventBlock]) {
        lock.lock(); targets = blocks; lock.unlock()
    }

    func setMappedKeys(_ keys: Set<Int>) {
        lock.lock(); mappedKeys = keys; lock.unlock()
    }

    func setLearning(_ on: Bool) {
        lock.lock(); learning = on; lock.unlock()
    }

    func beginRecording() {
        lock.lock(); recorded.removeAll(keepingCapacity: true); recording = true; lock.unlock()
    }

    func endRecording() -> [(host: UInt64, message: RawMIDI)] {
        lock.lock()
        recording = false
        let events = recorded
        recorded.removeAll()
        lock.unlock()
        return events
    }

    /// Hilo de CoreMIDI.
    func handle(_ message: RawMIDI, hostTime: UInt64) {
        lock.lock()
        if learning || (message.mappingKey.map { mappedKeys.contains($0) } ?? false) {
            lock.unlock()
            return
        }
        let blocks = targets
        if recording { recorded.append((hostTime, message)) }
        lock.unlock()
        for block in blocks { message.send(to: block, at: AUEventSampleTimeImmediate) }
    }
}

/// Reproduce los ítems MIDI en sincronía con el audio: un temporizador de alta prioridad
/// envía cada nota unos milisegundos antes, con su desplazamiento exacto en muestras.
final class MIDIPlaybackScheduler: @unchecked Sendable {
    struct Event {
        let time: TimeInterval
        let message: RawMIDI
        let target: AUScheduleMIDIEventBlock
    }

    private let queue = DispatchQueue(label: "worship.midi.playback", qos: .userInteractive)
    private var timer: DispatchSourceTimer?
    private var events: [Event] = []
    private var index = 0
    private var startHostSeconds: Double = 0
    private var playFrom: TimeInterval = 0
    private var sampleRate: Double = 48_000
    private var sounding: [(target: AUScheduleMIDIEventBlock, message: RawMIDI)] = []
    private static let lookahead: TimeInterval = 0.025

    func start(events: [Event], startHostTime: UInt64, playFrom: TimeInterval, sampleRate: Double) {
        queue.sync {
            cancelLocked()
            self.events = events.sorted { $0.time < $1.time }
            index = 0
            startHostSeconds = AVAudioTime.seconds(forHostTime: startHostTime)
            self.playFrom = playFrom
            self.sampleRate = sampleRate
            guard !self.events.isEmpty else { return }
            let t = DispatchSource.makeTimerSource(flags: .strict, queue: queue)
            t.schedule(deadline: .now(), repeating: .milliseconds(2), leeway: .microseconds(500))
            t.setEventHandler { [weak self] in self?.tick() }
            timer = t
            t.resume()
        }
    }

    func stop() {
        queue.sync { cancelLocked() }
    }

    private func cancelLocked() {
        timer?.cancel()
        timer = nil
        // Apagar lo que estaba sonando para que no queden notas colgadas.
        for (target, message) in sounding {
            RawMIDI(status: 0x80 | (message.status & 0x0F), data1: message.data1, data2: 0)
                .send(to: target, at: AUEventSampleTimeImmediate)
        }
        sounding.removeAll()
        events.removeAll()
    }

    private func tick() {
        let now = AVAudioTime.seconds(forHostTime: mach_absolute_time())
        let position = playFrom + (now - startHostSeconds)
        while index < events.count, events[index].time < position + Self.lookahead {
            let e = events[index]
            index += 1
            let offset = max(0, e.time - position)
            e.message.send(to: e.target, at: AUEventSampleTimeImmediate + AUEventSampleTime(offset * sampleRate))
            let isOn = e.message.status & 0xF0 == 0x90 && e.message.data2 > 0
            if isOn {
                sounding.append((e.target, e.message))
            } else if let i = sounding.firstIndex(where: {
                $0.message.data1 == e.message.data1 && ($0.message.status & 0x0F) == (e.message.status & 0x0F)
            }) {
                sounding.remove(at: i)
            }
        }
        if index >= events.count {
            timer?.cancel()
            timer = nil
        }
    }
}
