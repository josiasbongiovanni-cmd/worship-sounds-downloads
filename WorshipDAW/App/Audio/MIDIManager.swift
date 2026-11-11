import CoreMIDI
import Foundation
import Observation

/// Mensaje MIDI de canal ya interpretado.
struct MIDIMessage: Hashable, Sendable {
    enum Kind: String, Codable, Sendable {
        case controlChange = "CC"
        case noteOn = "Nota"
        case pitchBend = "Pitch"
    }

    let kind: Kind
    let channel: Int      // 1…16
    let number: Int       // CC o nota (0…127); 0 para pitch bend
    let value: Double     // 0…1

    var label: String {
        switch kind {
        case .controlChange: "CC \(number) · canal \(channel)"
        case .noteOn: "Nota \(number) · canal \(channel)"
        case .pitchBend: "Pitch bend · canal \(channel)"
        }
    }
}

/// Destino de un control MIDI aprendido (equivale a "Learn" en REAPER).
enum MIDITarget: Codable, Hashable, Sendable {
    case trackVolume(Int)
    case trackPan(Int)
    case trackMute(Int)
    case trackSolo(Int)
    case trackArm(Int)
    case masterVolume
    case playStop
    case record
    case repeatToggle
    case nextMarker
    case previousMarker
    case goToStart

    var displayName: String {
        switch self {
        case .trackVolume(let i): "Pista \(i + 1) · Volumen"
        case .trackPan(let i): "Pista \(i + 1) · Paneo"
        case .trackMute(let i): "Pista \(i + 1) · Mute"
        case .trackSolo(let i): "Pista \(i + 1) · Solo"
        case .trackArm(let i): "Pista \(i + 1) · Armar"
        case .masterVolume: "Master · Volumen"
        case .playStop: "Transporte · Reproducir/Detener"
        case .record: "Transporte · Grabar"
        case .repeatToggle: "Transporte · Repetir"
        case .nextMarker: "Transporte · Siguiente marcador"
        case .previousMarker: "Transporte · Marcador anterior"
        case .goToStart: "Transporte · Ir al inicio"
        }
    }

    /// Los botones (mute, play…) actúan al presionar; los continuos siguen el valor.
    var isContinuous: Bool {
        switch self {
        case .trackVolume, .trackPan, .masterVolume: true
        default: false
        }
    }

    static let transportTargets: [MIDITarget] = [.playStop, .record, .repeatToggle, .goToStart, .previousMarker, .nextMarker]
}

struct MIDIMapping: Identifiable, Codable, Hashable, Sendable {
    var id = UUID()
    var kind: MIDIMessage.Kind
    var channel: Int
    var number: Int
    var target: MIDITarget

    func matches(_ m: MIDIMessage) -> Bool {
        m.kind == kind && m.channel == channel && m.number == number
    }

    var sourceLabel: String {
        MIDIMessage(kind: kind, channel: channel, number: number, value: 0).label
    }
}

struct MIDISource: Identifiable, Hashable {
    let id: Int32        // kMIDIPropertyUniqueID
    let name: String
    var isEnabled: Bool
}

/// Lee todos los controladores MIDI conectados (USB, Bluetooth, red) con CoreMIDI.
@Observable
@MainActor
final class MIDIManager {
    private(set) var sources: [MIDISource] = []
    private(set) var mappings: [MIDIMapping] = []
    private(set) var lastMessage: MIDIMessage?
    /// Si no es nil, el próximo CC/nota recibido se asigna a este destino.
    var learning: MIDITarget?
    var errorMessage: String?

    /// Lo llama la app para aplicar una acción en la sesión abierta.
    @ObservationIgnored var onAction: ((MIDITarget, Double) -> Void)?

    @ObservationIgnored private var client = MIDIClientRef()
    @ObservationIgnored private var port = MIDIPortRef()
    @ObservationIgnored private var connected: Set<MIDIEndpointRef> = []
    @ObservationIgnored private var disabledIDs: Set<Int32>
    @ObservationIgnored private let defaults = UserDefaults.standard

    init() {
        disabledIDs = Set((UserDefaults.standard.array(forKey: "midi.disabled") as? [Int]).map { $0.map { Int32($0) } } ?? [])
        if let data = UserDefaults.standard.data(forKey: "midi.mappings"),
           let stored = try? JSONDecoder().decode([MIDIMapping].self, from: data) {
            mappings = stored
        }
        start()
    }

    private func start() {
        let notify = Self.makeNotifyBlock { [weak self] in self?.refreshSources() }
        var status = MIDIClientCreateWithBlock("Worship DAW" as CFString, &client, notify)
        guard status == noErr else {
            errorMessage = "No se pudo iniciar MIDI (\(status))."
            return
        }
        let receive = Self.makeReceiveBlock { [weak self] message in self?.handle(message) }
        status = MIDIInputPortCreateWithProtocol(client, "Entrada" as CFString, ._1_0, &port, receive)
        guard status == noErr else {
            errorMessage = "No se pudo abrir el puerto MIDI (\(status))."
            return
        }
        refreshSources()
    }

