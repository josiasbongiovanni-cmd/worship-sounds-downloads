import AVFoundation
import Foundation
import WorshipDAWCore

enum AudioFileError: LocalizedError {
    case unreadable(String)
    case conversionFailed(String)

    var errorDescription: String? {
        switch self {
        case .unreadable(let name): "No se pudo leer el archivo \(name)."
        case .conversionFailed(let reason): "No se pudo convertir el audio: \(reason)"
        }
    }
}

/// Formato interno de trabajo: 48 kHz, estéreo, float32 no intercalado.
/// Todo el audio importado o grabado se convierte a este formato para que
/// los `AVAudioPlayerNode` puedan reproducir cualquier clip sin conversión.
enum AudioFormats {
    static func project(sampleRate: Double = 48_000) -> AVAudioFormat {
        AVAudioFormat(standardFormatWithSampleRate: sampleRate, channels: 2)!
    }
}

enum AudioConverterService {

    /// Convierte cualquier archivo legible por Core Audio (wav, aiff, mp3, m4a, caf…)
    /// a un .caf en el formato del proyecto. Devuelve la duración en segundos.
    static func convert(source: URL, destination: URL, format: AVAudioFormat,
                        channelMap: [Int]? = nil) throws -> TimeInterval {
        let input: AVAudioFile
        do {
            input = try AVAudioFile(forReading: source)
        } catch {
            throw AudioFileError.unreadable(source.lastPathComponent)
        }
        let inFormat = input.processingFormat
        guard let converter = AVAudioConverter(from: inFormat, to: format) else {
            throw AudioFileError.conversionFailed("formato no soportado")
        }
        if let channelMap {
            // Extraer canales concretos de una grabación multicanal (entrada de la pista).
            let maxChannel = Int(inFormat.channelCount) - 1
            converter.channelMap = channelMap.map { NSNumber(value: min($0, maxChannel)) }
        } else if inFormat.channelCount == 1 && format.channelCount == 2 {
            // Mono → estéreo: duplicar el canal en ambos lados.
            converter.channelMap = [0, 0]
        } else if inFormat.channelCount > 2 && format.channelCount == 2 {
            converter.channelMap = [0, 1]
        }
        converter.sampleRateConverterQuality = AVAudioQuality.max.rawValue

        let output = try AVAudioFile(forWriting: destination, settings: format.settings,
                                     commonFormat: .pcmFormatFloat32, interleaved: false)

        let chunk: AVAudioFrameCount = 16_384
        guard let inBuffer = AVAudioPCMBuffer(pcmFormat: inFormat, frameCapacity: chunk) else {
            throw AudioFileError.conversionFailed("sin memoria")
        }
        let ratio = format.sampleRate / inFormat.sampleRate
        let outCapacity = AVAudioFrameCount(Double(chunk) * ratio) + 4096
        var reachedEnd = false

        while true {
            guard let outBuffer = AVAudioPCMBuffer(pcmFormat: format, frameCapacity: outCapacity) else {
                throw AudioFileError.conversionFailed("sin memoria")
            }
            var conversionError: NSError?
            let status = converter.convert(to: outBuffer, error: &conversionError) { _, inputStatus in
                if reachedEnd {
                    inputStatus.pointee = .endOfStream
                    return nil
                }
                do {
                    try input.read(into: inBuffer, frameCount: chunk)
                } catch {
                    reachedEnd = true
                    inputStatus.pointee = .endOfStream
                    return nil
                }
                if inBuffer.frameLength == 0 {
                    reachedEnd = true
                    inputStatus.pointee = .endOfStream
                    return nil
                }
                inputStatus.pointee = .haveData
                return inBuffer
            }
            if status == .error {
                throw AudioFileError.conversionFailed(conversionError?.localizedDescription ?? "error desconocido")
            }
            if outBuffer.frameLength > 0 {
                try output.write(from: outBuffer)
            }
            if status == .endOfStream { break }
            if status == .inputRanDry && reachedEnd { break }
        }
        return Double(output.length) / format.sampleRate
    }
}

