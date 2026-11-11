import Foundation

/// Un proyecto en disco es una carpeta:
///
///     Mi Canción.worship/
///       project.json
///       Audio/  (archivos .caf convertidos al formato del proyecto)
///       Peaks/  (caché de formas de onda)
public struct ProjectStore: Sendable {
    public static let fileExtension = "worship"
    public static let projectFileName = "project.json"

    public let rootURL: URL

    public init(rootURL: URL) {
        self.rootURL = rootURL
    }

    public struct Entry: Identifiable, Hashable, Sendable {
        public var id: URL { url }
        public let url: URL
        public let name: String
        public let bpm: Double
        public let key: String
        public let trackCount: Int
        public let modifiedAt: Date
    }

    public static func audioDirectory(of projectURL: URL) -> URL {
        projectURL.appendingPathComponent("Audio", isDirectory: true)
    }

    public static func peaksDirectory(of projectURL: URL) -> URL {
        projectURL.appendingPathComponent("Peaks", isDirectory: true)
    }

    public func ensureRoot() throws {
        try FileManager.default.createDirectory(at: rootURL, withIntermediateDirectories: true)
    }

    public func list() throws -> [Entry] {
        try ensureRoot()
        let urls = try FileManager.default.contentsOfDirectory(at: rootURL, includingPropertiesForKeys: nil)
        return urls
            .filter { $0.pathExtension == Self.fileExtension }
            .compactMap { url in
                guard let project = try? Self.load(from: url) else { return nil }
                return Entry(url: url, name: project.name, bpm: project.bpm, key: project.key,
                             trackCount: project.tracks.count, modifiedAt: project.modifiedAt)
            }
            .sorted { $0.modifiedAt > $1.modifiedAt }
    }

    /// Crea la carpeta del proyecto con un nombre único y lo guarda.
    public func create(_ project: Project) throws -> URL {
        try ensureRoot()
        let base = Self.sanitize(project.name)
        var url = rootURL.appendingPathComponent(base).appendingPathExtension(Self.fileExtension)
        var n = 2
        while FileManager.default.fileExists(atPath: url.path) {
            url = rootURL.appendingPathComponent("\(base) \(n)").appendingPathExtension(Self.fileExtension)
            n += 1
        }
        try Self.save(project, to: url)
        return url
    }

    public func delete(_ url: URL) throws {
        try FileManager.default.removeItem(at: url)
    }

    public static func save(_ project: Project, to url: URL) throws {
        let fm = FileManager.default
        try fm.createDirectory(at: audioDirectory(of: url), withIntermediateDirectories: true)
        try fm.createDirectory(at: peaksDirectory(of: url), withIntermediateDirectories: true)
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        encoder.dateEncodingStrategy = .iso8601
        let data = try encoder.encode(project)
        try data.write(to: url.appendingPathComponent(projectFileName), options: .atomic)
    }

    public static func load(from url: URL) throws -> Project {
        let data = try Data(contentsOf: url.appendingPathComponent(projectFileName))
        let decoder = JSONDecoder()
        decoder.dateDecodingStrategy = .iso8601
        return try decoder.decode(Project.self, from: data)
    }

    static func sanitize(_ name: String) -> String {
        let invalid = CharacterSet(charactersIn: "/\\:?%*|\"<>")
        let cleaned = name.components(separatedBy: invalid).joined(separator: "-")
            .trimmingCharacters(in: .whitespacesAndNewlines)
        return cleaned.isEmpty ? "Sin título" : cleaned
    }
}
