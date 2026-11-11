import SwiftUI
import WorshipDAWCore
#if os(macOS)
import AppKit
#endif

// MARK: - Cabecera de pista

struct TrackHeaderView: View {
    @Bindable var session: Session
    let track: Track
    var compact = false
    var onShowInspector: () -> Void = {}

    @State private var isRenaming = false
    @State private var renameText = ""

    private var isSelected: Bool { session.selectedTrackID == track.id }

    var body: some View {
        HStack(spacing: 0) {
            RoundedRectangle(cornerRadius: 2)
                .fill(track.color.color)
                .frame(width: 4)
                .padding(.vertical, 8)
                .shadow(color: track.color.color.opacity(0.7), radius: isSelected ? 6 : 0)
            VStack(alignment: .leading, spacing: compact ? 5 : 7) {
                HStack(spacing: 6) {
                    if track.role == .guide {
                        Image(systemName: "person.wave.2.fill")
                            .font(.system(size: 10))
                            .foregroundStyle(track.color.color)
                    }
                    Text(track.name)
                        .font(.system(size: compact ? 12 : 13, weight: .semibold, design: .rounded))
                        .foregroundStyle(isSelected ? Theme.textPrimary : Theme.textPrimary.opacity(0.82))
                        .lineLimit(1)
                    Spacer(minLength: 0)
                    if !track.inserts.isEmpty && !compact {
                        Text("FX \(track.inserts.count)")
                            .font(.system(size: 9, weight: .bold, design: .rounded))
                            .padding(.horizontal, 5)
                            .padding(.vertical, 2)
                            .background(Capsule().fill(Theme.accent.opacity(0.2)))
                            .foregroundStyle(Theme.accent)
                    }
                }
                HStack(spacing: 4) {
                    PillToggle(label: "M", isOn: track.isMuted, tint: Theme.mute, size: compact ? 22 : 22) {
                        session.updateTrack(track.id) { $0.isMuted.toggle() }
                    }
                    PillToggle(label: "S", isOn: track.isSolo, tint: Theme.solo, size: compact ? 22 : 22) {
                        session.updateTrack(track.id) { $0.isSolo.toggle() }
                    }
                    PillToggle(label: "R", isOn: track.isArmed, tint: Theme.record, size: compact ? 22 : 22) {
                        session.updateTrack(track.id) { $0.isArmed.toggle() }
                    }
                    if !compact {
                        Slider(value: Binding(
                            get: { Fader.position(for: track.volumeDB) },
                            set: { pos in
                                session.updateTrack(track.id, undoable: false) { $0.volumeDB = (Fader.db(for: pos) * 10).rounded() / 10 }
                            }), in: 0...1, onEditingChanged: { began in if began { session.beginGesture() } })
                            .controlSize(.mini)
                            .tint(track.color.color)
                            .padding(.leading, 4)
                        Text(formatDB(track.volumeDB))
                            .font(Theme.mono(9))
                            .foregroundStyle(Theme.textTertiary)
                            .frame(width: 34, alignment: .trailing)
                    }
                }
                TimelineView(.animation(minimumInterval: 1.0 / 30.0, paused: !(session.isPlaying || session.isRecording))) { _ in
                    MiniMeter(level: session.audio.meters.read(track.id))
                }
            }
            .padding(.leading, 9)
            .padding(.trailing, 10)
        }
        .frame(maxHeight: .infinity)
        .background(isSelected ? Theme.elevated : Color.clear)
        .overlay(alignment: .bottom) { Rectangle().fill(Theme.stroke).frame(height: 1) }
        .contentShape(Rectangle())
        .onTapGesture(count: 2) {
            session.selectedTrackID = track.id
            onShowInspector()
        }
        .onTapGesture {
            session.selectedTrackID = track.id
        }
        .contextMenu {
            Button {
                renameText = track.name
                isRenaming = true
            } label: { Label("Renombrar", systemImage: "pencil") }
            Menu {
                ForEach(TrackColor.allCases, id: \.self) { color in
                    Button(color.displayName) { session.updateTrack(track.id) { $0.color = color } }
                }
            } label: { Label("Color", systemImage: "paintpalette") }
            Button { onShowInspector() } label: { Label("Inspector", systemImage: "slider.horizontal.3") }
            Divider()
            Button(role: .destructive) { session.removeTrack(track.id) } label: {
                Label("Eliminar pista", systemImage: "trash")
            }
        }
        .alert("Renombrar pista", isPresented: $isRenaming) {
            TextField("Nombre", text: $renameText)
            Button("Cancelar", role: .cancel) {}
            Button("Guardar") { session.updateTrack(track.id) { $0.name = renameText } }
        }
    }
}

