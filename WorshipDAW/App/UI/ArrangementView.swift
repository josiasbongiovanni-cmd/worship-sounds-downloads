import SwiftUI
import UniformTypeIdentifiers
import WorshipDAWCore

/// Vista principal de arreglo: cabeceras de pista + línea de tiempo con secciones, regla y clips.
struct ArrangementView: View {
    @Bindable var session: Session
    var headerWidth: CGFloat = 230
    var trackHeight: CGFloat = 76
    var compact = false
    var onShowTrackInspector: (UUID) -> Void = { _ in }

    static let sectionBarHeight: CGFloat = 26
    static let rulerHeight: CGFloat = 26

    @State private var visibleRect: CGRect = .zero
    @State private var scrollPosition = ScrollPosition(point: .zero)
    @State private var pinchBase: Double?

    private var pps: Double { session.pixelsPerSecond }
    private var contentWidth: CGFloat { CGFloat(session.timelineLength * pps) }
    private var contentHeight: CGFloat {
        CGFloat(session.project.tracks.count) * trackHeight + 140
    }

    var body: some View {
        VStack(spacing: 0) {
            HStack(spacing: 0) {
                cornerControls
                    .frame(width: headerWidth, height: Self.sectionBarHeight + Self.rulerHeight)
                    .background(Theme.panel)
                    .overlay(alignment: .trailing) { Rectangle().fill(Theme.stroke).frame(width: 1) }
                TimelineHeader(session: session, offsetX: visibleRect.minX, width: visibleRect.width)
                    .frame(height: Self.sectionBarHeight + Self.rulerHeight)
                    .frame(maxWidth: .infinity)
                    .clipped()
            }
            .overlay(alignment: .bottom) { Rectangle().fill(Theme.stroke).frame(height: 1) }

            HStack(spacing: 0) {
                trackHeaders
                    .frame(width: headerWidth)
                    .frame(maxHeight: .infinity, alignment: .top)
                    .clipped()
                    .background(Theme.panel)
                    .overlay(alignment: .trailing) { Rectangle().fill(Theme.stroke).frame(width: 1) }
                lanesScroll
            }
        }
        .background(Theme.background)
    }

    // MARK: Esquina

    private var cornerControls: some View {
        HStack(spacing: 6) {
            Menu {
                Button { session.addTrack() } label: { Label("Pista de audio", systemImage: "waveform") }
                Button { session.addTrack(role: .guide) } label: { Label("Pista guía", systemImage: "person.wave.2") }
            } label: {
                Label("Pista", systemImage: "plus")
                    .font(.system(size: 12, weight: .semibold))
            }
            .menuStyle(.button)
            .buttonStyle(.bordered)
            .controlSize(.small)
            .fixedSize()
            Spacer(minLength: 0)
            if !compact {
                Button { session.snapEnabled.toggle() } label: {
                    Image(systemName: "squareshape.split.3x3")
                        .foregroundStyle(session.snapEnabled ? Theme.accent : Theme.textTertiary)
                }
                .buttonStyle(.plain)
                .help("Ajustar a la grilla")
                Button { session.followPlayhead.toggle() } label: {
                    Image(systemName: "arrow.right.to.line")
                        .foregroundStyle(session.followPlayhead ? Theme.accent : Theme.textTertiary)
                }
                .buttonStyle(.plain)
                .help("Seguir el cursor")
            }
            Button { session.zoom(by: 1 / 1.4) } label: { Image(systemName: "minus.magnifyingglass") }
                .buttonStyle(.plain)
            Button { session.zoom(by: 1.4) } label: { Image(systemName: "plus.magnifyingglass") }
                .buttonStyle(.plain)
        }
        .font(.system(size: 13))
        .foregroundStyle(Theme.textSecondary)
        .padding(.horizontal, 10)
    }

    // MARK: Cabeceras

