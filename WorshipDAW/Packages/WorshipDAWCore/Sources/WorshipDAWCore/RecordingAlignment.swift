import Foundation

/// Dónde colocar un ítem grabado en la línea de tiempo, compensando la latencia de la interfaz
/// (como "use audio driver reported latency" en REAPER) y descartando la cuenta previa.
public enum RecordingAlignment {
    /// - Parameters:
    ///   - playFrom: posición desde la que arrancó la reproducción (negativa si hubo cuenta previa).
    ///   - editCursor: posición donde empieza la grabación (el cursor de edición).
    ///   - captureDelay: segundos entre el inicio de la reproducción y la primera muestra capturada.
    ///   - latency: latencia total de ida y vuelta a compensar (entrada + salida + ajuste manual).
    /// - Returns: inicio del ítem y segundos a saltar del principio del archivo.
    public static func compute(playFrom: TimeInterval, editCursor: TimeInterval,
                               captureDelay: TimeInterval, latency: TimeInterval) -> (start: TimeInterval, fileOffset: TimeInterval) {
        // Posición de la línea de tiempo que corresponde a la primera muestra del archivo.
        let fileStartsAt = playFrom + captureDelay - latency
        let start = max(fileStartsAt, editCursor, 0)
        return (start, start - fileStartsAt)
    }
}