// MARK: - Carril de pista

struct TrackLaneView: View {
    @Bindable var session: Session
    let track: Track
    let trackIndex: Int
    let trackHeight: CGFloat
    let visibleStart: Double
    let visibleEnd: Double

    var body: some View {
        ZStack(alignment: .topLeading) {
            Color.clear
                .contentShape(Rectangle())
                .onTapGesture(coordinateSpace: .local) { location in
                    session.selectedTrackID = track.id
                    session.selectedClipID = nil
                    session.seek(to: session.snap(Double(location.x) / session.pixelsPerSecond))
                }
            ForEach(track.clips) { clip in
                if clip.end >= visibleStart - 1 && clip.start <= visibleEnd + 1 {
                    ClipView(session: session, clip: clip, track: track, trackIndex: trackIndex,
                             trackHeight: trackHeight, visibleStart: visibleStart, visibleEnd: visibleEnd)
                }
            }
        }
    }
}

// MARK: - Clip

struct ClipView: View {
    @Bindable var session: Session
    let clip: Clip
    let track: Track
    let trackIndex: Int
    let trackHeight: CGFloat
    let visibleStart: Double
    let visibleEnd: Double

    @State private var moveDelta: Double = 0
    @State private var laneDelta: Int = 0
    @State private var trimStartDelta: Double = 0
    @State private var trimEndDelta: Double = 0
    @State private var isDragging = false

    private var isSelected: Bool { session.selectedClipID == clip.id }
    private var pps: Double { session.pixelsPerSecond }
    private var tint: Color { track.color.color }

    private var previewStart: Double { clip.start + trimStartDelta + moveDelta }
    private var previewDuration: Double { max(0.01, clip.duration - trimStartDelta + trimEndDelta) }
    private var previewOffset: Double { clip.offset + trimStartDelta }

    private var canDirectDrag: Bool {
        #if os(macOS)
        true
        #else
        isSelected
        #endif
    }

    var body: some View {
        let width = max(3, CGFloat(previewDuration * pps))
        let height = trackHeight - 8
        let clipX = CGFloat(previewStart * pps)
        // Porción visible del clip en coordenadas locales.
        let visX0 = max(0, CGFloat(visibleStart * pps) - clipX)
        let visX1 = min(width, CGFloat(visibleEnd * pps) - clipX)

        ZStack(alignment: .topLeading) {
            RoundedRectangle(cornerRadius: 8, style: .continuous)
                .fill(
                    LinearGradient(colors: [tint.opacity(isSelected ? 0.42 : 0.26), tint.opacity(isSelected ? 0.28 : 0.14)],
                                   startPoint: .top, endPoint: .bottom)
                )

            if visX1 > visX0 {
                WaveformView(session: session, fileName: clip.fileName, sourceOffset: previewOffset + Double(visX0) / pps,
                             pps: pps, tint: tint)
                    .frame(width: visX1 - visX0, height: height - 18)
                    .offset(x: visX0, y: 16)
                    .allowsHitTesting(false)
            }

            Text(clip.name)
                .font(.system(size: 10, weight: .semibold, design: .rounded))
                .foregroundStyle(Color.white.opacity(0.9))
                .lineLimit(1)
                .padding(.horizontal, 6)
                .padding(.top, 3)
                .frame(maxWidth: max(0, min(width, visX1) - visX0), alignment: .leading)
                .offset(x: visX0)

            RoundedRectangle(cornerRadius: 8, style: .continuous)
                .strokeBorder(isSelected ? Color.white.opacity(0.9) : tint.opacity(0.55), lineWidth: isSelected ? 1.5 : 1)
        }
        .frame(width: width, height: height)
        .clipShape(RoundedRectangle(cornerRadius: 8, style: .continuous))
        .overlay(alignment: .leading) { if isSelected { trimHandle(leading: true) } }
        .overlay(alignment: .trailing) { if isSelected { trimHandle(leading: false) } }
        .shadow(color: isDragging ? .black.opacity(0.5) : .clear, radius: 8, y: 4)
        .contentShape(Rectangle())
        .onTapGesture {
            session.selectedClipID = clip.id
            session.selectedTrackID = track.id
        }
        .gesture(canDirectDrag ? moveGesture : nil)
        .contextMenu {
            Button { session.selectedClipID = clip.id; session.splitAtPlayhead() } label: {
                Label("Dividir en el cursor", systemImage: "scissors")
            }
            Button { session.selectedClipID = clip.id; session.duplicateSelection() } label: {
                Label("Duplicar", systemImage: "plus.square.on.square")
            }
            Divider()
            Button(role: .destructive) { session.selectedClipID = clip.id; session.deleteSelection() } label: {
                Label("Eliminar", systemImage: "trash")
            }
        }
        .offset(x: clipX, y: 4 + CGFloat(laneDelta) * trackHeight)
        .zIndex(isDragging ? 10 : 0)
    }

