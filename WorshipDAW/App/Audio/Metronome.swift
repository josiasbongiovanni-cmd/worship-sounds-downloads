import AVFoundation
import WorshipDAWCore

/// Genera el buffer de un compás de click (acento en el tiempo 1).
enum Metronome {
    static func barBuffer(timeline: Timeline, format: AVAudioFormat) -> AVAudioPCMBuffer? {
        let sr = format.sampleRate
        let beatFrames = Int((timeline.secondsPerBeat * sr).rounded())
        let barFrames = beatFrames * timeline.beatsPerBar
        guard barFrames > 0,
              let buffer = AVAudioPCMBuffer(pcmFormat: format, frameCapacity: AVAudioFrameCount(barFrames)),
              let channels = buffer.floatChannelData else { return nil }
        buffer.frameLength = AVAudioFrameCount(barFrames)
        for c in 0..<Int(format.channelCount) {
            channels[c].update(repeating: 0, count: barFrames)
        }
        let clickFrames = min(beatFrames, Int(0.035 * sr))
        for beat in 0..<timeline.beatsPerBar {
            let accent = beat == 0
            let freq = accent ? 1_760.0 : 1_175.0
            let amp: Float = accent ? 0.9 : 0.6
            let startFrame = beat * beatFrames
            for i in 0..<clickFrames {
                let t = Double(i) / sr
                let env = Float(exp(-t * 90))
                let sample = Float(sin(2 * .pi * freq * t)) * env * amp
                for c in 0..<Int(format.channelCount) {
                    channels[c][startFrame + i] = sample
                }
            }
        }
        return buffer
    }

    /// Copia del buffer a partir de `offset` frames (para arrancar a mitad de compás).
    static func tail(of buffer: AVAudioPCMBuffer, from offset: Int) -> AVAudioPCMBuffer? {
        let total = Int(buffer.frameLength)
        let remaining = total - offset
        guard remaining > 0,
              let out = AVAudioPCMBuffer(pcmFormat: buffer.format, frameCapacity: AVAudioFrameCount(remaining)),
              let src = buffer.floatChannelData, let dst = out.floatChannelData else { return nil }
        out.frameLength = AVAudioFrameCount(remaining)
        for c in 0..<Int(buffer.format.channelCount) {
            dst[c].update(from: src[c].advanced(by: offset), count: remaining)
        }
        return out
    }
}
