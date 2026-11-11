import SwiftUI
import WorshipDAWCore

struct MixerView: View {
    @Bindable var session: Session
    let midi: MIDIManager
    @State private var pluginTarget: UUID?
    @State private var editingPlugin: PluginEditorTarget?

    var body: some View {
        ScrollView(.horizontal) {
            HStack(spacing: 8) {
                ForEach(session.project.tracks) { track in
                    ChannelStrip(session: session, track: track, midi: midi,
                                 onAddPlugin: { pluginTarget = track.id },
                                 onOpenPlugin: { ref in editingPlugin = PluginEditorTarget(trackID: track.id, plugin: ref) })
                }
                MasterStrip(session: session, midi: midi)
            }
            .padding(10)
        }
        .scrollIndicators(.visible)
        .background(Theme.background)
        .sheet(item: Binding(get: { pluginTarget.map(IdentifiedUUID.init) }, set: { pluginTarget = $0?.id })) { target in
            PluginBrowser { plugin in
                session.updateTrack(target.id) { $0.inserts.append(plugin.makeRef()) }
                pluginTarget = nil
            }
        }
        .sheet(item: $editingPlugin) { target in
            PluginEditorView(session: session, target: target)
        }
    }
}

struct IdentifiedUUID: Identifiable {
    let id: UUID
}

struct PluginEditorTarget: Identifiable {
    let trackID: UUID
    let plugin: PluginRef
    var id: UUID { plugin.id }
}

struct ChannelStrip: View {
    @Bindable var session: Session
    let track: Track
    let midi: MIDIManager
    var onAddPlugin: () -> Void
    var onOpenPlugin: (PluginRef) -> Void

    private var isSelected: Bool { session.selectedTrackID == track.id }

    var body: some View {
        VStack(spacing: 10) {
            // Nombre
            VStack(spacing: 4) {
                Capsule().fill(track.color.color).frame(height: 3)
                    .shadow(color: track.color.color.opacity(0.8), radius: 4)
                Text(track.name)
                    .font(.system(size: 11, weight: .bold, design: .rounded))
                    .lineLimit(1)
                    .foregroundStyle(Theme.textPrimary)
            }

            // Inserts
            VStack(spacing: 3) {
                ForEach(track.inserts) { ref in
                    Button { onOpenPlugin(ref) } label: {
                        HStack(spacing: 4) {
                            Circle()
                                .fill(ref.isBypassed ? Theme.textTertiary : Theme.accent2)
                                .frame(width: 5, height: 5)
                            Text(ref.name)
                                .font(.system(size: 9.5, weight: .semibold))
                                .lineLimit(1)
                            Spacer(minLength: 0)
                        }
                        .padding(.horizontal, 6)
                        .frame(height: 20)
                        .background(RoundedRectangle(cornerRadius: 5).fill(Color.white.opacity(0.06)))
                        .foregroundStyle(ref.isBypassed ? Theme.textTertiary : Theme.textPrimary)
                    }
                    .buttonStyle(.plain)
                    .contextMenu {
                        Button(ref.isBypassed ? "Activar" : "Bypass") {
                            session.updateTrack(track.id) { t in
                                if let i = t.inserts.firstIndex(where: { $0.id == ref.id }) { t.inserts[i].isBypassed.toggle() }
                            }
                        }
                        Button("Quitar", role: .destructive) {
                            session.updateTrack(track.id) { t in t.inserts.removeAll { $0.id == ref.id } }
                        }
                    }
                }
                Button(action: onAddPlugin) {
                    Image(systemName: "plus")
                        .font(.system(size: 10, weight: .bold))
                        .frame(maxWidth: .infinity)
                        .frame(height: 20)
                        .background(RoundedRectangle(cornerRadius: 5).strokeBorder(Theme.stroke, style: StrokeStyle(lineWidth: 1, dash: [3, 3])))
                        .foregroundStyle(Theme.textTertiary)
                }
                .buttonStyle(.plain)
                .help("Agregar plugin (AU)")
            }
            .frame(minHeight: 50, alignment: .top)

            // EQ
            VStack(spacing: 6) {
                eqKnob("Agudos", value: \.highGain, tint: Theme.accent2)
                eqKnob("Medios", value: \.midGain, tint: Theme.accent)
                eqKnob("Graves", value: \.lowGain, tint: Color(red: 1, green: 0.6, blue: 0.4))
            }

            Knob(value: Binding(get: { track.pan },
                                set: { v in session.updateTrack(track.id, undoable: false) { $0.pan = (v * 100).rounded() / 100 } }),
                 range: -1...1, defaultValue: 0, label: formatPan(track.pan), tint: Theme.textSecondary,
                 size: 30, format: formatPan, onBegin: { session.beginGesture() })

            // Fader + medidor
            TimelineView(.animation(minimumInterval: 1.0 / 30.0, paused: !(session.isPlaying || session.isRecording))) { _ in
                Fader(db: Binding(get: { track.volumeDB },
                                  set: { v in session.updateTrack(track.id, undoable: false) { $0.volumeDB = v } }),
                      meterLevel: session.audio.meters.read(track.id), tint: track.color.color,
                      onBegin: { session.beginGesture() })
            }
            .frame(minHeight: 140, maxHeight: .infinity)

            Text(formatDB(track.volumeDB))
                .font(Theme.mono(10))
                .foregroundStyle(Theme.textSecondary)

            HStack(spacing: 3) {
                PillToggle(label: "M", isOn: track.isMuted, tint: Theme.mute, size: 22) {
                    session.updateTrack(track.id) { $0.isMuted.toggle() }
                }
                PillToggle(label: "S", isOn: track.isSolo, tint: Theme.solo, size: 22) {
                    session.updateTrack(track.id) { $0.isSolo.toggle() }
                }
                PillToggle(label: "R", isOn: track.isArmed, tint: Theme.record, size: 22) {
                    session.updateTrack(track.id) { $0.isArmed.toggle() }
                }
                .contextMenu { TrackInputMenu(session: session, track: track) }
            }
            if track.isArmed {
                Text(track.input.displayName)
                    .font(.system(size: 9, weight: .bold, design: .rounded))
                    .foregroundStyle(Theme.record)
                    .lineLimit(1)
            }
        }
        .padding(.horizontal, 8)
        .padding(.vertical, 10)
        .frame(width: 92)
        .background(
            RoundedRectangle(cornerRadius: 14, style: .continuous)
                .fill(isSelected ? Theme.elevated : Theme.panel)
        )
        .overlay(
            RoundedRectangle(cornerRadius: 14, style: .continuous)
                .strokeBorder(isSelected ? track.color.color.opacity(0.5) : Theme.stroke, lineWidth: 1)
        )
        .contentShape(Rectangle())
        .onTapGesture { session.selectedTrackID = track.id }
        .contextMenu { midiLearnMenu }
    }