    private var trackHeaders: some View {
        VStack(spacing: 0) {
            ForEach(session.project.tracks) { track in
                TrackHeaderView(session: session, track: track, compact: compact,
                                onShowInspector: { onShowTrackInspector(track.id) })
                    .frame(height: trackHeight)
            }
            Spacer(minLength: 0)
        }
        .offset(y: -visibleRect.minY)
    }

    // MARK: Carriles

    private var lanesScroll: some View {
        ScrollView([.horizontal, .vertical]) {
            ZStack(alignment: .topLeading) {
                Color.clear.frame(width: contentWidth, height: contentHeight)

                GridCanvas(timeline: session.project.timeline, pps: pps, offsetX: visibleRect.minX,
                           trackCount: session.project.tracks.count, trackHeight: trackHeight,
                           sections: session.project.sections)
                    .frame(width: max(1, visibleRect.width), height: contentHeight)
                    .offset(x: visibleRect.minX)
                    .allowsHitTesting(false)

                if session.project.loop.isEnabled {
                    Rectangle()
                        .fill(Theme.loop.opacity(0.06))
                        .frame(width: CGFloat(session.project.loop.length * pps), height: contentHeight)
                        .offset(x: CGFloat(session.project.loop.start * pps))
                        .allowsHitTesting(false)
                }

                VStack(spacing: 0) {
                    ForEach(Array(session.project.tracks.enumerated()), id: \.element.id) { index, track in
                        TrackLaneView(session: session, track: track, trackIndex: index, trackHeight: trackHeight,
                                      visibleStart: Double(visibleRect.minX) / pps,
                                      visibleEnd: Double(visibleRect.maxX) / pps)
                            .frame(width: contentWidth, height: trackHeight)
                    }
                }

                Rectangle()
                    .fill(Theme.playhead)
                    .frame(width: 1.5, height: contentHeight)
                    .shadow(color: Theme.accent.opacity(0.8), radius: 4)
                    .offset(x: CGFloat(session.playhead * pps))
                    .allowsHitTesting(false)
            }
            .frame(width: contentWidth, height: contentHeight, alignment: .topLeading)
            .contentShape(Rectangle())
            .dropDestination(for: URL.self) { urls, location in
                let time = session.snap(Double(location.x) / pps)
                let index = Int(location.y / trackHeight)
                let trackID = session.project.tracks.indices.contains(index) ? session.project.tracks[index].id : nil
                session.importAudio(urls: urls, to: urls.count == 1 ? trackID : nil, at: time)
                return true
            }
        }
        .scrollPosition($scrollPosition)
        .scrollIndicators(.visible)
        .onScrollGeometryChange(for: CGRect.self) { geo in
            geo.visibleRect
        } action: { _, rect in
            visibleRect = rect
        }
        .simultaneousGesture(
            MagnifyGesture()
                .onChanged { value in
                    if pinchBase == nil { pinchBase = session.pixelsPerSecond }
                    session.pixelsPerSecond = min(600, max(6, (pinchBase ?? 48) * value.magnification))
                }
                .onEnded { _ in pinchBase = nil }
        )
        .onChange(of: session.playhead) { _, playhead in
            followIfNeeded(playhead)
        }
    }

    private func followIfNeeded(_ playhead: TimeInterval) {
        guard session.isPlaying, session.followPlayhead, visibleRect.width > 0 else { return }
        let x = CGFloat(playhead * pps)
        if x > visibleRect.maxX - visibleRect.width * 0.1 || x < visibleRect.minX {
            scrollPosition.scrollTo(point: CGPoint(x: max(0, x - visibleRect.width * 0.15), y: visibleRect.minY))
        }
    }
}

// MARK: - Grilla (dibuja solo el rango visible)

struct GridCanvas: View {
    let timeline: Timeline
    let pps: Double
    let offsetX: CGFloat
    let trackCount: Int
    let trackHeight: CGFloat
    let sections: [SongSection]

