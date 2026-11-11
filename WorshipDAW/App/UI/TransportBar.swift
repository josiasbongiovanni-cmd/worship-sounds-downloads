import SwiftUI
import WorshipDAWCore

/// Contador grande de posición: compás.tiempo y tiempo absoluto.
struct PositionDisplay: View {
    let session: Session
    var compact = false

    var body: some View {
        let timeline = session.project.timeline
        let pos = timeline.barBeat(at: session.playhead)
        HStack(spacing: compact ? 8 : 14) {
            VStack(alignment: .leading, spacing: 0) {
                Text("\(pos.bar).\(pos.beat)")
                    .font(.system(size: compact ? 22 : 26, weight: .bold, design: .rounded))
                    .monospacedDigit()
                    .foregroundStyle(Theme.textPrimary)
                    .contentTransition(.numericText())
                Text(Timeline.timecode(session.playhead))
                    .font(Theme.mono(compact ? 10 : 11))
                    .foregroundStyle(Theme.textSecondary)
            }
            .frame(minWidth: compact ? 72 : 96, alignment: .leading)
            if let section = session.project.section(at: session.playhead) {
                Text(compact ? section.kind.shortName : section.name)
                    .font(Theme.display(compact ? 12 : 13, weight: .bold))
                    .padding(.horizontal, 10)
                    .padding(.vertical, 5)
                    .background(Capsule().fill(section.kind.color.opacity(0.22)))
                    .overlay(Capsule().strokeBorder(section.kind.color.opacity(0.6), lineWidth: 1))
                    .foregroundStyle(section.kind.color)
                    .transition(.opacity)
            }
        }
        .padding(.horizontal, 14)
        .padding(.vertical, 6)
        .background(RoundedRectangle(cornerRadius: 12, style: .continuous).fill(Color.black.opacity(0.35)))
        .overlay(RoundedRectangle(cornerRadius: 12, style: .continuous).strokeBorder(Theme.stroke))
    }
}

struct TransportControls: View {
    @Bindable var session: Session
    var size: CGFloat = 34

    var body: some View {
        HStack(spacing: 8) {
            TransportButton(systemName: "backward.end.fill", size: size) { session.returnToStart() }
                .help("Volver al inicio (Inicio)")
            TransportButton(systemName: "chevron.backward.2", size: size) { session.goToPreviousMarker() }
                .help("Marcador / sección anterior ([)")
            Button {
                session.togglePlay()
            } label: {
                Image(systemName: session.isPlaying ? "stop.fill" : "play.fill")
                    .font(.system(size: size * 0.48, weight: .bold))
                    .foregroundStyle(.white)
                    .frame(width: size * 1.35, height: size * 1.35)
                    .background(Circle().fill(Theme.accentGradient))
                    .shadow(color: Theme.accent.opacity(0.6), radius: session.isPlaying ? 14 : 6)
            }
            .buttonStyle(.plain)
            .help("Reproducir / Detener (Espacio)")
            Button {
                session.toggleRecord()
            } label: {
                Circle()
                    .fill(Theme.record)
                    .frame(width: size * 0.42, height: size * 0.42)
                    .frame(width: size, height: size)
                    .background(Circle().fill(Theme.record.opacity(session.isRecording ? 0.3 : 0.08)))
                    .overlay(Circle().strokeBorder(Theme.record.opacity(session.isRecording ? 0.9 : 0.35), lineWidth: 1))
                    .shadow(color: session.isRecording ? Theme.record.opacity(0.7) : .clear, radius: 10)
            }
            .buttonStyle(.plain)
            .help("Grabar (⌘R)")
            TransportButton(systemName: "chevron.forward.2", size: size) { session.goToNextMarker() }
                .help("Marcador / sección siguiente (])")
            TransportButton(systemName: "repeat", isActive: session.project.loop.isEnabled, tint: Theme.loop, size: size) {
                session.toggleLoop()
            }
            .help("Repetir (R)")
            TransportButton(systemName: "metronome.fill", isActive: session.project.isClickEnabled, tint: Theme.solo, size: size) {
                session.edit { $0.isClickEnabled.toggle() }
            }
            .help("Metrónomo")
        }
    }
}

/// Chips de BPM / tonalidad / compás editables.
struct SongInfoChips: View {
    @Bindable var session: Session
    @State private var showTempo = false

