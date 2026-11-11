import XCTest
@testable import WorshipDAWCore

final class TimelineTests: XCTestCase {
    func testBarBeatAt72BPM() {
        let t = Timeline(bpm: 72, beatsPerBar: 4)
        XCTAssertEqual(t.secondsPerBeat, 60.0 / 72.0, accuracy: 1e-9)
        XCTAssertEqual(t.barBeatString(at: 0), "1.1.00")
        XCTAssertEqual(t.barBeat(at: t.secondsPerBar).bar, 2)
        XCTAssertEqual(t.barBeat(at: t.secondsPerBeat * 6).beat, 3)
    }

    func testSnap() {
        let t = Timeline(bpm: 120, beatsPerBar: 4)
        XCTAssertEqual(t.snap(0.26), 0.5, accuracy: 1e-9)
        XCTAssertEqual(t.snap(0.24), 0.0, accuracy: 1e-9)
        XCTAssertEqual(t.snap(1.1, division: 4), 2.0, accuracy: 1e-9)
    }

    func testTimecode() {
        XCTAssertEqual(Timeline.timecode(83.5), "1:23.500")
    }
}

final class EditingTests: XCTestCase {
    private func projectWithClip() -> (Project, UUID, UUID) {
        var p = Project(name: "Test", bpm: 120)
        let track = p.addTrack(name: "Voz")
        let clip = Clip(name: "Toma 1", fileName: "a.caf", start: 2, offset: 0, duration: 10)
        p.addClip(clip, toTrack: track.id)
        return (p, track.id, clip.id)
    }

    func testSplit() {
        var (p, _, clipID) = projectWithClip()
        let rightID = p.splitClip(clipID, at: 5)
        XCTAssertNotNil(rightID)
        let clips = p.tracks[0].clips
        XCTAssertEqual(clips.count, 2)
        XCTAssertEqual(clips[0].duration, 3, accuracy: 1e-9)
        XCTAssertEqual(clips[1].start, 5, accuracy: 1e-9)
        XCTAssertEqual(clips[1].offset, 3, accuracy: 1e-9)
        XCTAssertEqual(clips[1].duration, 7, accuracy: 1e-9)
        XCTAssertNil(p.splitClip(clipID, at: 100))
    }

    func testTrimRespectsSource() {
        var (p, _, clipID) = projectWithClip()
        p.trimClipStart(clipID, to: 4)
        XCTAssertEqual(p.tracks[0].clips[0].offset, 2, accuracy: 1e-9)
        XCTAssertEqual(p.tracks[0].clips[0].duration, 8, accuracy: 1e-9)
        // No se puede extender antes del inicio del archivo
        p.trimClipStart(clipID, to: 0)
        XCTAssertEqual(p.tracks[0].clips[0].start, 2, accuracy: 1e-9)
        XCTAssertEqual(p.tracks[0].clips[0].offset, 0, accuracy: 1e-9)
        // Ni después del final del archivo
        p.trimClipEnd(clipID, to: 100)
        XCTAssertEqual(p.tracks[0].clips[0].end, 12, accuracy: 1e-9)
    }

    func testMoveBetweenTracks() {
        var (p, _, clipID) = projectWithClip()
        let other = p.addTrack(name: "Coros")
        p.moveClip(clipID, to: 8, trackID: other.id)
        XCTAssertTrue(p.tracks[0].clips.isEmpty)
        XCTAssertEqual(p.tracks[1].clips.first?.start, 8)
    }

    func testSoloLogic() {
        var p = Project(name: "Solo")
        p.addTrack(name: "A")
        p.addTrack(name: "B")
        XCTAssertTrue(p.isAudible(p.tracks[0]))
        p.tracks[1].isSolo = true
        XCTAssertFalse(p.isAudible(p.tracks[0]))
        XCTAssertTrue(p.isAudible(p.tracks[1]))
    }

    func testSectionsAndNavigation() {
        let p = ProjectTemplate.fullBand.makeProject(name: "Gracia", bpm: 60)
        // 60 BPM, 4/4 → 4 s por compás. Intro 4 compases = 16 s.
        XCTAssertEqual(p.sections.first?.kind, .intro)
        XCTAssertEqual(p.sections.first?.length ?? 0, 16, accuracy: 1e-9)
        XCTAssertEqual(p.section(at: 17)?.name, "Verso 1")
        XCTAssertEqual(p.nextSection(after: 0)?.name, "Verso 1")
        XCTAssertEqual(p.previousSection(before: 16.5)?.kind, .intro)
        XCTAssertEqual(p.previousSection(before: 20)?.name, "Verso 1")
    }

    func testMoveTracks() {
        var p = Project(name: "Orden")
        ["A", "B", "C", "D"].forEach { p.addTrack(name: $0) }
        p.moveTracks(from: IndexSet(integer: 0), to: 3)
        XCTAssertEqual(p.tracks.map(\.name), ["B", "C", "A", "D"])
    }
}