    var body: some View {
        Canvas { ctx, size in
            // Bandas de carril alternadas
            for i in 0..<trackCount {
                let rect = CGRect(x: 0, y: CGFloat(i) * trackHeight, width: size.width, height: trackHeight)
                ctx.fill(Path(rect), with: .color(i.isMultiple(of: 2) ? Theme.lane : Theme.laneAlt))
                ctx.fill(Path(CGRect(x: 0, y: rect.maxY - 1, width: size.width, height: 1)), with: .color(Theme.stroke))
            }
            // Tinte suave de cada sección
            for section in sections {
                let x0 = CGFloat(section.start * pps) - offsetX
                let x1 = CGFloat(section.end * pps) - offsetX
                guard x1 > 0, x0 < size.width else { continue }
                ctx.fill(Path(CGRect(x: x0, y: 0, width: x1 - x0, height: size.height)),
                         with: .color(section.kind.color.opacity(0.025)))
                ctx.fill(Path(CGRect(x: x0, y: 0, width: 1, height: size.height)),
                         with: .color(section.kind.color.opacity(0.35)))
            }
            // Líneas de compás y tiempo
            let beatPx = timeline.secondsPerBeat * pps
            let showBeats = beatPx >= 14
            let firstBeat = Int(floor(Double(offsetX) / beatPx))
            let lastBeat = Int(ceil(Double(offsetX + size.width) / beatPx))
            guard lastBeat >= firstBeat else { return }
            for beat in max(0, firstBeat)...max(0, lastBeat) {
                let isBar = beat % timeline.beatsPerBar == 0
                if !isBar && !showBeats { continue }
                let x = CGFloat(Double(beat) * beatPx) - offsetX
                ctx.fill(Path(CGRect(x: x, y: 0, width: 1, height: size.height)),
                         with: .color(Color.white.opacity(isBar ? 0.075 : 0.03)))
            }
        }
    }
}

// MARK: - Encabezado de línea de tiempo: secciones + regla

struct TimelineHeader: View {
    @Bindable var session: Session
    let offsetX: CGFloat
    let width: CGFloat

    @State private var renamingSection: SongSection?
    @State private var renameText = ""

    private var pps: Double { session.pixelsPerSecond }

    var body: some View {
        VStack(spacing: 0) {
            sectionBar
                .frame(height: ArrangementView.sectionBarHeight)
            ruler
                .frame(height: ArrangementView.rulerHeight)
        }
        .background(Theme.panel)
        .alert("Renombrar sección", isPresented: Binding(
            get: { renamingSection != nil },
            set: { if !$0 { renamingSection = nil } })) {
            TextField("Nombre", text: $renameText)
            Button("Cancelar", role: .cancel) { renamingSection = nil }
            Button("Guardar") {
                if let id = renamingSection?.id {
                    session.edit { p in
                        if let i = p.sections.firstIndex(where: { $0.id == id }) { p.sections[i].name = renameText }
                    }
                }
                renamingSection = nil
            }
        }
    }

    private var sectionBar: some View {
        ZStack(alignment: .topLeading) {
            Color.clear
            ForEach(session.project.sections) { section in
                let x = CGFloat(section.start * pps) - offsetX
                let w = CGFloat(section.length * pps)
                if x + w > 0 && x < max(width, 1) + 200 {
                    sectionChip(section, width: w)
                        .offset(x: x)
                }
            }
        }
        .clipped()
    }

