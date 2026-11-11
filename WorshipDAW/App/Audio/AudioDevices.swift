import AVFoundation
import Foundation
import Observation
#if os(macOS)
import CoreAudio
import AudioToolbox
#endif

/// Dispositivo de audio (interfaz, micrófono, salida) visible para el usuario.
struct AudioDeviceInfo: Identifiable, Hashable {
    /// En Mac: UID de Core Audio. En iPhone: UID del puerto de AVAudioSession.
    let id: String
    let name: String
    let inputChannels: Int
    let outputChannels: Int
}

/// Preferencias › Audio › Dispositivo, al estilo de REAPER.
/// Se guardan en UserDefaults y se aplican al motor de la sesión abierta.
@Observable
@MainActor
final class AudioSettings {
    static let bufferSizes = [32, 64, 128, 256, 512, 1024, 2048]
    static let sampleRates: [Double] = [44_100, 48_000, 88_200, 96_000]

    var inputDevices: [AudioDeviceInfo] = []
    var outputDevices: [AudioDeviceInfo] = []

    // Valores guardados (las propiedades públicas de abajo los persisten al cambiar).
    private var storedInput: String?
    private var storedOutput: String?
    private var storedBuffer: Int
    private var storedRate: Double

    /// nil = dispositivo predeterminado del sistema.
    var inputDeviceID: String? {
        get { storedInput }
        set { storedInput = newValue; save() }
    }
    var outputDeviceID: String? {
        get { storedOutput }
        set { storedOutput = newValue; save() }
    }
    var bufferSize: Int {
        get { storedBuffer }
        set { storedBuffer = newValue; save() }
    }
    var sampleRate: Double {
        get { storedRate }
        set { storedRate = newValue; save() }
    }

    // Grabación (Preferencias › Grabación en REAPER)
    private var storedUseReported: Bool
    private var storedManualOffset: Double
    private var storedCountIn: Int

    /// Compensar la latencia que reporta el driver ("use audio driver reported latency").
    var useReportedLatency: Bool {
        get { storedUseReported }
        set { storedUseReported = newValue; save() }
    }
    /// Ajuste manual en milisegundos (positivo = mover la grabación antes).
    var manualLatencyOffsetMs: Double {
        get { storedManualOffset }
        set { storedManualOffset = newValue; save() }
    }
    /// Compases de cuenta previa antes de grabar (0 = sin cuenta).
    var countInBars: Int {
        get { storedCountIn }
        set { storedCountIn = newValue; save() }
    }
    /// Latencia de ida y vuelta que reporta el dispositivo (segundos).
    var reportedRoundTrip: Double = 0

    /// Estado real reportado por el sistema después de aplicar.
    var actualBufferSize: Int = 0
    var actualSampleRate: Double = 0
    var inputChannelCount: Int = 2
    var lastError: String?

    var latencyDescription: String {
        guard actualSampleRate > 0, actualBufferSize > 0 else { return "—" }
        let ms = Double(actualBufferSize) / actualSampleRate * 1000
        return String(format: "%d muestras · %.1f ms", actualBufferSize, ms)
    }

    @ObservationIgnored private let defaults = UserDefaults.standard

    init() {
        let d = UserDefaults.standard
        storedInput = d.string(forKey: "audio.inputDevice")
        storedOutput = d.string(forKey: "audio.outputDevice")
        let buffer = d.integer(forKey: "audio.bufferSize")
        storedBuffer = max(0, buffer)  // 0 = no pedir (como REAPER por defecto)
        let rate = d.double(forKey: "audio.sampleRate")
        storedRate = max(0, rate)
        storedUseReported = d.object(forKey: "rec.useReportedLatency") as? Bool ?? true
        storedManualOffset = d.double(forKey: "rec.manualOffsetMs")
        storedCountIn = max(0, d.integer(forKey: "rec.countInBars"))
        refreshDevices()
    }

    private func save() {
        defaults.set(storedInput, forKey: "audio.inputDevice")
        defaults.set(storedOutput, forKey: "audio.outputDevice")
        defaults.set(storedBuffer, forKey: "audio.bufferSize")
        defaults.set(storedRate, forKey: "audio.sampleRate")
        defaults.set(storedUseReported, forKey: "rec.useReportedLatency")
        defaults.set(storedManualOffset, forKey: "rec.manualOffsetMs")
        defaults.set(storedCountIn, forKey: "rec.countInBars")
    }

