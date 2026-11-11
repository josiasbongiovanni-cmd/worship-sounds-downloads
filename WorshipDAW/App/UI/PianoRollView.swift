import SwiftUI
import WorshipDAWCore
#if os(macOS)
import AppKit
#endif

/// Editor MIDI (piano roll), al estilo del MIDI Editor de REAPER:
/// teclado a la izquierda, notas en la grilla, carril de velocidad abajo y cuantización.
struct PianoRollView: View {
    @Bindable var session: Session
    let clipID: UUID
    @Environment(\.dismiss) private var dismiss

    enum Tool: String, CaseIterable, Identifiable {
        case select, draw
        #if os(iOS)
        case hand
        #endif
        var id: String { rawValue }
        var label: String {
            switch self {
            case .select: "Seleccionar"
            case .draw: "Dibujar"
            #if os(iOS)
            case .hand: "Mover vista"
            #endif
            }
        }
        var symbol: String {
            switch self {
            case .select: "cursorarrow"
            case .draw: "pencil"
            #if os(iOS)
            case .hand: "hand.draw"
            #endif
            }
        }
    }

    /// Gesto en curso (para mostrar la vista previa antes de confirmar).
    enum DragAction {
        case move(ids: Set<UUID>, dt: TimeInterval, semitones: Int)
        case resize(ids: Set<UUID>, dt: TimeInterval)
        case draw(pitch: Int, start: TimeInterval, end: TimeInterval)
        case marquee(CGRect)
    }

    static let rowH: CGFloat = 14
    static let keyW: CGFloat = 64
    static let velocityH: CGFloat = 86
    static let rulerH: CGFloat = 22

    @State private var selection: Set<UUID> = []
    @State private var tool: Tool
    @State private var grid: MIDIGrid = .sixteenth
    @State private var strength: Double = 1
    @State private var quantizeLengths = false
    @State private var pps: Double = 140
    @State private var visibleRect: CGRect = .zero
    @State private var scrollPosition: ScrollPosition
    @State private var action: DragAction?
    @State private var lastTap: (id: UUID, time: Date)?
    @State private var velocityDragging = false
    @FocusState private var focused: Bool

    init(session: Session, clipID: UUID) {
        _session = Bindable(wrappedValue: session)
        self.clipID = clipID
        #if os(iOS)
        _tool = State(initialValue: .hand)
        #else
        _tool = State(initialValue: .select)
        #endif
        // Centrar verticalmente en las notas del ítem (o en Do4).
        let center = session.midiClip(clipID).map { ($0.clip.pitchRange.lowerBound + $0.clip.pitchRange.upperBound) / 2 } ?? 60
        let y = max(0, CGFloat(127 - center) * Self.rowH - 220)
        _scrollPosition = State(initialValue: ScrollPosition(point: CGPoint(x: 0, y: y)))
    }

    private var located: (track: Track, clip: MIDIClip)? { session.midiClip(clipID) }
    private var gridSeconds: TimeInterval { grid.seconds(in: session.project.timeline) }

    var body: some View {
        VStack(spacing: 0) {
            if let located {
                toolbar(located)
                editor(located)
            } else {
                ContentUnavailableView("El ítem MIDI ya no existe", systemImage: "pianokeys")
                Button("Cerrar") { dismiss() }.padding()
            }
        }
        .background(Theme.background)
        .preferredColorScheme(.dark)
        .macSheetFrame(width: 1100, height: 700)
        .focusable()
        .focusEffectDisabled()
        .focused($focused)
        .onAppear { focused = true }
        .onKeyPress(phases: .down) { handleKey($0) }
    }

    // MARK: - Barra de herramientas