    func refreshSources() {
        var list: [MIDISource] = []
        var wanted: Set<MIDIEndpointRef> = []
        for i in 0..<MIDIGetNumberOfSources() {
            let endpoint = MIDIGetSource(i)
            var uid: Int32 = 0
            MIDIObjectGetIntegerProperty(endpoint, kMIDIPropertyUniqueID, &uid)
            var cfName: Unmanaged<CFString>?
            MIDIObjectGetStringProperty(endpoint, kMIDIPropertyDisplayName, &cfName)
            let name = (cfName?.takeRetainedValue() as String?) ?? "Dispositivo MIDI"
            let enabled = !disabledIDs.contains(uid)
            list.append(MIDISource(id: uid, name: name, isEnabled: enabled))
            if enabled { wanted.insert(endpoint) }
        }
        for endpoint in connected.subtracting(wanted) { MIDIPortDisconnectSource(port, endpoint) }
        for endpoint in wanted.subtracting(connected) { MIDIPortConnectSource(port, endpoint, nil) }
        connected = wanted
        sources = list
    }

    func setEnabled(_ enabled: Bool, source: MIDISource) {
        if enabled { disabledIDs.remove(source.id) } else { disabledIDs.insert(source.id) }
        defaults.set(disabledIDs.map { Int($0) }, forKey: "midi.disabled")
        refreshSources()
    }

    func mapping(for target: MIDITarget) -> MIDIMapping? {
        mappings.first { $0.target == target }
    }

    func removeMapping(_ id: UUID) {
        mappings.removeAll { $0.id == id }
        saveMappings()
    }

    func clearMapping(for target: MIDITarget) {
        mappings.removeAll { $0.target == target }
        saveMappings()
    }

    private func saveMappings() {
        if let data = try? JSONEncoder().encode(mappings) {
            defaults.set(data, forKey: "midi.mappings")
        }
    }

    private func handle(_ message: MIDIMessage) {
        lastMessage = message
        if let target = learning {
            // Ignorar el "soltar" de un botón (valor 0) mientras se aprende.
            guard message.value > 0 || message.kind == .pitchBend else { return }
            mappings.removeAll { $0.target == target || ($0.kind == message.kind && $0.channel == message.channel && $0.number == message.number) }
            mappings.append(MIDIMapping(kind: message.kind, channel: message.channel, number: message.number, target: target))
            saveMappings()
            learning = nil
            return
        }
        for mapping in mappings where mapping.matches(message) {
            if mapping.target.isContinuous {
                onAction?(mapping.target, message.value)
            } else if message.value > 0.5 {
                onAction?(mapping.target, 1)
            }
        }
    }

    // MARK: - Bloques que corren en el hilo de CoreMIDI (no aislados al MainActor)

    nonisolated private static func makeNotifyBlock(_ refresh: @escaping @MainActor () -> Void) -> MIDINotifyBlock {
        { notification in
            if notification.pointee.messageID == .msgSetupChanged {
                DispatchQueue.main.async { MainActor.assumeIsolated { refresh() } }
            }
        }
    }

    nonisolated private static func makeReceiveBlock(_ deliver: @escaping @MainActor (MIDIMessage) -> Void) -> MIDIReceiveBlock {
        { eventList, _ in
            var messages: [MIDIMessage] = []
            for packet in eventList.unsafeSequence() {
                let count = Int(packet.pointee.wordCount)
                withUnsafeBytes(of: packet.pointee.words) { raw in
                    let words = raw.bindMemory(to: UInt32.self)
                    for i in 0..<min(count, words.count) {
                        if let m = parse(words[i]) { messages.append(m) }
                    }
                }
            }
            guard !messages.isEmpty else { return }
            DispatchQueue.main.async {
                MainActor.assumeIsolated { messages.forEach(deliver) }
            }
        }
    }

    /// Interpreta una palabra UMP de tipo 0x2 (mensaje de canal MIDI 1.0).
    nonisolated static func parse(_ word: UInt32) -> MIDIMessage? {
        guard (word >> 28) & 0xF == 0x2 else { return nil }
        let status = UInt8((word >> 16) & 0xFF)
        let data1 = Int((word >> 8) & 0x7F)
        let data2 = Int(word & 0x7F)
        let channel = Int(status & 0x0F) + 1
        switch status & 0xF0 {
        case 0xB0: return MIDIMessage(kind: .controlChange, channel: channel, number: data1, value: Double(data2) / 127)
        case 0x90: return MIDIMessage(kind: .noteOn, channel: channel, number: data1, value: Double(data2) / 127)
        case 0x80: return MIDIMessage(kind: .noteOn, channel: channel, number: data1, value: 0)
        case 0xE0: return MIDIMessage(kind: .pitchBend, channel: channel, number: 0, value: Double(data2 << 7 | data1) / 16383)
        default: return nil
        }
    }
}
