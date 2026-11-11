import Foundation

// MARK: - Colores

/// Paleta de colores para pistas y secciones. La UI traduce cada caso a un color real.
public enum TrackColor: String, Codable, CaseIterable, Sendable {
    case violet, indigo, sky, teal, mint, lime, amber, coral, rose, slate
}

// MARK: - Secciones de canción (estructura worship)

public enum SectionKind: String, Codable, CaseIterable, Sendable {
    case intro, verse, preChorus, chorus, bridge, interlude, tag, vamp, outro

    /// Nombre visible en español.
    public var displayName: String {
        switch self {
        case .intro: "Intro"
        case .verse: "Verso"
        case .preChorus: "Pre-Coro"
        case .chorus: "Coro"
        case .bridge: "Puente"
        case .interlude: "Interludio"
        case .tag: "Tag"
        case .vamp: "Vamp"
        case .outro: "Final"
        }
    }

    /// Abreviatura corta para pantallas pequeñas (iPhone).
    public var shortName: String {
        switch self {
        case .intro: "In"
        case .verse: "V"
        case .preChorus: "PC"
        case .chorus: "C"
        case .bridge: "P"
        case .interlude: "Int"
        case .tag: "T"
        case .vamp: "Vp"
        case .outro: "F"
        }
    }

    public var defaultColor: TrackColor {
        switch self {
        case .intro: .slate
        case .verse: .sky
        case .preChorus: .teal
        case .chorus: .violet
        case .bridge: .amber
        case .interlude: .indigo
        case .tag: .rose
        case .vamp: .coral
        case .outro: .slate
        }
    }
}

public struct SongSection: Identifiable, Codable, Hashable, Sendable {
    public var id: UUID
    public var name: String
    public var kind: SectionKind
    /// Inicio en segundos dentro de la línea de tiempo.
    public var start: TimeInterval
    /// Duración en segundos.
    public var length: TimeInterval

    public var end: TimeInterval { start + length }

    public init(id: UUID = UUID(), name: String? = nil, kind: SectionKind, start: TimeInterval, length: TimeInterval) {
        self.id = id
        self.name = name ?? kind.displayName
        self.kind = kind
        self.start = start
        self.length = length
    }
}

// MARK: - Plugins (AU / AUv3)

/// Referencia serializable a un Audio Unit insertado en una pista.
/// Los códigos son los de `AudioComponentDescription` (FourCC como UInt32).
public struct PluginRef: Identifiable, Codable, Hashable, Sendable {
    public var id: UUID
    public var name: String
    public var manufacturerName: String
    public var componentType: UInt32
    public var componentSubType: UInt32
    public var componentManufacturer: UInt32
    public var isBypassed: Bool
    /// Estado completo del plugin (`fullState` serializado como property list).
    public var state: Data?

    public init(id: UUID = UUID(), name: String, manufacturerName: String,
                componentType: UInt32, componentSubType: UInt32, componentManufacturer: UInt32,
                isBypassed: Bool = false, state: Data? = nil) {
        self.id = id
        self.name = name
        self.manufacturerName = manufacturerName
        self.componentType = componentType
        self.componentSubType = componentSubType
        self.componentManufacturer = componentManufacturer
        self.isBypassed = isBypassed
        self.state = state
    }
}

// MARK: - Clips

public struct Clip: Identifiable, Codable, Hashable, Sendable {
    public var id: UUID
    public var name: String
    /// Nombre del archivo dentro de la carpeta `Audio/` del proyecto.
    public var fileName: String
    /// Posición en la línea de tiempo (segundos).
    public var start: TimeInterval
    /// Desplazamiento dentro del archivo de audio (segundos).
    public var offset: TimeInterval
    /// Duración audible (segundos).
    public var duration: TimeInterval
    /// Duración total del archivo fuente (para limitar el recorte).
    public var sourceDuration: TimeInterval
    public var gainDB: Double

    public var end: TimeInterval { start + duration }

    public init(id: UUID = UUID(), name: String, fileName: String, start: TimeInterval,
                offset: TimeInterval = 0, duration: TimeInterval, sourceDuration: TimeInterval? = nil,
                gainDB: Double = 0) {
        self.id = id
        self.name = name
        self.fileName = fileName
        self.start = start
        self.offset = offset
        self.duration = duration
        self.sourceDuration = sourceDuration ?? (offset + duration)
        self.gainDB = gainDB
    }
}

// MARK: - Pistas

public enum TrackRole: String, Codable, CaseIterable, Sendable {
    /// Pista de audio normal.
    case audio
    /// Pista de guía hablada ("Verso… Coro…") para el equipo de alabanza.
    case guide

    public var displayName: String {
        switch self {
        case .audio: "Audio"
        case .guide: "Guía"
        }
    }
}

public struct ChannelEQ: Codable, Hashable, Sendable {
    public var lowGain: Double = 0   // dB, shelf 100 Hz
    public var midGain: Double = 0   // dB, campana 1 kHz
    public var highGain: Double = 0  // dB, shelf 8 kHz
    public var isEnabled: Bool = true