    /// "Aprender MIDI" (Learn en REAPER): el próximo control que muevas queda asignado.
    @ViewBuilder private var midiLearnMenu: some View {
        if let index = session.project.trackIndex(track.id) {
            Section("Aprender MIDI") {
                ForEach([MIDITarget.trackVolume(index), .trackPan(index), .trackMute(index), .trackSolo(index), .trackArm(index)], id: \.self) { target in
                    Button {
                        midi.learning = target
                    } label: {
                        if let m = midi.mapping(for: target) {
                            Text("\(target.displayName) — \(m.sourceLabel)")
                        } else {
                            Text(target.displayName)
                        }
                    }
                }
            }
            Section {
                Button("Quitar asignaciones MIDI de esta pista") {
                    for t in [MIDITarget.trackVolume(index), .trackPan(index), .trackMute(index), .trackSolo(index), .trackArm(index)] {
                        midi.clearMapping(for: t)
                    }
                }
            }
        }
    }

    private func eqKnob(_ label: String, value: WritableKeyPath<ChannelEQ, Double>, tint: Color) -> some View {
        Knob(value: Binding(get: { track.eq[keyPath: value] },
                            set: { v in session.updateTrack(track.id, undoable: false) { $0.eq[keyPath: value] = (v * 10).rounded() / 10 } }),
             range: -12...12, defaultValue: 0, label: label, tint: tint, size: 30,
             format: { String(format: "%+.1f dB", $0) }, onBegin: { session.beginGesture() })
    }
}

struct MasterStrip: View {
    @Bindable var session: Session
    var midi: MIDIManager?

    var body: some View {
        VStack(spacing: 10) {
            VStack(spacing: 4) {
                Capsule().fill(Theme.accentGradient).frame(height: 3)
                Text("Master")
                    .font(.system(size: 11, weight: .heavy, design: .rounded))
            }
            Spacer(minLength: 0)
            TimelineView(.animation(minimumInterval: 1.0 / 30.0, paused: !(session.isPlaying || session.isRecording))) { _ in
                Fader(db: Binding(get: { session.project.masterVolumeDB },
                                  set: { v in session.edit(undoable: false) { $0.masterVolumeDB = v } }),
                      meterLevel: session.audio.meters.read(MeterBank.masterID), tint: Theme.accent,
                      onBegin: { session.beginGesture() })
            }
            .frame(minHeight: 140, maxHeight: .infinity)
            Text(formatDB(session.project.masterVolumeDB))
                .font(Theme.mono(10))
                .foregroundStyle(Theme.textSecondary)
            if session.isRecording {
                TimelineView(.animation(minimumInterval: 1.0 / 30.0)) { _ in
                    VStack(spacing: 2) {
                        Text("Entrada").font(.system(size: 9, weight: .semibold)).foregroundStyle(Theme.textTertiary)
                        MiniMeter(level: session.audio.meters.read(MeterBank.inputID))
                    }
                }
            }
        }
        .padding(10)
        .frame(width: 92)
        .background(
            RoundedRectangle(cornerRadius: 14, style: .continuous)
                .fill(LinearGradient(colors: [Theme.elevated, Theme.panel], startPoint: .top, endPoint: .bottom))
        )
        .overlay(
            RoundedRectangle(cornerRadius: 14, style: .continuous)
                .strokeBorder(Theme.accent.opacity(0.35), lineWidth: 1)
        )
        .contextMenu {
            if let midi {
                Button("Aprender MIDI: volumen master") { midi.learning = .masterVolume }
            }
        }
    }
}