    func refreshDevices() {
        #if os(macOS)
        let all = CoreAudioDevices.all()
        inputDevices = all.filter { $0.inputChannels > 0 }
        outputDevices = all.filter { $0.outputChannels > 0 }
        #else
        let session = AVAudioSession.sharedInstance()
        inputDevices = (session.availableInputs ?? []).map {
            AudioDeviceInfo(id: $0.uid, name: $0.portName, inputChannels: $0.channels?.count ?? 1, outputChannels: 0)
        }
        outputDevices = session.currentRoute.outputs.map {
            AudioDeviceInfo(id: $0.uid, name: $0.portName, inputChannels: 0, outputChannels: $0.channels?.count ?? 2)
        }
        #endif
    }

    /// Aplica la configuración al motor. Debe llamarse con el motor detenido.
    func apply(to engine: AVAudioEngine) {
        lastError = nil
        #if os(macOS)
        let outID = outputDeviceID.flatMap(CoreAudioDevices.deviceID(forUID:)) ?? CoreAudioDevices.defaultDevice(input: false)
        let inID = inputDeviceID.flatMap(CoreAudioDevices.deviceID(forUID:)) ?? CoreAudioDevices.defaultDevice(input: true)
        if let outID {
            if sampleRate > 0 { CoreAudioDevices.setNominalSampleRate(sampleRate, device: outID) }
            if bufferSize > 0 { CoreAudioDevices.setBufferSize(bufferSize, device: outID) }
            if let unit = engine.outputNode.audioUnit, CoreAudioDevices.setCurrentDevice(outID, on: unit) != noErr {
                lastError = "No se pudo usar la salida seleccionada."
            }
            actualBufferSize = CoreAudioDevices.bufferSize(device: outID)
            actualSampleRate = CoreAudioDevices.nominalSampleRate(device: outID)
        }
        if let inID {
            if inID != outID {
                if sampleRate > 0 { CoreAudioDevices.setNominalSampleRate(sampleRate, device: inID) }
                if bufferSize > 0 { CoreAudioDevices.setBufferSize(bufferSize, device: inID) }
            }
            inputChannelCount = max(1, CoreAudioDevices.channelCount(device: inID, input: true))
        }
        #else
        let session = AVAudioSession.sharedInstance()
        if sampleRate > 0 { try? session.setPreferredSampleRate(sampleRate) }
        if bufferSize > 0 { try? session.setPreferredIOBufferDuration(Double(bufferSize) / max(1, session.sampleRate)) }
        if let id = inputDeviceID, let port = session.availableInputs?.first(where: { $0.uid == id }) {
            do { try session.setPreferredInput(port) } catch { lastError = "No se pudo usar la entrada seleccionada." }
        }
        if session.maximumInputNumberOfChannels > 1 {
            try? session.setPreferredInputNumberOfChannels(session.maximumInputNumberOfChannels)
        }
        actualSampleRate = session.sampleRate
        actualBufferSize = Int((session.ioBufferDuration * session.sampleRate).rounded())
        inputChannelCount = max(1, session.inputNumberOfChannels)
        #endif
    }

    /// Canal de entrada que se debe aplicar al nodo de entrada (solo Mac).
    func applyInput(to engine: AVAudioEngine) {
        #if os(macOS)
        guard let inID = inputDeviceID.flatMap(CoreAudioDevices.deviceID(forUID:)),
              let unit = engine.inputNode.audioUnit else { return }
        if CoreAudioDevices.setCurrentDevice(inID, on: unit) != noErr {
            lastError = "No se pudo usar la entrada seleccionada."
        }
        #endif
    }
}

#if os(macOS)
/// Acceso directo a Core Audio (lo que REAPER llama "CoreAudio device").
enum CoreAudioDevices {
    private static func address(_ selector: AudioObjectPropertySelector,
                                scope: AudioObjectPropertyScope = kAudioObjectPropertyScopeGlobal) -> AudioObjectPropertyAddress {
        AudioObjectPropertyAddress(mSelector: selector, mScope: scope, mElement: kAudioObjectPropertyElementMain)
    }

    static func allIDs() -> [AudioDeviceID] {
        var addr = address(kAudioHardwarePropertyDevices)
        var size: UInt32 = 0
        guard AudioObjectGetPropertyDataSize(AudioObjectID(kAudioObjectSystemObject), &addr, 0, nil, &size) == noErr else { return [] }
        var ids = [AudioDeviceID](repeating: 0, count: Int(size) / MemoryLayout<AudioDeviceID>.size)
        guard AudioObjectGetPropertyData(AudioObjectID(kAudioObjectSystemObject), &addr, 0, nil, &size, &ids) == noErr else { return [] }
        return ids
    }