    public init(lowGain: Double = 0, midGain: Double = 0, highGain: Double = 0, isEnabled: Bool = true) {
        self.lowGain = lowGain
        self.midGain = midGain
        self.highGain = highGain
        self.isEnabled = isEnabled
    }

    public var isFlat: Bool { lowGain == 0 && midGain == 0 && highGain == 0 }
}

/// Entrada de grabación de una pista, como el menú del botón de armar en REAPER.
public enum TrackInput: Codable, Hashable, Sendable {
    case off
    /// Canal de entrada (base 0) de la interfaz.
    case mono(Int)
    /// Par estéreo que empieza en el canal indicado (base 0): 0 = Entradas 1/2.
    case stereo(Int)

    public var displayName: String {
        switch self {
        case .off: "Sin entrada"
        case .mono(let c): "Entrada \(c + 1)"
        case .stereo(let c): "Entradas \(c + 1)/\(c + 2)"
        }
    }

    public var shortName: String {
        switch self {
        case .off: "—"
        case .mono(let c): "In \(c + 1)"
        case .stereo(let c): "In \(c + 1)/\(c + 2)"
        }
    }

    /// Mapa de canales para convertir la grabación multicanal a estéreo.
    public var channelMap: [Int]? {
        switch self {
        case .off: nil
        case .mono(let c): [c, c]
        case .stereo(let c): [c, c + 1]
        }
    }

    public var highestChannel: Int {
        switch self {
        case .off: -1
        case .mono(let c): c
        case .stereo(let c): c + 1
        }
    }
}

public struct Track: Identifiable, Codable, Hashable, Sendable {
    public var id: UUID
    public var name: String
    public var color: TrackColor
    public var role: TrackRole
    /// Volumen del fader en dB (-inf representado como -60 o menos).
    public var volumeDB: Double
    /// Panorama -1 (izquierda) … 1 (derecha).
    public var pan: Double
    public var isMuted: Bool
    public var isSolo: Bool
    public var isArmed: Bool
    public var input: TrackInput
    public var eq: ChannelEQ
    public var inserts: [PluginRef]
    public var clips: [Clip]

    public init(id: UUID = UUID(), name: String, color: TrackColor = .violet, role: TrackRole = .audio,
                volumeDB: Double = 0, pan: Double = 0, isMuted: Bool = false, isSolo: Bool = false,
                isArmed: Bool = false, input: TrackInput = .mono(0), eq: ChannelEQ = ChannelEQ(),
                inserts: [PluginRef] = [], clips: [Clip] = []) {
        self.id = id
        self.name = name
        self.color = color
        self.role = role
        self.volumeDB = volumeDB
        self.pan = pan
        self.isMuted = isMuted
        self.isSolo = isSolo
        self.isArmed = isArmed
        self.input = input
        self.eq = eq
        self.inserts = inserts
        self.clips = clips
    }

    public var end: TimeInterval { clips.map(\.end).max() ?? 0 }
}

extension Track {
    /// Lectura tolerante: los proyectos guardados antes de agregar un campo siguen abriendo.
    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        id = try c.decode(UUID.self, forKey: .id)
        name = try c.decode(String.self, forKey: .name)
        color = try c.decodeIfPresent(TrackColor.self, forKey: .color) ?? .violet
        role = try c.decodeIfPresent(TrackRole.self, forKey: .role) ?? .audio
        volumeDB = try c.decodeIfPresent(Double.self, forKey: .volumeDB) ?? 0
        pan = try c.decodeIfPresent(Double.self, forKey: .pan) ?? 0
        isMuted = try c.decodeIfPresent(Bool.self, forKey: .isMuted) ?? false
        isSolo = try c.decodeIfPresent(Bool.self, forKey: .isSolo) ?? false
        isArmed = try c.decodeIfPresent(Bool.self, forKey: .isArmed) ?? false
        input = try c.decodeIfPresent(TrackInput.self, forKey: .input) ?? .mono(0)
        eq = try c.decodeIfPresent(ChannelEQ.self, forKey: .eq) ?? ChannelEQ()
        inserts = try c.decodeIfPresent([PluginRef].self, forKey: .inserts) ?? []
        clips = try c.decodeIfPresent([Clip].self, forKey: .clips) ?? []
    }
}

// MARK: - Marcadores

/// Marcador numerado (tecla M en REAPER; las teclas 1–9 saltan a cada uno).
public struct Marker: Identifiable, Codable, Hashable, Sendable {
    public var id: UUID
    public var number: Int
    public var name: String
    public var time: TimeInterval

    public init(id: UUID = UUID(), number: Int, name: String = "", time: TimeInterval) {
        self.id = id
        self.number = number
        self.name = name
        self.time = time
    }
}

// MARK: - Proyecto

public struct LoopRegion: Codable, Hashable, Sendable {
    public var start: TimeInterval
    public var end: TimeInterval
    public var isEnabled: Bool