    private var moveGesture: some Gesture {
        DragGesture(minimumDistance: 4, coordinateSpace: .global)
            .onChanged { g in
                if !isDragging {
                    isDragging = true
                    session.selectedClipID = clip.id
                    session.selectedTrackID = track.id
                }
                let raw = clip.start + Double(g.translation.width) / pps
                moveDelta = session.snap(raw) - clip.start
                let maxIndex = session.project.tracks.count - 1
                let target = min(maxIndex, max(0, trackIndex + Int((g.translation.height / trackHeight).rounded())))
                laneDelta = target - trackIndex
            }
            .onEnded { _ in
                let newStart = clip.start + moveDelta
                let tracks = session.project.tracks
                let targetIndex = trackIndex + laneDelta
                let targetID = tracks.indices.contains(targetIndex) ? tracks[targetIndex].id : track.id
                if abs(moveDelta) > 0.0001 || targetID != track.id {
                    session.edit { $0.moveClip(clip.id, to: newStart, trackID: targetID) }
                }
                moveDelta = 0
                laneDelta = 0
                isDragging = false
            }
    }

    private func trimHandle(leading: Bool) -> some View {
        Rectangle()
            .fill(Color.white.opacity(0.001))
            .frame(width: 12)
            .overlay(
                Capsule()
                    .fill(Color.white.opacity(0.9))
                    .frame(width: 3, height: 22)
            )
            .contentShape(Rectangle())
            .gesture(
                DragGesture(minimumDistance: 1, coordinateSpace: .global)
                    .onChanged { g in
                        let dt = Double(g.translation.width) / pps
                        if leading {
                            let earliest = clip.start - clip.offset
                            let target = session.snap(clip.start + dt)
                            let clamped = min(max(target, earliest, 0), clip.end - 0.05)
                            trimStartDelta = clamped - clip.start
                        } else {
                            let maxEnd = clip.start + (clip.sourceDuration - clip.offset)
                            let target = session.snap(clip.end + dt)
                            let clamped = min(max(target, clip.start + 0.05), maxEnd)
                            trimEndDelta = clamped - clip.end
                        }
                    }
                    .onEnded { _ in
                        if leading {
                            let newStart = clip.start + trimStartDelta
                            session.edit { $0.trimClipStart(clip.id, to: newStart) }
                        } else {
                            let newEnd = clip.end + trimEndDelta
                            session.edit { $0.trimClipEnd(clip.id, to: newEnd) }
                        }
                        trimStartDelta = 0
                        trimEndDelta = 0
                    }
            )
            #if os(macOS)
            .onHover { inside in
                if inside { NSCursor.resizeLeftRight.push() } else { NSCursor.pop() }
            }
            #endif
    }
}

// MARK: - Forma de onda

struct WaveformView: View {
    let session: Session
    let fileName: String
    /// Segundo del archivo fuente que corresponde al borde izquierdo de esta vista.
    let sourceOffset: Double
    let pps: Double
    let tint: Color

    var body: some View {
        let _ = session.waveformRevision
        let peaks = session.waveforms.peaks(for: fileName, projectURL: session.url, sampleRate: session.project.sampleRate)
        Canvas { ctx, size in
            guard let peaks else { return }
            let mid = size.height / 2
            let step: CGFloat = 2
            var path = Path()
            var x: CGFloat = 0
            while x < size.width {
                let t0 = sourceOffset + Double(x) / pps
                let t1 = t0 + Double(step) / pps
                let p = CGFloat(pow(peaks.peak(from: t0, to: t1), 0.75))
                let h = max(0.5, p * mid * 0.95)
                path.addRect(CGRect(x: x, y: mid - h, width: step * 0.7, height: h * 2))
                x += step
            }
            ctx.fill(path, with: .color(tint.opacity(0.95)))
        }
    }
}
