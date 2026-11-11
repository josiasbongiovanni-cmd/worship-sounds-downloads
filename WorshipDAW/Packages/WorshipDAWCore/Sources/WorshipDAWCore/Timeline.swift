import Foundation

/// Conversión entre tiempo (segundos) y posición musical (compases/tiempos).
public struct Timeline: Hashable, Sendable {
    public var bpm: Double
    public var beatsPerBar: Int

    public init(bpm: Double, beatsPerBar: Int) {
        self.bpm = max(20, min(300, bpm))
        self.beatsPerBar = max(1, beatsPerBar)
    }

    public var secondsPerBeat: Double { 60 / bpm }
    public var secondsPerBar: Double { secondsPerBeat * Double(beatsPerBar) }

    public func beats(at seconds: TimeInterval) -> Double { seconds / secondsPerBeat }
    public func seconds(atBeat beat: Double) -> TimeInterval { beat * secondsPerBeat }
    public func seconds(atBar bar: Double) -> TimeInterval { bar * secondsPerBar }

    /// Ajusta un tiempo a la grilla. `division` = tiempos por celda (1 = negra, 0.5 = corchea, 4 = compás de 4/4).
    public func snap(_ seconds: TimeInterval, division: Double = 1) -> TimeInterval {
        let cell = secondsPerBeat * division
        guard cell > 0 else { return seconds }
        return max(0, (seconds / cell).rounded() * cell)
    }

    /// Compás (base 1) y tiempo (base 1) en una posición dada.
    public func barBeat(at seconds: TimeInterval) -> (bar: Int, beat: Int, tick: Int) {
        let totalBeats = max(0, beats(at: seconds) + 1e-9)
        let bar = Int(totalBeats / Double(beatsPerBar))
        let beatInBar = totalBeats - Double(bar * beatsPerBar)
        let beat = Int(beatInBar)
        let tick = Int((beatInBar - Double(beat)) * 100)
        return (bar + 1, beat + 1, min(99, tick))
    }

    /// "12.3.50"
    public func barBeatString(at seconds: TimeInterval) -> String {
        let p = barBeat(at: seconds)
        return String(format: "%d.%d.%02d", p.bar, p.beat, p.tick)
    }

    /// "1:23.456"
    public static func timecode(_ seconds: TimeInterval) -> String {
        let s = max(0, seconds)
        let minutes = Int(s / 60)
        let rest = s - Double(minutes * 60)
        return String(format: "%d:%06.3f", minutes, rest)
    }
}