    private func toolbar(_ located: (track: Track, clip: MIDIClip)) -> some View {
        HStack(spacing: 10) {
            VStack(alignment: .leading, spacing: 1) {
                Text(located.clip.name).font(Theme.display(14, weight: .bold)).lineLimit(1)
                Text("\(located.track.instrument?.name ?? located.track.name) · \(located.clip.notes.count) notas")
                    .font(.system(size: 11)).foregroundStyle(Theme.textSecondary).lineLimit(1)
            }
            .frame(minWidth: 120, alignment: .leading)

            Picker("Herramienta", selection: $tool) {
                ForEach(Tool.allCases) { t in Label(t.label, systemImage: t.symbol).tag(t) }
            }
            .pickerStyle(.segmented)
            .labelsHidden()
            .fixedSize()

            Menu {
                Picker("Grilla", selection: $grid) {
                    ForEach(MIDIGrid.allCases) { Text($0.rawValue).tag($0) }
                }
            } label: {
                Label("Grilla \(grid.rawValue)", systemImage: "squareshape.split.3x3")
            }
            .fixedSize()

            Menu {
                Button("Cuantizar (Q)") { quantize() }
                Divider()
                Picker("Intensidad", selection: $strength) {
                    Text("100 %").tag(1.0)
                    Text("75 %").tag(0.75)
                    Text("50 %").tag(0.5)
                    Text("25 %").tag(0.25)
                }
                Toggle("Cuantizar también la duración", isOn: $quantizeLengths)
            } label: {
                Label("Cuantizar", systemImage: "metronome")
            } primaryAction: {
                quantize()
            }
            .fixedSize()
            .help("Cuantiza las notas seleccionadas (o todas) a la grilla. Atajo: Q")

            Menu {
                Button("Subir un semitono (↑)") { transpose(1) }
                Button("Bajar un semitono (↓)") { transpose(-1) }
                Button("Subir una octava (⇧↑)") { transpose(12) }
                Button("Bajar una octava (⇧↓)") { transpose(-12) }
                Divider()
                Button("Legato") { applyToTargets { c, ids in c.legato(ids) } }
                Button("Duplicar notas (⌘D)") { duplicate() }
                Divider()
                Button("Seleccionar todas") { selection = Set(located.clip.notes.map(\.id)) }
                Button("Quitar selección (Esc)") { selection.removeAll() }
                Divider()
                Button("Eliminar seleccionadas (⌫)", role: .destructive) { deleteSelected() }
            } label: {
                Label("Editar", systemImage: "slider.horizontal.3")
            }
            .fixedSize()

            Spacer(minLength: 0)

            Button { pps = max(20, pps / 1.4) } label: { Image(systemName: "minus.magnifyingglass") }
                .buttonStyle(.borderless)
            Button { pps = min(1200, pps * 1.4) } label: { Image(systemName: "plus.magnifyingglass") }
                .buttonStyle(.borderless)
            Button {
                if !session.isPlaying { session.seek(to: located.clip.start) }
                session.togglePlay()
            } label: {
                Image(systemName: session.isPlaying ? "stop.fill" : "play.fill")
                    .frame(width: 18)
            }
            .buttonStyle(.bordered)
            .help("Reproducir desde el inicio del ítem (Espacio)")
            Button("Listo") {
                session.editingMIDIClipID = nil
                dismiss()
            }
            .buttonStyle(.borderedProminent)
        }
        .font(.system(size: 12, weight: .medium))
        .padding(.horizontal, 12)
        .padding(.vertical, 9)
        .background(Theme.panel)
        .overlay(alignment: .bottom) { Rectangle().fill(Theme.stroke).frame(height: 1) }
    }

    // MARK: - Editor

