import Foundation
import Observation
import WorshipDAWCore

/// Estado global de la app: biblioteca de proyectos y sesión abierta.
@Observable
@MainActor
final class AppModel {
    var session: Session?
    var entries: [ProjectStore.Entry] = []
    var errorMessage: String?

    let store: ProjectStore

    init() {
        let documents = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
        #if os(macOS)
        let root = documents.appendingPathComponent("Worship DAW", isDirectory: true)
        #else
        // En iOS la carpeta Documentos es visible en la app Archivos.
        let root = documents.appendingPathComponent("Proyectos", isDirectory: true)
        #endif
        store = ProjectStore(rootURL: root)
        refresh()
    }

    func refresh() {
        do {
            entries = try store.list()
        } catch {
            errorMessage = error.localizedDescription
        }
    }

    func create(name: String, template: ProjectTemplate, bpm: Double, key: String, beatsPerBar: Int) {
        let project = template.makeProject(name: name.isEmpty ? "Nueva canción" : name, bpm: bpm, key: key,
                                           beatsPerBar: beatsPerBar)
        do {
            let url = try store.create(project)
            open(url)
        } catch {
            errorMessage = error.localizedDescription
        }
    }

    func open(_ url: URL) {
        do {
            let project = try ProjectStore.load(from: url)
            session?.close()
            session = Session(project: project, url: url)
        } catch {
            errorMessage = "No se pudo abrir el proyecto: \(error.localizedDescription)"
        }
    }

    func closeSession() {
        session?.close()
        session = nil
        refresh()
    }

    func delete(_ entry: ProjectStore.Entry) {
        do {
            try store.delete(entry.url)
        } catch {
            errorMessage = error.localizedDescription
        }
        refresh()
    }

    func duplicate(_ entry: ProjectStore.Entry) {
        let fm = FileManager.default
        var dest = store.rootURL.appendingPathComponent("\(entry.name) copia").appendingPathExtension(ProjectStore.fileExtension)
        var n = 2
        while fm.fileExists(atPath: dest.path) {
            dest = store.rootURL.appendingPathComponent("\(entry.name) copia \(n)").appendingPathExtension(ProjectStore.fileExtension)
            n += 1
        }
        do {
            try fm.copyItem(at: entry.url, to: dest)
            var project = try ProjectStore.load(from: dest)
            project.id = UUID()
            project.name = dest.deletingPathExtension().lastPathComponent
            project.modifiedAt = Date()
            try ProjectStore.save(project, to: dest)
        } catch {
            errorMessage = error.localizedDescription
        }
        refresh()
    }
}