    public init(start: TimeInterval = 0, end: TimeInterval = 8, isEnabled: Bool = false) {
        self.start = start
        self.end = end
        self.isEnabled = isEnabled
    }

    public var length: TimeInterval { max(0, end - start) }
}

public struct Project: Identifiable, Codable, Hashable, Sendable {
    public static let currentVersion = 1

    public var version: Int
    public var id: UUID
    public var name: String
    public var bpm: Double
    public var beatsPerBar: Int
    /// Tonalidad de la canción, por ejemplo "G", "Bb", "F#m".
    public var key: String
    public var sampleRate: Double
    public var tracks: [Track]
    public var sections: [SongSection]
    public var markers: [Marker]
    public var loop: LoopRegion
    public var isClickEnabled: Bool
    public var clickVolumeDB: Double
    public var masterVolumeDB: Double
    public var createdAt: Date
    public var modifiedAt: Date

    public init(id: UUID = UUID(), name: String, bpm: Double = 72, beatsPerBar: Int = 4, key: String = "G",
                sampleRate: Double = 48_000, tracks: [Track] = [], sections: [SongSection] = [],
                markers: [Marker] = [], loop: LoopRegion = LoopRegion(), isClickEnabled: Bool = true, clickVolumeDB: Double = -6,
                masterVolumeDB: Double = 0, createdAt: Date = Date(), modifiedAt: Date = Date()) {
        self.version = Project.currentVersion
        self.id = id
        self.name = name
        self.bpm = bpm
        self.beatsPerBar = beatsPerBar
        self.key = key
        self.sampleRate = sampleRate
        self.tracks = tracks
        self.sections = sections
        self.markers = markers
        self.loop = loop
        self.isClickEnabled = isClickEnabled
        self.clickVolumeDB = clickVolumeDB
        self.masterVolumeDB = masterVolumeDB
        self.createdAt = createdAt
        self.modifiedAt = modifiedAt
    }

    public var timeline: Timeline { Timeline(bpm: bpm, beatsPerBar: beatsPerBar) }

    /// Final del contenido (clips o secciones).
    public var contentEnd: TimeInterval {
        max(tracks.map(\.end).max() ?? 0, sections.map(\.end).max() ?? 0)
    }

    public var hasSoloedTrack: Bool { tracks.contains { $0.isSolo } }

    /// Una pista suena si no está muteada y, cuando hay pistas en solo, ella también lo está.
    public func isAudible(_ track: Track) -> Bool {
        if track.isMuted { return false }
        if hasSoloedTrack { return track.isSolo }
        return true
    }

    public func trackIndex(_ id: UUID) -> Int? { tracks.firstIndex { $0.id == id } }

    public func clipLocation(_ clipID: UUID) -> (track: Int, clip: Int)? {
        for (t, track) in tracks.enumerated() {
            if let c = track.clips.firstIndex(where: { $0.id == clipID }) { return (t, c) }
        }
        return nil
    }

    /// Todos los archivos de audio referenciados por el proyecto.
    public var referencedAudioFiles: Set<String> {
        Set(tracks.flatMap { $0.clips.map(\.fileName) })
    }
}

extension Project {
    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        version = try c.decodeIfPresent(Int.self, forKey: .version) ?? Project.currentVersion
        id = try c.decode(UUID.self, forKey: .id)
        name = try c.decode(String.self, forKey: .name)
        bpm = try c.decodeIfPresent(Double.self, forKey: .bpm) ?? 120
        beatsPerBar = try c.decodeIfPresent(Int.self, forKey: .beatsPerBar) ?? 4
        key = try c.decodeIfPresent(String.self, forKey: .key) ?? "C"
        sampleRate = try c.decodeIfPresent(Double.self, forKey: .sampleRate) ?? 48_000
        tracks = try c.decodeIfPresent([Track].self, forKey: .tracks) ?? []
        sections = try c.decodeIfPresent([SongSection].self, forKey: .sections) ?? []
        markers = try c.decodeIfPresent([Marker].self, forKey: .markers) ?? []
        loop = try c.decodeIfPresent(LoopRegion.self, forKey: .loop) ?? LoopRegion()
        isClickEnabled = try c.decodeIfPresent(Bool.self, forKey: .isClickEnabled) ?? true
        clickVolumeDB = try c.decodeIfPresent(Double.self, forKey: .clickVolumeDB) ?? -6
        masterVolumeDB = try c.decodeIfPresent(Double.self, forKey: .masterVolumeDB) ?? 0
        createdAt = try c.decodeIfPresent(Date.self, forKey: .createdAt) ?? Date()
        modifiedAt = try c.decodeIfPresent(Date.self, forKey: .modifiedAt) ?? Date()
    }
}

// MARK: - Utilidades de dB

public enum Decibels {
    public static let silence: Double = -60

    public static func toLinear(_ db: Double) -> Float {
        db <= silence ? 0 : Float(pow(10, db / 20))
    }

    public static func fromLinear(_ value: Float) -> Double {
        value <= 0 ? silence : max(silence, 20 * log10(Double(value)))
    }
}