    private func editor(_ located: (track: Track, clip: MIDIClip)) -> some View {
        let clip = located.clip
        let contentWidth = CGFloat((clip.duration + session.project.timeline.secondsPerBar * 2) * pps)
        let contentHeight = CGFloat(128) * Self.rowH
        return VStack(spacing: 0) {
            HStack(spacing: 0) {
                Color.clear.frame(width: Self.keyW, height: Self.rulerH)
                ruler(clip)
                    .frame(height: Self.rulerH)
                    .frame(maxWidth: .infinity)
                    .clipped()
            }
            .background(Theme.panel)
            HStack(spacing: 0) {
                keyboard(trackID: located.track.id)
                    .frame(width: Self.keyW)
                    .frame(maxHeight: .infinity, alignment: .top)
                    .clipped()
                ScrollView([.horizontal, .vertical]) {
                    ZStack(alignment: .topLeading) {
                        Color.clear.frame(width: contentWidth, height: contentHeight)
                        gridAndNotes(clip, trackColor: located.track.color.color)
                            .frame(width: max(1, visibleRect.width), height: max(1, visibleRect.height))
                            .offset(x: visibleRect.minX, y: visibleRect.minY)
                            .allowsHitTesting(false)
                    }
                    .frame(width: contentWidth, height: contentHeight, alignment: .topLeading)
                    .contentShape(Rectangle())
                    .modifier(EditGestures(enabled: isEditTool, onDrag: { dragChanged($0, clip: clip, trackID: located.track.id) },
                                           onEnd: { dragEnded($0, clip: clip, trackID: located.track.id) },
                                           onTap: { tap(at: $0, clip: clip, trackID: located.track.id) }))
                }
                .scrollPosition($scrollPosition)
                .onScrollGeometryChange(for: CGRect.self) { $0.visibleRect } action: { _, r in visibleRect = r }
            }
            velocityLane(clip)
                .frame(height: Self.velocityH)
        }
    }

    private var isEditTool: Bool {
        #if os(iOS)
        tool != .hand
        #else
        true
        #endif
    }

    /// Notas como se ven ahora (incluye la vista previa del gesto en curso).
    private func displayedNotes(_ clip: MIDIClip) -> [MIDINote] {
        var notes = clip.notes
        switch action {
        case .move(let ids, let dt, let semis):
            for i in notes.indices where ids.contains(notes[i].id) {
                notes[i].start = max(0, notes[i].start + dt)
                notes[i].pitch = max(0, min(127, notes[i].pitch + semis))
            }
        case .resize(let ids, let dt):
            for i in notes.indices where ids.contains(notes[i].id) {
                notes[i].duration = max(minimumLength, notes[i].duration + dt)
            }
        case .draw(let pitch, let start, let end):
            notes.append(MIDINote(id: UUID(), pitch: pitch, velocity: 100, start: start, duration: end - start))
        default:
            break
        }
        return notes
    }

    private var minimumLength: TimeInterval { max(0.01, gridSeconds / 4) }