/// Picos de forma de onda precalculados (un valor máximo absoluto cada `framesPerPeak` frames).
struct WaveformPeaks: Sendable {
    static let framesPerPeak = 512

    let peaks: [Float]
    let sampleRate: Double

    var peaksPerSecond: Double { sampleRate / Double(Self.framesPerPeak) }

    static func compute(from url: URL) throws -> WaveformPeaks {
        let file = try AVAudioFile(forReading: url)
        let format = file.processingFormat
        let chunk: AVAudioFrameCount = AVAudioFrameCount(framesPerPeak * 64)
        guard let buffer = AVAudioPCMBuffer(pcmFormat: format, frameCapacity: chunk) else {
            return WaveformPeaks(peaks: [], sampleRate: format.sampleRate)
        }
        var peaks: [Float] = []
        peaks.reserveCapacity(Int(file.length) / framesPerPeak + 1)
        while file.framePosition < file.length {
            try file.read(into: buffer, frameCount: chunk)
            let frames = Int(buffer.frameLength)
            if frames == 0 { break }
            guard let channels = buffer.floatChannelData else { break }
            var i = 0
            while i < frames {
                let end = min(i + framesPerPeak, frames)
                var peak: Float = 0
                for c in 0..<Int(format.channelCount) {
                    let data = channels[c]
                    for f in i..<end {
                        let v = abs(data[f])
                        if v > peak { peak = v }
                    }
                }
                peaks.append(min(1, peak))
                i = end
            }
        }
        return WaveformPeaks(peaks: peaks, sampleRate: format.sampleRate)
    }

    func write(to url: URL) throws {
        let data = peaks.withUnsafeBufferPointer { Data(buffer: $0) }
        try data.write(to: url, options: .atomic)
    }

    static func read(from url: URL, sampleRate: Double) throws -> WaveformPeaks {
        let data = try Data(contentsOf: url)
        let count = data.count / MemoryLayout<Float>.size
        let peaks = data.withUnsafeBytes { raw in
            Array(raw.bindMemory(to: Float.self).prefix(count))
        }
        return WaveformPeaks(peaks: peaks, sampleRate: sampleRate)
    }

    /// Valor máximo en el rango de tiempo [from, to) del archivo fuente.
    func peak(from: TimeInterval, to: TimeInterval) -> Float {
        guard !peaks.isEmpty else { return 0 }
        let a = max(0, Int(from * peaksPerSecond))
        let b = min(peaks.count, max(a + 1, Int(to * peaksPerSecond)))
        guard a < b else { return 0 }
        var m: Float = 0
        for i in a..<b where peaks[i] > m { m = peaks[i] }
        return m
    }
}

/// Caché en memoria + disco de formas de onda por archivo.
@MainActor
final class WaveformCache {
    private var cache: [String: WaveformPeaks] = [:]
    private var loading: Set<String> = []
    var onLoaded: (() -> Void)?

    func peaks(for fileName: String, projectURL: URL, sampleRate: Double) -> WaveformPeaks? {
        if let cached = cache[fileName] { return cached }
        guard !loading.contains(fileName) else { return nil }
        loading.insert(fileName)
        let audioURL = ProjectStore.audioDirectory(of: projectURL).appendingPathComponent(fileName)
        let peaksURL = ProjectStore.peaksDirectory(of: projectURL).appendingPathComponent(fileName + ".peaks")
        Task.detached(priority: .utility) {
            var result: WaveformPeaks?
            if let stored = try? WaveformPeaks.read(from: peaksURL, sampleRate: sampleRate), !stored.peaks.isEmpty {
                result = stored
            } else if let computed = try? WaveformPeaks.compute(from: audioURL) {
                try? computed.write(to: peaksURL)
                result = computed
            }
            await MainActor.run {
                self.loading.remove(fileName)
                if let result {
                    self.cache[fileName] = result
                    self.onLoaded?()
                }
            }
        }
        return nil
    }

    func store(_ peaks: WaveformPeaks, for fileName: String) {
        cache[fileName] = peaks
    }

    func clear() {
        cache.removeAll()
        loading.removeAll()
    }
}