    var body: some View {
        Button { showTempo = true } label: {
            HStack(spacing: 6) {
                chip("\(Int(session.project.bpm.rounded()))", unit: "BPM")
                chip(session.project.key, unit: "Tono")
                chip("\(session.project.beatsPerBar)/4", unit: nil)
            }
        }
        .buttonStyle(.plain)
        .popover(isPresented: $showTempo) {
            SongSettingsForm(session: session)
                .frame(minWidth: 280)
                .padding()
                .presentationCompactAdaptation(.popover)
        }
    }

    private func chip(_ value: String, unit: String?) -> some View {
        HStack(spacing: 3) {
            Text(value)
                .font(Theme.display(13, weight: .bold))
                .monospacedDigit()
            if let unit {
                Text(unit)
                    .font(.system(size: 9, weight: .semibold))
                    .foregroundStyle(Theme.textTertiary)
            }
        }
        .padding(.horizontal, 9)
        .padding(.vertical, 5)
        .background(Capsule().fill(Color.white.opacity(0.06)))
        .overlay(Capsule().strokeBorder(Theme.stroke))
        .foregroundStyle(Theme.textPrimary)
    }
}

struct SongSettingsForm: View {
    @Bindable var session: Session

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            Text("Canción")
                .font(Theme.display(15, weight: .bold))
            TextField("Nombre", text: Binding(
                get: { session.project.name },
                set: { name in session.edit { $0.name = name } }))
                .textFieldStyle(.roundedBorder)
            HStack {
                Text("Tempo")
                Spacer()
                Stepper(value: Binding(
                    get: { session.project.bpm },
                    set: { bpm in session.edit { $0.bpm = min(300, max(20, bpm.rounded())) } }),
                        in: 20...300, step: 1) {
                    Text("\(Int(session.project.bpm)) BPM").monospacedDigit()
                }
                .fixedSize()
            }
            HStack {
                Text("Tonalidad")
                Spacer()
                Picker("Tonalidad", selection: Binding(
                    get: { session.project.key },
                    set: { key in session.edit { $0.key = key } })) {
                    ForEach(MusicalKey.all, id: \.self) { Text($0).tag($0) }
                }
                .labelsHidden()
                .fixedSize()
            }
            HStack {
                Text("Compás")
                Spacer()
                Picker("Compás", selection: Binding(
                    get: { session.project.beatsPerBar },
                    set: { beats in session.edit { $0.beatsPerBar = beats } })) {
                    ForEach([2, 3, 4, 6], id: \.self) { Text("\($0)/4").tag($0) }
                }
                .labelsHidden()
                .pickerStyle(.segmented)
                .fixedSize()
            }
            Divider()
            HStack {
                Toggle("Click", isOn: Binding(
                    get: { session.project.isClickEnabled },
                    set: { on in session.edit { $0.isClickEnabled = on } }))
                Spacer()
                Slider(value: Binding(
                    get: { session.project.clickVolumeDB },
                    set: { v in session.edit(undoable: false) { $0.clickVolumeDB = v } }),
                       in: -40...6)
                    .frame(width: 120)
            }
        }
    }
}

struct TransportBar: View {
    @Bindable var session: Session
    @Binding var showMixer: Bool
    @Binding var showInspector: Bool
    var onClose: () -> Void
    var onImport: () -> Void

    var body: some View {
        HStack(spacing: 14) {
            Button(action: onClose) {
                Image(systemName: "square.grid.2x2")
                    .font(.system(size: 14, weight: .semibold))
                    .frame(width: 30, height: 30)
                    .background(RoundedRectangle(cornerRadius: 8).fill(Color.white.opacity(0.05)))
            }
            .buttonStyle(.plain)
            .help("Proyectos")
            VStack(alignment: .leading, spacing: 2) {
                Text(session.project.name)
                    .font(Theme.display(15, weight: .bold))
                    .lineLimit(1)
                Text(session.statusMessage ?? "\(session.project.tracks.count) pistas · \(session.project.sections.count) secciones")
                    .font(.system(size: 11))
                    .foregroundStyle(Theme.textSecondary)
                    .lineLimit(1)
            }
            .frame(minWidth: 120, maxWidth: 220, alignment: .leading)
            SongInfoChips(session: session)
            Spacer(minLength: 8)
            TransportControls(session: session)
            PositionDisplay(session: session)
            Spacer(minLength: 8)
            HStack(spacing: 6) {
                TransportButton(systemName: "arrow.uturn.backward", size: 30) { session.undo() }
                    .disabled(!session.canUndo)
                    .help("Deshacer (⌘Z)")
                TransportButton(systemName: "square.and.arrow.down", size: 30, action: onImport)
                    .help("Importar audio (⌘I)")
                exportButton
                TransportButton(systemName: "gearshape", size: 30) {
                    NotificationCenter.default.post(name: .worshipShowPreferences, object: nil)
                }
                .help("Preferencias: audio y MIDI (⌘,)")
                TransportButton(systemName: "slider.vertical.3", isActive: showMixer, tint: Theme.accent, size: 30) {
                    withAnimation(.snappy) { showMixer.toggle() }
                }
                .help("Mezclador (⌘M)")
                TransportButton(systemName: "sidebar.right", isActive: showInspector, tint: Theme.accent, size: 30) {
                    withAnimation(.snappy) { showInspector.toggle() }
                }
                .help("Inspector")
            }
        }
        .padding(.horizontal, 14)
        .padding(.vertical, 10)
        .background(Theme.panel)
        .overlay(alignment: .bottom) { Rectangle().fill(Theme.stroke).frame(height: 1) }
    }