    private func gridAndNotes(_ clip: MIDIClip, trackColor: Color) -> some View {
        let notes = displayedNotes(clip)
        let rect = visibleRect
        let timeline = session.project.timeline
        let pps = self.pps
        let grid = gridSeconds
        let selection = self.selection
        let action = self.action
        let playheadX = CGFloat((session.playhead - clip.start) * pps)
        return Canvas { ctx, size in
            ctx.translateBy(x: -rect.minX, y: -rect.minY)
            // Filas: teclas negras más oscuras, línea en cada Do
            let firstRow = max(0, Int(rect.minY / Self.rowH))
            let lastRow = min(127, Int(rect.maxY / Self.rowH) + 1)
            if firstRow <= lastRow {
                for row in firstRow...lastRow {
                    let pitch = 127 - row
                    let y = CGFloat(row) * Self.rowH
                    let r = CGRect(x: rect.minX, y: y, width: rect.width, height: Self.rowH)
                    ctx.fill(Path(r), with: .color(MIDINote.isBlackKey(pitch) ? Theme.lane : Theme.laneAlt))
                    if pitch % 12 == 0 {
                        ctx.fill(Path(CGRect(x: rect.minX, y: y + Self.rowH - 1, width: rect.width, height: 1)),
                                 with: .color(Color.white.opacity(0.12)))
                    }
                }
            }
            // Fin del ítem
            let endX = CGFloat(clip.duration * pps)
            if endX < rect.maxX {
                ctx.fill(Path(CGRect(x: endX, y: rect.minY, width: rect.maxX - endX, height: rect.height)),
                         with: .color(Color.black.opacity(0.35)))
            }
            // Grilla alineada al proyecto (el ítem puede empezar fuera de la grilla)
            let cell = max(0.001, grid)
            let firstCell = Int(floor((clip.start + Double(rect.minX) / pps) / cell))
            let lastCell = Int(ceil((clip.start + Double(rect.maxX) / pps) / cell))
            if lastCell >= firstCell, (lastCell - firstCell) < 4000 {
                for k in firstCell...lastCell {
                    let absolute = Double(k) * cell
                    let x = CGFloat((absolute - clip.start) * pps)
                    let beats = timeline.beats(at: absolute)
                    let isBar = abs(beats / Double(timeline.beatsPerBar) - (beats / Double(timeline.beatsPerBar)).rounded()) < 1e-6
                    let isBeat = abs(beats - beats.rounded()) < 1e-6
                    let alpha = isBar ? 0.16 : (isBeat ? 0.08 : 0.035)
                    ctx.fill(Path(CGRect(x: x, y: rect.minY, width: 1, height: rect.height)), with: .color(Color.white.opacity(alpha)))
                }
            }
            // Notas
            for n in notes {
                let r = CGRect(x: CGFloat(n.start * pps), y: CGFloat(127 - n.pitch) * Self.rowH + 1,
                               width: max(4, CGFloat(n.duration * pps)), height: Self.rowH - 2)
                guard r.intersects(rect) else { continue }
                let selected = selection.contains(n.id)
                let alpha = 0.45 + 0.55 * Double(n.velocity) / 127
                ctx.fill(Path(roundedRect: r, cornerRadius: 3), with: .color(trackColor.opacity(alpha)))
                if selected {
                    ctx.stroke(Path(roundedRect: r, cornerRadius: 3), with: .color(.white), lineWidth: 1.5)
                }
                if r.width > 34 {
                    ctx.draw(Text(MIDINote.name(for: n.pitch)).font(.system(size: 8, weight: .bold)).foregroundStyle(Color.black.opacity(0.7)),
                             at: CGPoint(x: r.minX + 3, y: r.midY), anchor: .leading)
                }
            }
            // Selección por rectángulo
            if case .marquee(let m) = action {
                ctx.fill(Path(m), with: .color(Theme.accent2.opacity(0.12)))
                ctx.stroke(Path(m), with: .color(Theme.accent2), lineWidth: 1)
            }
            // Cursor de reproducción
            if playheadX >= rect.minX && playheadX <= rect.maxX {
                ctx.fill(Path(CGRect(x: playheadX, y: rect.minY, width: 1.5, height: rect.height)), with: .color(.white))
            }
        }
    }

    private func ruler(_ clip: MIDIClip) -> some View {
        let timeline = session.project.timeline
        let pps = self.pps
        let offset = visibleRect.minX
        return Canvas { ctx, size in
            let barSec = timeline.secondsPerBar
            let first = Int(floor((clip.start + Double(offset) / pps) / barSec))
            let last = Int(ceil((clip.start + Double(offset + size.width) / pps) / barSec))
            guard last >= first else { return }
            for bar in max(0, first)...max(0, last) {
                let x = CGFloat((Double(bar) * barSec - clip.start) * pps) - offset
                ctx.fill(Path(CGRect(x: x, y: 8, width: 1, height: size.height - 8)), with: .color(Color.white.opacity(0.35)))
                ctx.draw(Text("\(bar + 1)").font(.system(size: 10, weight: .semibold, design: .rounded)).foregroundStyle(Theme.textSecondary),
                         at: CGPoint(x: x + 4, y: 11), anchor: .leading)
            }
        }
    }

    private func keyboard(trackID: UUID) -> some View {
        VStack(spacing: 0) {
            ForEach(0..<128, id: \.self) { row in
                let pitch = 127 - row
                let black = MIDINote.isBlackKey(pitch)
                Button {
                    session.audio.previewNote(trackID: trackID, pitch: pitch)
                } label: {
                    HStack {
                        Spacer()
                        if pitch % 12 == 0 {
                            Text(MIDINote.name(for: pitch))
                                .font(.system(size: 8, weight: .bold))
                                .foregroundStyle(Color.black.opacity(0.6))
                                .padding(.trailing, 3)
                        }
                    }
                    .frame(height: Self.rowH)
                    .background(black ? Color(white: 0.16) : Color(white: 0.88))
                    .overlay(alignment: .bottom) { Rectangle().fill(Color.black.opacity(0.25)).frame(height: 0.5) }
                }
                .buttonStyle(.plain)
            }
        }
        .offset(y: -visibleRect.minY)
    }

