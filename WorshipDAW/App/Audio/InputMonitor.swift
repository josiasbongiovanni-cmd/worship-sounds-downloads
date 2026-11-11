import AVFoundation
import Synchronization

/// Buffer circular de la entrada de audio, escrito por el AVAudioSinkNode (hilo de audio)
/// y leído por los AVAudioSourceNode de monitoreo de cada pista. Sin locks ni memoria dinámica
/// en el hilo de audio.
final class InputRing: @unchecked Sendable {
    let channels: Int
    let capacity: Int
    private let mask: Int
    private let storage: UnsafeMutablePointer<Float>
    /// Total de frames escritos desde que se creó.
    let written = Atomic<Int>(0)

    init(channels: Int, capacity: Int = 16_384) {
        self.channels = max(1, channels)
        var cap = 1
        while cap < capacity { cap <<= 1 }
        self.capacity = cap
        mask = cap - 1
        storage = .allocate(capacity: self.channels * cap)
        storage.initialize(repeating: 0, count: self.channels * cap)
    }

    deinit {
        storage.deallocate()
    }

    /// Hilo de audio: copia un bloque de la entrada.
    func write(_ list: UnsafeMutableAudioBufferListPointer, frames: Int) {
        let w = written.load(ordering: .relaxed)
        if list.count == 1, list[0].mNumberChannels > 1, let data = list[0].mData {
            // Intercalado
            let src = data.assumingMemoryBound(to: Float.self)
            let n = Int(list[0].mNumberChannels)
            for c in 0..<min(channels, n) {
                let base = storage + c * capacity
                for i in 0..<frames { base[(w + i) & mask] = src[i * n + c] }
            }
        } else {
            for c in 0..<min(channels, list.count) {
                guard let data = list[c].mData else { continue }
                let src = data.assumingMemoryBound(to: Float.self)
                let base = storage + c * capacity
                for i in 0..<frames { base[(w + i) & mask] = src[i] }
            }
        }
        written.store(w + frames, ordering: .releasing)
    }

    /// Hilo de audio: lee `frames` del canal desde la posición absoluta `position`.
    func read(channel: Int, from position: Int, frames: Int, into dst: UnsafeMutablePointer<Float>) {
        guard channel >= 0, channel < channels else {
            dst.update(repeating: 0, count: frames)
            return
        }
        let base = storage + channel * capacity
        for i in 0..<frames { dst[i] = base[(position + i) & mask] }
    }
}

/// Estado del monitoreo de una pista. Lo escribe el hilo principal y lo lee el hilo de audio.
final class MonitorState: @unchecked Sendable {
    let isActive = Atomic<Bool>(false)
    let leftChannel = Atomic<Int>(0)
    let rightChannel = Atomic<Int>(0)
    /// Solo lo usa el hilo de audio.
    var readPosition = -1
}

enum InputMonitor {
    /// Nodo que entrega la entrada (canales de la pista) a la cadena de la pista: EQ, plugins y fader.
    static func makeSourceNode(ring: InputRing, state: MonitorState, format: AVAudioFormat) -> AVAudioSourceNode {
        AVAudioSourceNode(format: format) { isSilence, _, frameCount, outputData in
            let frames = Int(frameCount)
            let out = UnsafeMutableAudioBufferListPointer(outputData)
            guard state.isActive.load(ordering: .relaxed) else {
                for buffer in out {
                    if let d = buffer.mData { memset(d, 0, Int(buffer.mDataByteSize)) }
                }
                isSilence.pointee = true
                state.readPosition = -1
                return noErr
            }
            let w = ring.written.load(ordering: .acquiring)
            // Mantener un bloque de margen detrás de la escritura; resincronizar si nos atrasamos.
            let lag = w - state.readPosition
            if state.readPosition < 0 || lag < frames || lag > ring.capacity / 2 {
                state.readPosition = max(0, w - frames * 2)
            }
            let left = state.leftChannel.load(ordering: .relaxed)
            let right = state.rightChannel.load(ordering: .relaxed)
            for i in 0..<out.count {
                guard let d = out[i].mData else { continue }
                ring.read(channel: i == 0 ? left : right, from: state.readPosition, frames: frames,
                          into: d.assumingMemoryBound(to: Float.self))
            }
            state.readPosition += frames
            return noErr
        }
    }

    /// Nodo que recibe la entrada de la interfaz y la copia al buffer circular.
    static func makeSinkNode(ring: InputRing) -> AVAudioSinkNode {
        AVAudioSinkNode { _, frameCount, inputData in
            ring.write(UnsafeMutableAudioBufferListPointer(UnsafeMutablePointer(mutating: inputData)), frames: Int(frameCount))
            return noErr
        }
    }
}