    private func sectionChip(_ section: SongSection, width: CGFloat) -> some View {
        let isCurrent = session.project.section(at: session.playhead)?.id == section.id
        return Button {
            session.seek(to: section.start)
        } label: {
            HStack(spacing: 4) {
                Text(width > 60 ? section.name : section.kind.shortName)
                    .font(.system(size: 11, weight: .bold, design: .rounded))
                    .lineLimit(1)
                Spacer(minLength: 0)
            }
            .padding(.horizontal, 8)
            .frame(width: max(2, width - 2), height: ArrangementView.sectionBarHeight - 6)
            .background(
                RoundedRectangle(cornerRadius: 6, style: .continuous)
                    .fill(section.kind.color.opacity(isCurrent ? 0.42 : 0.2))
            )
            .overlay(
                RoundedRectangle(cornerRadius: 6, style: .continuous)
                    .strokeBorder(section.kind.color.opacity(isCurrent ? 0.95 : 0.4), lineWidth: 1)
            )
            .foregroundStyle(isCurrent ? Color.white : section.kind.color)
        }
        .buttonStyle(.plain)
        .padding(.top, 3)
        .contextMenu {
            Button { session.loopSection(section.id) } label: { Label("Repetir esta sección", systemImage: "repeat") }
            Button {
                renameText = section.name
                renamingSection = section
            } label: { Label("Renombrar", systemImage: "pencil") }
            Menu("Tipo") {
                ForEach(SectionKind.allCases, id: \.self) { kind in
                    Button(kind.displayName) {
                        session.edit { p in
                            if let i = p.sections.firstIndex(where: { $0.id == section.id }) {
                                p.sections[i].kind = kind
                                p.sections[i].name = kind.displayName
                            }
                        }
                    }
                }
            }
            Menu("Largo") {
                ForEach([2, 4, 8, 12, 16], id: \.self) { bars in
                    Button("\(bars) compases") {
                        session.edit { p in
                            if let i = p.sections.firstIndex(where: { $0.id == section.id }) {
                                p.sections[i].length = p.timeline.seconds(atBar: Double(bars))
                            }
                        }
                    }
                }
            }
            Divider()
            Button(role: .destructive) { session.edit { $0.removeSection(section.id) } } label: {
                Label("Eliminar", systemImage: "trash")
            }
        }
    }

    private var ruler: some View {
        Canvas { ctx, size in
            let timeline = session.project.timeline
            let barPx = timeline.secondsPerBar * pps
            // Etiquetas cada N compases para que no se encimen.
            var labelEvery = 1
            while Double(labelEvery) * barPx < 44 { labelEvery *= 2 }

            let loop = session.project.loop
            if loop.length > 0.05 {
                let x0 = CGFloat(loop.start * pps) - offsetX
                let x1 = CGFloat(loop.end * pps) - offsetX
                let color = Theme.loop.opacity(loop.isEnabled ? 0.35 : 0.12)
                ctx.fill(Path(roundedRect: CGRect(x: x0, y: 3, width: x1 - x0, height: 6), cornerRadius: 3), with: .color(color))
            }

            let firstBar = max(0, Int(floor(Double(offsetX) / barPx)))
            let lastBar = Int(ceil(Double(offsetX + size.width) / barPx))
            if lastBar >= firstBar {
                for bar in firstBar...lastBar {
                    let x = CGFloat(Double(bar) * barPx) - offsetX
                    let labeled = bar % labelEvery == 0
                    ctx.fill(Path(CGRect(x: x, y: labeled ? 10 : 18, width: 1, height: labeled ? 16 : 8)),
                             with: .color(Color.white.opacity(labeled ? 0.35 : 0.15)))
                    if labeled {
                        ctx.draw(Text("\(bar + 1)").font(.system(size: 10, weight: .semibold, design: .rounded))
                                    .foregroundStyle(Theme.textSecondary),
                                 at: CGPoint(x: x + 4, y: 15), anchor: .leading)
                    }
                }
            }
            // Cursor
            let px = CGFloat(session.playhead * pps) - offsetX
            var tri = Path()
            tri.move(to: CGPoint(x: px - 6, y: size.height - 10))
            tri.addLine(to: CGPoint(x: px + 6, y: size.height - 10))
            tri.addLine(to: CGPoint(x: px, y: size.height))
            tri.closeSubpath()
            ctx.fill(tri, with: .color(Theme.playhead))
        }
        .contentShape(Rectangle())
        .gesture(
            DragGesture(minimumDistance: 0)
                .onChanged { g in
                    let t = Double(g.location.x + offsetX) / pps
                    session.playhead = session.snap(t)
                }
                .onEnded { g in
                    session.seek(to: session.snap(Double(g.location.x + offsetX) / pps))
                }
        )
    }
}