    @ViewBuilder private var exportButton: some View {
        if let progress = session.exportProgress {
            ProgressView(value: progress)
                .progressViewStyle(.circular)
                .controlSize(.small)
                .frame(width: 30, height: 30)
        } else if let url = session.lastExportURL {
            Menu {
                ShareLink(item: url) { Label("Compartir mezcla", systemImage: "square.and.arrow.up") }
                Button("Exportar de nuevo") { session.exportMix() }
            } label: {
                Image(systemName: "checkmark.circle.fill")
                    .foregroundStyle(Theme.accent2)
                    .frame(width: 30, height: 30)
            }
            .menuStyle(.button)
            .buttonStyle(.plain)
            .fixedSize()
        } else {
            TransportButton(systemName: "square.and.arrow.up", size: 30) { session.exportMix() }
                .help("Renderizar mezcla WAV (⌥⌘R)")
        }
    }
}

/// Transporte compacto para iPhone.
struct CompactTransportBar: View {
    @Bindable var session: Session
    var onClose: () -> Void
    var onImport: () -> Void

    var body: some View {
        VStack(spacing: 10) {
            HStack(spacing: 10) {
                Button(action: onClose) {
                    Image(systemName: "chevron.left")
                        .font(.system(size: 15, weight: .bold))
                        .frame(width: 32, height: 32)
                        .background(Circle().fill(Color.white.opacity(0.06)))
                }
                .buttonStyle(.plain)
                VStack(alignment: .leading, spacing: 1) {
                    Text(session.project.name)
                        .font(Theme.display(16, weight: .bold))
                        .lineLimit(1)
                    Text("\(Int(session.project.bpm)) BPM · \(session.project.key)")
                        .font(.system(size: 11, weight: .medium))
                        .foregroundStyle(Theme.textSecondary)
                }
                Spacer()
                Menu {
                    Button { onImport() } label: { Label("Importar audio", systemImage: "square.and.arrow.down") }
                    Button { session.exportMix() } label: { Label("Exportar mezcla", systemImage: "square.and.arrow.up") }
                    if let url = session.lastExportURL {
                        ShareLink(item: url) { Label("Compartir última mezcla", systemImage: "paperplane") }
                    }
                    Button {
                        NotificationCenter.default.post(name: .worshipShowPreferences, object: nil)
                    } label: { Label("Preferencias (audio y MIDI)", systemImage: "gearshape") }
                    Divider()
                    Button { session.undo() } label: { Label("Deshacer", systemImage: "arrow.uturn.backward") }
                        .disabled(!session.canUndo)
                    Button { session.redo() } label: { Label("Rehacer", systemImage: "arrow.uturn.forward") }
                        .disabled(!session.canRedo)
                } label: {
                    Image(systemName: session.exportProgress != nil ? "hourglass" : "ellipsis")
                        .font(.system(size: 15, weight: .bold))
                        .frame(width: 32, height: 32)
                        .background(Circle().fill(Color.white.opacity(0.06)))
                }
                .buttonStyle(.plain)
            }
            HStack {
                PositionDisplay(session: session, compact: true)
                Spacer(minLength: 0)
            }
            TransportControls(session: session, size: 32)
        }
        .padding(.horizontal, 14)
        .padding(.top, 6)
        .padding(.bottom, 10)
        .background(Theme.panel)
        .overlay(alignment: .bottom) { Rectangle().fill(Theme.stroke).frame(height: 1) }
    }
}