final class StoreTests: XCTestCase {
    func testRoundTrip() throws {
        let root = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        defer { try? FileManager.default.removeItem(at: root) }
        let store = ProjectStore(rootURL: root)
        let project = ProjectTemplate.multitracks.makeProject(name: "Way Maker / Live", bpm: 68, key: "E")
        let url = try store.create(project)
        XCTAssertEqual(url.pathExtension, "worship")
        XCTAssertFalse(url.lastPathComponent.contains("/"))
        let loaded = try ProjectStore.load(from: url)
        XCTAssertEqual(loaded.name, project.name)
        XCTAssertEqual(loaded.tracks.count, project.tracks.count)
        XCTAssertEqual(try store.list().count, 1)
        let second = try store.create(project)
        XCTAssertNotEqual(url, second)
    }
}

final class ReaperParityTests: XCTestCase {
    func testMarkersNumberingAndNavigation() {
        var p = ProjectTemplate.fullBand.makeProject(name: "M", bpm: 60)
        p.addMarker(at: 10)
        p.addMarker(at: 5)
        XCTAssertEqual(p.markers.map(\.number), [2, 1])
        p.removeMarker(p.marker(number: 1)!.id)
        XCTAssertEqual(p.addMarker(at: 30).number, 1)
        // Quedan los marcadores 2 (5 s) y 1 (30 s); las secciones empiezan en 0, 16, 48…
        XCTAssertEqual(p.marker(number: 1)?.time, 30)
        XCTAssertEqual(p.nextStop(after: 0), 5)
        XCTAssertEqual(p.nextStop(after: 5), 16)
        XCTAssertEqual(p.previousStop(before: 16), 5)
    }

    func testTrackInputChannelMap() {
        XCTAssertEqual(TrackInput.mono(2).channelMap, [2, 2])
        XCTAssertEqual(TrackInput.stereo(0).channelMap, [0, 1])
        XCTAssertNil(TrackInput.off.channelMap)
        var p = Project(name: "In")
        p.addTrack(name: "A"); p.addTrack(name: "B")
        p.tracks[0].isArmed = true; p.tracks[0].input = .mono(3)
        p.tracks[1].isArmed = true; p.tracks[1].input = .stereo(4)
        XCTAssertEqual(p.highestArmedInputChannel, 5)
    }

    func testDecodesProjectsSavedBeforeNewFields() throws {
        let json = """
        {"version":1,"id":"\(UUID().uuidString)","name":"Viejo","bpm":70,"beatsPerBar":4,"key":"D",
         "sampleRate":48000,"sections":[],"loop":{"start":0,"end":8,"isEnabled":false},
         "isClickEnabled":true,"clickVolumeDB":-6,"masterVolumeDB":0,
         "createdAt":"2026-11-11T10:00:00Z","modifiedAt":"2026-11-11T10:00:00Z",
         "tracks":[{"id":"\(UUID().uuidString)","name":"Voz","color":"violet","role":"audio","volumeDB":0,"pan":0,
           "isMuted":false,"isSolo":false,"isArmed":false,"eq":{"lowGain":0,"midGain":0,"highGain":0,"isEnabled":true},
           "inserts":[],"clips":[]}]}
        """
        let decoder = JSONDecoder()
        decoder.dateDecodingStrategy = .iso8601
        let p = try decoder.decode(Project.self, from: Data(json.utf8))
        XCTAssertEqual(p.name, "Viejo")
        XCTAssertTrue(p.markers.isEmpty)
        XCTAssertEqual(p.tracks.first?.input, .mono(0))
        XCTAssertEqual(p.tracks.first?.monitor, .off)
    }

    func testMonitorModes() {
        XCTAssertFalse(MonitorMode.on.isMonitoring(armed: false, playing: false, recording: false))
        XCTAssertTrue(MonitorMode.on.isMonitoring(armed: true, playing: true, recording: false))
        // Estilo cinta: al reproducir se oye lo grabado, al grabar o detenido se oye la entrada.
        XCTAssertFalse(MonitorMode.auto.isMonitoring(armed: true, playing: true, recording: false))
        XCTAssertTrue(MonitorMode.auto.isMonitoring(armed: true, playing: true, recording: true))
        XCTAssertTrue(MonitorMode.auto.isMonitoring(armed: true, playing: false, recording: false))
        XCTAssertFalse(MonitorMode.off.isMonitoring(armed: true, playing: false, recording: true))
    }

    func testRecordingAlignment() {
        // Grabación que arrancó 0,25 s después del inicio de la reproducción, con 12 ms de latencia,
        // reproduciendo desde -2 s (cuenta previa) y cursor en 0: el ítem empieza en 0.
        let a = RecordingAlignment.compute(playFrom: -2, editCursor: 0, captureDelay: 0.25, latency: 0.012)
        XCTAssertEqual(a.start, 0, accuracy: 1e-9)
        // El archivo empezó en -1,762 s de la línea de tiempo: hay que saltar 1,762 s del archivo.
        XCTAssertEqual(a.fileOffset, 1.762, accuracy: 1e-9)
        // Sin cuenta previa: el ítem empieza en el cursor menos la latencia compensada.
        let b = RecordingAlignment.compute(playFrom: 10, editCursor: 10, captureDelay: 0.05, latency: 0.02)
        XCTAssertEqual(b.start, 10.03, accuracy: 1e-9)
        XCTAssertEqual(b.fileOffset, 0, accuracy: 1e-9)
    }
}