    static func all() -> [AudioDeviceInfo] {
        allIDs().compactMap { id in
            guard let uid = string(kAudioDevicePropertyDeviceUID, device: id) else { return nil }
            let name = string(kAudioObjectPropertyName, device: id) ?? uid
            return AudioDeviceInfo(id: uid, name: name,
                                   inputChannels: channelCount(device: id, input: true),
                                   outputChannels: channelCount(device: id, input: false))
        }
    }

    static func string(_ selector: AudioObjectPropertySelector, device: AudioDeviceID) -> String? {
        var addr = address(selector)
        var value: Unmanaged<CFString>?
        var size = UInt32(MemoryLayout<Unmanaged<CFString>?>.size)
        guard AudioObjectGetPropertyData(device, &addr, 0, nil, &size, &value) == noErr, let value else { return nil }
        return value.takeRetainedValue() as String
    }

    static func channelCount(device: AudioDeviceID, input: Bool) -> Int {
        var addr = address(kAudioDevicePropertyStreamConfiguration,
                           scope: input ? kAudioDevicePropertyScopeInput : kAudioDevicePropertyScopeOutput)
        var size: UInt32 = 0
        guard AudioObjectGetPropertyDataSize(device, &addr, 0, nil, &size) == noErr, size > 0 else { return 0 }
        let raw = UnsafeMutableRawPointer.allocate(byteCount: Int(size), alignment: MemoryLayout<AudioBufferList>.alignment)
        defer { raw.deallocate() }
        guard AudioObjectGetPropertyData(device, &addr, 0, nil, &size, raw) == noErr else { return 0 }
        let list = UnsafeMutableAudioBufferListPointer(raw.assumingMemoryBound(to: AudioBufferList.self))
        return list.reduce(0) { $0 + Int($1.mNumberChannels) }
    }

    static func deviceID(forUID uid: String) -> AudioDeviceID? {
        allIDs().first { string(kAudioDevicePropertyDeviceUID, device: $0) == uid }
    }

    static func defaultDevice(input: Bool) -> AudioDeviceID? {
        var addr = address(input ? kAudioHardwarePropertyDefaultInputDevice : kAudioHardwarePropertyDefaultOutputDevice)
        var id = AudioDeviceID(0)
        var size = UInt32(MemoryLayout<AudioDeviceID>.size)
        guard AudioObjectGetPropertyData(AudioObjectID(kAudioObjectSystemObject), &addr, 0, nil, &size, &id) == noErr, id != 0 else { return nil }
        return id
    }

    static func bufferSize(device: AudioDeviceID) -> Int {
        var addr = address(kAudioDevicePropertyBufferFrameSize)
        var frames = UInt32(0)
        var size = UInt32(MemoryLayout<UInt32>.size)
        guard AudioObjectGetPropertyData(device, &addr, 0, nil, &size, &frames) == noErr else { return 0 }
        return Int(frames)
    }

    static func setBufferSize(_ frames: Int, device: AudioDeviceID) {
        guard bufferSize(device: device) != frames else { return }
        var addr = address(kAudioDevicePropertyBufferFrameSize)
        var value = UInt32(frames)
        AudioObjectSetPropertyData(device, &addr, 0, nil, UInt32(MemoryLayout<UInt32>.size), &value)
    }

    static func nominalSampleRate(device: AudioDeviceID) -> Double {
        var addr = address(kAudioDevicePropertyNominalSampleRate)
        var rate = Float64(0)
        var size = UInt32(MemoryLayout<Float64>.size)
        guard AudioObjectGetPropertyData(device, &addr, 0, nil, &size, &rate) == noErr else { return 0 }
        return rate
    }

    static func setNominalSampleRate(_ rate: Double, device: AudioDeviceID) {
        guard abs(nominalSampleRate(device: device) - rate) > 0.5 else { return }
        var addr = address(kAudioDevicePropertyNominalSampleRate)
        var value = Float64(rate)
        AudioObjectSetPropertyData(device, &addr, 0, nil, UInt32(MemoryLayout<Float64>.size), &value)
    }

    static func setCurrentDevice(_ device: AudioDeviceID, on unit: AudioUnit) -> OSStatus {
        // No reasignar el mismo dispositivo: dispararía otro cambio de configuración del motor.
        var current = AudioDeviceID(0)
        var size = UInt32(MemoryLayout<AudioDeviceID>.size)
        if AudioUnitGetProperty(unit, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &current, &size) == noErr,
           current == device { return noErr }
        var id = device
        return AudioUnitSetProperty(unit, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0,
                                    &id, UInt32(MemoryLayout<AudioDeviceID>.size))
    }
}
#endif