// MARK: - Navegador de plugins

struct PluginBrowser: View {
    var onPick: (AvailablePlugin) -> Void
    @Environment(\.dismiss) private var dismiss
    @State private var plugins: [AvailablePlugin] = []
    @State private var query = ""
    @State private var loaded = false

    private var filtered: [AvailablePlugin] {
        guard !query.isEmpty else { return plugins }
        return plugins.filter {
            $0.name.localizedCaseInsensitiveContains(query) || $0.manufacturerName.localizedCaseInsensitiveContains(query)
        }
    }

    var body: some View {
        NavigationStack {
            List {
                let worship = filtered.filter(\.isWorshipSounds)
                if !worship.isEmpty {
                    Section("Worship Sounds") {
                        ForEach(worship) { row($0) }
                    }
                }
                let others = filtered.filter { !$0.isWorshipSounds }
                Section("Audio Units") {
                    ForEach(others) { row($0) }
                    if loaded && others.isEmpty {
                        Text("No se encontraron plugins de efecto.")
                            .foregroundStyle(.secondary)
                    }
                }
            }
            .searchable(text: $query, prompt: "Buscar plugin")
            .navigationTitle("Agregar plugin")
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Cerrar") { dismiss() }
                }
            }
        }
        .macSheetFrame(width: 420, height: 480)
        .task {
            plugins = PluginCatalog.availableEffects()
            loaded = true
        }
    }

    private func row(_ plugin: AvailablePlugin) -> some View {
        Button { onPick(plugin) } label: {
            HStack(spacing: 10) {
                Image(systemName: plugin.isWorshipSounds ? "sparkles" : "puzzlepiece.extension")
                    .foregroundStyle(plugin.isWorshipSounds ? Theme.accent : .secondary)
                    .frame(width: 22)
                VStack(alignment: .leading, spacing: 1) {
                    Text(plugin.name).font(.body.weight(.semibold))
                    Text(plugin.manufacturerName).font(.caption).foregroundStyle(.secondary)
                }
                Spacer()
            }
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
    }
}

// MARK: - Editor de plugin (interfaz nativa del AU)

struct PluginEditorView: View {
    let session: Session
    let target: PluginEditorTarget
    @Environment(\.dismiss) private var dismiss
    @State private var controller: PlatformViewController?
    @State private var loading = true

    var body: some View {
        VStack(spacing: 0) {
            HStack {
                VStack(alignment: .leading, spacing: 2) {
                    Text(target.plugin.name).font(Theme.display(15, weight: .bold))
                    Text(target.plugin.manufacturerName).font(.caption).foregroundStyle(Theme.textSecondary)
                }
                Spacer()
                Button("Listo") { dismiss() }
                    .buttonStyle(.borderedProminent)
            }
            .padding(12)
            .background(Theme.panel)
            Group {
                if let controller {
                    PluginViewHost(controller: controller)
                } else if loading {
                    ProgressView("Cargando interfaz…")
                } else {
                    ContentUnavailableView("Sin interfaz", systemImage: "slider.horizontal.3",
                                           description: Text("Este plugin no ofrece una interfaz gráfica o aún no terminó de cargar."))
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
        .macSheetFrame(width: 520, height: 380)
        .background(Theme.background)
        .task {
            // El plugin puede tardar en instanciarse: reintentar unos segundos.
            for _ in 0..<20 {
                if let unit = session.audio.pluginUnit(trackID: target.trackID, pluginID: target.plugin.id) {
                    controller = await PluginCatalog.requestViewController(for: unit)
                    break
                }
                try? await Task.sleep(for: .milliseconds(250))
            }
            loading = false
        }
    }
}

#if os(macOS)
struct PluginViewHost: NSViewControllerRepresentable {
    let controller: NSViewController
    func makeNSViewController(context: Context) -> NSViewController { controller }
    func updateNSViewController(_ nsViewController: NSViewController, context: Context) {}
}
#else
struct PluginViewHost: UIViewControllerRepresentable {
    let controller: UIViewController
    func makeUIViewController(context: Context) -> UIViewController { controller }
    func updateUIViewController(_ uiViewController: UIViewController, context: Context) {}
}
#endif