    private func velocityLane(_ clip: MIDIClip) -> some View {
        let notes = displayedNotes(clip)
        let pps = self.pps
        let offset = visibleRect.minX
        let selection = self.selection
        let tint = located?.track.color.color ?? Theme.accent
        return HStack(spacing: 0) {
            Text("Velocidad")
                .font(.system(size: 9, weight: .bold))
                .foregroundStyle(Theme.textTertiary)
                .frame(width: Self.keyW)
            Canvas { ctx, size in
                for n in notes {
                    let x = CGFloat(n.start * pps) - offset
                    guard x > -6, x < size.width + 6 else { continue }
                    let h = (size.height - 8) * CGFloat(n.velocity) / 127
                    let sel = selection.contains(n.id)
                    ctx.fill(Path(CGRect(x: x, y: size.height - h, width: 3, height: h)),
                             with: .color(sel ? Color.white : tint.opacity(0.8)))
                    ctx.fill(Path(ellipseIn: CGRect(x: x - 2, y: size.height - h - 3, width: 7, height: 7)),
                             with: .color(sel ? Color.white : tint))
                }
            }
            .contentShape(Rectangle())
            .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { g in
                        if !velocityDragging {
                            velocityDragging = true
                            session.beginGesture()
                        }
                        setVelocity(at: g.location, laneHeight: Self.velocityH, clip: clip)
                    }
                    .onEnded { _ in velocityDragging = false }
            )
        }
        .background(Theme.panel)
        .overlay(alignment: .top) { Rectangle().fill(Theme.stroke).frame(height: 1) }
    }

    // MARK: - Gestos

    private func row(at y: CGFloat) -> Int { 127 - max(0, min(127, Int(y / Self.rowH))) }

    private func time(at x: CGFloat) -> TimeInterval { max(0, Double(x) / pps) }

    /// Ajusta un tiempo relativo al ítem a la grilla del proyecto.
    private func snapped(_ t: TimeInterval, clip: MIDIClip, mode: (Double) -> Double = { $0.rounded() }) -> TimeInterval {
        let g = gridSeconds
        guard g > 0 else { return t }
        return max(0, mode((clip.start + t) / g) * g - clip.start)
    }

    private func hit(_ p: CGPoint, clip: MIDIClip) -> (note: MIDINote, edge: Bool)? {
        for n in clip.notes.reversed() {
            let r = CGRect(x: CGFloat(n.start * pps), y: CGFloat(127 - n.pitch) * Self.rowH,
                           width: max(4, CGFloat(n.duration * pps)), height: Self.rowH)
            if r.contains(p) {
                return (n, p.x > r.maxX - min(8, r.width / 3))
            }
        }
        return nil
    }

    private var shiftHeld: Bool {
        #if os(macOS)
        NSEvent.modifierFlags.contains(.shift)
        #else
        false
        #endif
    }

    private func dragChanged(_ g: DragGesture.Value, clip: MIDIClip, trackID: UUID) {
        if action == nil {
            // Decidir qué hace el gesto según dónde empezó.
            if let h = hit(g.startLocation, clip: clip) {
                if !selection.contains(h.note.id) {
                    selection = shiftHeld ? selection.union([h.note.id]) : [h.note.id]
                }
                action = h.edge ? .resize(ids: selection, dt: 0) : .move(ids: selection, dt: 0, semitones: 0)
                if !h.edge { session.audio.previewNote(trackID: trackID, pitch: h.note.pitch, velocity: h.note.velocity) }
            } else if tool == .draw {
                let start = snapped(time(at: g.startLocation.x), clip: clip, mode: { $0.rounded(.down) })
                action = .draw(pitch: row(at: g.startLocation.y), start: start, end: start + gridSeconds)
                session.audio.previewNote(trackID: trackID, pitch: row(at: g.startLocation.y))
            } else {
                action = .marquee(.zero)
            }
        }
        switch action {
        case .move(let ids, _, _):
            guard let anchor = clip.notes.first(where: { ids.contains($0.id) }) else { return }
            let raw = anchor.start + Double(g.translation.width) / pps
            let dt = snapped(raw, clip: clip) - anchor.start
            let semis = -Int((g.translation.height / Self.rowH).rounded())
            action = .move(ids: ids, dt: dt, semitones: semis)
        case .resize(let ids, _):
            guard let anchor = clip.notes.first(where: { ids.contains($0.id) }) else { return }
            let newEnd = max(anchor.start + minimumLength, snapped(anchor.end + Double(g.translation.width) / pps, clip: clip))
            action = .resize(ids: ids, dt: newEnd - anchor.end)
        case .draw(let pitch, let start, _):
            let end = max(start + gridSeconds, snapped(time(at: g.location.x), clip: clip, mode: { $0.rounded(.up) }))
            action = .draw(pitch: pitch, start: start, end: end)
        case .marquee:
            let a = g.startLocation, b = g.location
            action = .marquee(CGRect(x: min(a.x, b.x), y: min(a.y, b.y), width: abs(a.x - b.x), height: abs(a.y - b.y)))
        case nil:
            break
        }
    }

    private func dragEnded(_ g: DragGesture.Value, clip: MIDIClip, trackID: UUID) {
        let moved = abs(g.translation.width) > 3 || abs(g.translation.height) > 3
        let current = action
        action = nil
        guard moved else {
            tap(at: g.startLocation, clip: clip, trackID: trackID)
            return
        }
        switch current {
        case .move(let ids, let dt, let semis):
            if dt != 0 || semis != 0 { session.updateMIDIClip(clipID) { $0.move(ids, by: dt, semitones: semis) } }
        case .resize(let ids, let dt):
            if dt != 0 { session.updateMIDIClip(clipID) { $0.resize(ids, by: dt, minimum: minimumLength) } }
        case .draw(let pitch, let start, let end):
            var newID: UUID?
            session.updateMIDIClip(clipID) { newID = $0.addNote(pitch: pitch, at: start, length: end - start) }
            if let newID { selection = [newID] }
        case .marquee(let rect):
            let hits = clip.notes.filter {
                CGRect(x: CGFloat($0.start * pps), y: CGFloat(127 - $0.pitch) * Self.rowH,
                       width: max(4, CGFloat($0.duration * pps)), height: Self.rowH).intersects(rect)
            }.map(\.id)
            selection = shiftHeld ? selection.union(hits) : Set(hits)
        case nil:
            break
        }
    }

    /// Clic: seleccionar nota; doble clic en una nota la borra (REAPER); clic en vacío dibuja (lápiz) o deselecciona.
    private func tap(at p: CGPoint, clip: MIDIClip, trackID: UUID) {
        if let h = hit(p, clip: clip) {
            if let last = lastTap, last.id == h.note.id, Date().timeIntervalSince(last.time) < 0.35 {
                session.updateMIDIClip(clipID) { $0.removeNotes([h.note.id]) }
                selection.remove(h.note.id)
                lastTap = nil
                return
            }
            lastTap = (h.note.id, Date())
            if shiftHeld {
                if selection.contains(h.note.id) { selection.remove(h.note.id) } else { selection.insert(h.note.id) }
            } else {
                selection = [h.note.id]
            }
            session.audio.previewNote(trackID: trackID, pitch: h.note.pitch, velocity: h.note.velocity)
        } else if tool == .draw {
            let start = snapped(time(at: p.x), clip: clip, mode: { $0.rounded(.down) })
            let pitch = row(at: p.y)
            var newID: UUID?
            session.updateMIDIClip(clipID) { newID = $0.addNote(pitch: pitch, at: start, length: gridSeconds) }
            if let newID { selection = [newID] }
            session.audio.previewNote(trackID: trackID, pitch: pitch)
        } else {
            selection.removeAll()
        }
    }

    private func setVelocity(at p: CGPoint, laneHeight: CGFloat, clip: MIDIClip) {
        let x = Double(p.x + visibleRect.minX) / pps
        let value = Int(((1 - (p.y - 8) / (laneHeight - 8)) * 127).rounded())
        // Notas bajo el cursor (las seleccionadas tienen prioridad); si no hay, las seleccionadas.
        let tolerance = 6 / pps
        var ids = Set(clip.notes.filter { abs($0.start - x) < tolerance }.map(\.id))
        if !selection.isEmpty, !ids.isDisjoint(with: selection) { ids = selection }
        guard !ids.isEmpty else { return }
        session.updateMIDIClip(clipID, undoable: false) { $0.setVelocity(ids, to: value) }
    }

    // MARK: - Comandos

    private var targetIDs: Set<UUID>? { selection.isEmpty ? nil : selection }

    private func applyToTargets(_ change: @escaping (inout MIDIClip, Set<UUID>?) -> Void) {
        let ids = targetIDs
        session.updateMIDIClip(clipID) { change(&$0, ids) }
    }

    private func quantize() {
        let g = gridSeconds, s = strength, lengths = quantizeLengths
        applyToTargets { c, ids in c.quantize(ids, grid: g, strength: s, lengths: lengths) }
    }

    private func transpose(_ semis: Int) {
        guard !selection.isEmpty else { return }
        let ids = selection
        session.updateMIDIClip(clipID) { $0.transpose(ids, semitones: semis) }
        if let trackID = located?.track.id, let first = located?.clip.notes.first(where: { ids.contains($0.id) }) {
            session.audio.previewNote(trackID: trackID, pitch: first.pitch)
        }
    }

    private func nudge(_ direction: Double) {
        guard !selection.isEmpty else { return }
        let ids = selection, dt = gridSeconds * direction
        session.updateMIDIClip(clipID) { $0.move(ids, by: dt, semitones: 0) }
    }

    private func duplicate() {
        guard !selection.isEmpty else { return }
        let ids = selection
        var created = Set<UUID>()
        session.updateMIDIClip(clipID) { created = $0.duplicateNotes(ids) }
        selection = created
    }

    private func deleteSelected() {
        guard !selection.isEmpty else { return }
        let ids = selection
        session.updateMIDIClip(clipID) { $0.removeNotes(ids) }
        selection.removeAll()
    }

    private func handleKey(_ press: KeyPress) -> KeyPress.Result {
        let shift = press.modifiers.contains(.shift)
        if press.modifiers.contains(.command) {
            switch press.characters.lowercased() {
            case "a": selection = Set(located?.clip.notes.map(\.id) ?? [])
            case "d": duplicate()
            default: return .ignored
            }
            return .handled
        }
        switch press.key {
        case .space:
            if !session.isPlaying, let start = located?.clip.start { session.seek(to: start) }
            session.togglePlay()
        case .delete, .deleteForward: deleteSelected()
        case .escape: selection.removeAll()
        case .upArrow: transpose(shift ? 12 : 1)
        case .downArrow: transpose(shift ? -12 : -1)
        case .leftArrow: nudge(-1)
        case .rightArrow: nudge(1)
        default:
            switch press.characters.lowercased() {
            case "q": quantize()
            case "d": tool = .draw
            case "s": tool = .select
            case "+", "=": pps = min(1200, pps * 1.4)
            case "-": pps = max(20, pps / 1.4)
            default: return .ignored
            }
        }
        return .handled
    }
}

/// En Mac: un solo gesto (clic y arrastre). En iPhone: toque + arrastre solo con las herramientas de edición,
/// para que con "Mover vista" el dedo desplace la grilla.
private struct EditGestures: ViewModifier {
    let enabled: Bool
    let onDrag: (DragGesture.Value) -> Void
    let onEnd: (DragGesture.Value) -> Void
    let onTap: (CGPoint) -> Void

    func body(content: Content) -> some View {
        #if os(macOS)
        content.gesture(
            DragGesture(minimumDistance: 0)
                .onChanged(onDrag)
                .onEnded(onEnd)
        )
        #else
        content
            .onTapGesture(coordinateSpace: .local) { onTap($0) }
            .gesture(enabled ? DragGesture(minimumDistance: 4).onChanged(onDrag).onEnded(onEnd) : nil)
        #endif
    }
}
