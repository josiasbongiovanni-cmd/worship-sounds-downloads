import SwiftUI
import WorshipDAWCore

/// Panel lateral: pista seleccionada, canción y estructura.
struct InspectorView: View {
    @Bindable var session: Session
    @State private var tab: Tab = .track

    enum Tab: String, CaseIterable {
        case track = "Pista"
        case song = "Canción"
        case structure = "Estructura"
    }

    var body: some View {
        VStack(spacing: 0) {
            Picker("", selection: $tab) {
                ForEach(Tab.allCases, id: \.self) { Text($0.rawValue).tag($0) }
            }
            .pickerStyle(.segmented)
            .labelsHidden()
            .padding(12)
            ScrollView {
                Group {
                    switch tab {
                    case .track: TrackInspector(session: session)
                    case .song: SongSettingsForm(session: session)
                    case .structure: StructureEditor(session: session)
                    }
                }
                .padding(14)
            }
        }
        .background(Theme.panel)
        .overlay(alignment: .leading) { Rectangle().fill(Theme.stroke).frame(width: 1) }
    }
}

struct TrackInspector: View {
    @Bindable var session: Session

    var body: some View {
        if let id = session.selectedTrackID, let track = session.project.tracks.first(where: { $0.id == id }) {
            VStack(alignment: .leading, spacing: 16) {
                TextField("Nombre", text: Binding(
                    get: { track.name },
                    set: { name in session.updateTrack(id) { $0.name = name } }))
                    .textFieldStyle(.roundedBorder)
                    .font(Theme.display(14, weight: .semibold))

                VStack(alignment: .leading, spacing: 8) {
                    label("Color")
                    LazyVGrid(columns: Array(repeating: GridItem(.flexible(), spacing: 8), count: 5), spacing: 8) {
                        ForEach(TrackColor.allCases, id: \.self) { color in
                            Circle()
                                .fill(color.color)
                                .frame(width: 26, height: 26)
                                .overlay(Circle().strokeBorder(Color.white, lineWidth: track.color == color ? 2 : 0))
                                .shadow(color: track.color == color ? color.color.opacity(0.8) : .clear, radius: 5)
                                .onTapGesture { session.updateTrack(id) { $0.color = color } }
                        }
                    }
                }

                VStack(alignment: .leading, spacing: 8) {
                    label("Tipo")
                    Picker("Tipo", selection: Binding(get: { track.role }, set: { r in session.updateTrack(id) { $0.role = r } })) {
                        ForEach(TrackRole.allCases, id: \.self) { Text($0.displayName).tag($0) }
                    }
                    .pickerStyle(.segmented)
                    .labelsHidden()
                }

                VStack(alignment: .leading, spacing: 8) {
                    label("Grabación")
                    HStack {
                        Toggle("Armada", isOn: Binding(get: { track.isArmed }, set: { on in session.updateTrack(id) { $0.isArmed = on } }))
                            .toggleStyle(.button)
                            .tint(Theme.record)
                        Spacer()
                        Menu(track.input.displayName) { TrackInputMenu(session: session, track: track) }
                            .fixedSize()
                    }
                    Picker("Monitoreo", selection: Binding(get: { track.monitor }, set: { m in session.updateTrack(id) { $0.monitor = m } })) {
                        ForEach(MonitorMode.allCases, id: \.self) { Text($0.displayName).tag($0) }
                    }
                    .font(.system(size: 12))
                }

                VStack(alignment: .leading, spacing: 8) {
                    HStack {
                        label("EQ de canal")
                        Spacer()
                        Toggle("", isOn: Binding(get: { track.eq.isEnabled }, set: { on in session.updateTrack(id) { $0.eq.isEnabled = on } }))
                            .labelsHidden()
                            .toggleStyle(.switch)
                            .controlSize(.mini)
                    }
                    HStack(spacing: 18) {
                        eqKnob(track, "Graves", \.lowGain)
                        eqKnob(track, "Medios", \.midGain)
                        eqKnob(track, "Agudos", \.highGain)
                    }
                    .frame(maxWidth: .infinity)
                }

                VStack(alignment: .leading, spacing: 8) {
                    label("Volumen · \(formatDB(track.volumeDB)) dB   Paneo · \(formatPan(track.pan))")
                    Slider(value: Binding(get: { Fader.position(for: track.volumeDB) },
                                          set: { p in session.updateTrack(id, undoable: false) { $0.volumeDB = (Fader.db(for: p) * 10).rounded() / 10 } }),
                           in: 0...1, onEditingChanged: { if $0 { session.beginGesture() } })
                        .tint(track.color.color)
                    Slider(value: Binding(get: { track.pan },
                                          set: { p in session.updateTrack(id, undoable: false) { $0.pan = (p * 100).rounded() / 100 } }),
                           in: -1...1, onEditingChanged: { if $0 { session.beginGesture() } })
                        .tint(Theme.textSecondary)
                }

                VStack(alignment: .leading, spacing: 8) {
                    label("Clips · \(track.clips.count)")
                    ForEach(track.clips) { clip in
                        HStack {
                            Text(clip.name).lineLimit(1)
                            Spacer()
                            Text(session.project.timeline.barBeatString(at: clip.start))
                                .font(Theme.mono(10))
                                .foregroundStyle(Theme.textTertiary)
                        }
                        .font(.system(size: 12))
                        .padding(8)
                        .background(RoundedRectangle(cornerRadius: 8).fill(session.selectedClipID == clip.id ? track.color.color.opacity(0.25) : Color.white.opacity(0.04)))
                        .onTapGesture {
                            session.selectedClipID = clip.id
                            session.seek(to: clip.start)
                        }
                    }
                }

                Button(role: .destructive) { session.removeTrack(id) } label: {
                    Label("Eliminar pista", systemImage: "trash")
                }
                .buttonStyle(.bordered)
            }
        } else {
            ContentUnavailableView("Ninguna pista", systemImage: "waveform", description: Text("Selecciona una pista para ver sus ajustes."))
        }
    }

    private func label(_ text: String) -> some View {
        Text(text.uppercased())
            .font(.system(size: 10, weight: .bold))
            .tracking(0.6)
            .foregroundStyle(Theme.textTertiary)
    }

    private func eqKnob(_ track: Track, _ title: String, _ kp: WritableKeyPath<ChannelEQ, Double>) -> some View {
        Knob(value: Binding(get: { track.eq[keyPath: kp] },
                            set: { v in session.updateTrack(track.id, undoable: false) { $0.eq[keyPath: kp] = (v * 10).rounded() / 10 } }),
             range: -12...12, defaultValue: 0, label: title, tint: track.color.color, size: 40,
             format: { String(format: "%+.1f dB", $0) }, onBegin: { session.beginGesture() })
    }
}

/// Edición de la estructura de la canción (secciones).
struct StructureEditor: View {
    @Bindable var session: Session
    @State private var newBars = 8

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            Text("Agregar sección")
                .font(.system(size: 10, weight: .bold))
                .foregroundStyle(Theme.textTertiary)
            LazyVGrid(columns: [GridItem(.adaptive(minimum: 82), spacing: 6)], spacing: 6) {
                ForEach(SectionKind.allCases, id: \.self) { kind in
                    Button { session.addSection(kind, bars: newBars) } label: {
                        Text(kind.displayName)
                            .font(.system(size: 11, weight: .bold, design: .rounded))
                            .frame(maxWidth: .infinity)
                            .padding(.vertical, 7)
                            .background(RoundedRectangle(cornerRadius: 8).fill(kind.color.opacity(0.18)))
                            .overlay(RoundedRectangle(cornerRadius: 8).strokeBorder(kind.color.opacity(0.5)))
                            .foregroundStyle(kind.color)
                    }
                    .buttonStyle(.plain)
                }
            }
            Stepper("Largo: \(newBars) compases", value: $newBars, in: 1...32)
                .font(.system(size: 12))

            Divider()

            ForEach(session.project.sections) { section in
                HStack(spacing: 10) {
                    RoundedRectangle(cornerRadius: 3).fill(section.kind.color).frame(width: 4, height: 26)
                    VStack(alignment: .leading, spacing: 1) {
                        Text(section.name).font(.system(size: 12, weight: .semibold))
                        Text("\(session.project.timeline.barBeatString(at: section.start)) · \(Int((section.length / session.project.timeline.secondsPerBar).rounded())) compases")
                            .font(Theme.mono(9))
                            .foregroundStyle(Theme.textTertiary)
                    }
                    Spacer()
                    Button { session.loopSection(section.id) } label: { Image(systemName: "repeat") }
                        .buttonStyle(.plain)
                        .foregroundStyle(Theme.loop)
                    Button { session.edit { $0.removeSection(section.id) } } label: { Image(systemName: "xmark") }
                        .buttonStyle(.plain)
                        .foregroundStyle(Theme.textTertiary)
                }
                .contentShape(Rectangle())
                .onTapGesture { session.seek(to: section.start) }
            }
        }
    }
}

/// Modo en vivo: pads grandes de secciones para navegar la canción (ideal en iPhone sobre el atril).
struct LivePadsView: View {
    @Bindable var session: Session

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 16) {
                if session.project.sections.isEmpty {
                    ContentUnavailableView("Sin estructura", systemImage: "rectangle.split.3x1",
                                           description: Text("Agrega secciones (Intro, Verso, Coro…) desde Estructura."))
                        .padding(.top, 40)
                }
                LazyVGrid(columns: [GridItem(.adaptive(minimum: 140), spacing: 10)], spacing: 10) {
                    ForEach(session.project.sections) { section in
                        pad(section)
                    }
                }
                HStack(spacing: 10) {
                    Toggle(isOn: Binding(get: { session.project.loop.isEnabled },
                                         set: { on in session.edit { $0.loop.isEnabled = on } })) {
                        Label("Repetir sección", systemImage: "repeat")
                    }
                    .toggleStyle(.button)
                    .tint(Theme.loop)
                    Toggle(isOn: Binding(get: { session.project.isClickEnabled },
                                         set: { on in session.edit { $0.isClickEnabled = on } })) {
                        Label("Click", systemImage: "metronome")
                    }
                    .toggleStyle(.button)
                    .tint(Theme.solo)
                }
                .font(.system(size: 13, weight: .semibold))
            }
            .padding(14)
        }
        .background(Theme.background)
    }

    private func pad(_ section: SongSection) -> some View {
        let isCurrent = session.project.section(at: session.playhead)?.id == section.id
        let isLooping = session.project.loop.isEnabled
            && abs(session.project.loop.start - section.start) < 0.01
            && abs(session.project.loop.end - section.end) < 0.01
        let progress = isCurrent ? (session.playhead - section.start) / max(0.01, section.length) : 0
        return Button {
            session.seek(to: section.start)
            if !session.isPlaying { session.play() }
        } label: {
            VStack(alignment: .leading, spacing: 6) {
                HStack {
                    Text(section.kind.shortName)
                        .font(.system(size: 11, weight: .heavy, design: .rounded))
                        .padding(.horizontal, 6)
                        .padding(.vertical, 2)
                        .background(Capsule().fill(section.kind.color.opacity(0.3)))
                    Spacer()
                    if isLooping { Image(systemName: "repeat").font(.system(size: 11, weight: .bold)) }
                }
                Spacer(minLength: 0)
                Text(section.name)
                    .font(Theme.display(18, weight: .bold))
                    .lineLimit(1)
                    .minimumScaleFactor(0.7)
                GeometryReader { geo in
                    Capsule().fill(Color.white.opacity(0.1))
                        .overlay(alignment: .leading) {
                            Capsule().fill(section.kind.color).frame(width: geo.size.width * min(1, max(0, progress)))
                        }
                }
                .frame(height: 4)
            }
            .padding(12)
            .frame(height: 104)
            .foregroundStyle(isCurrent ? Color.white : section.kind.color)
            .background(
                RoundedRectangle(cornerRadius: 16, style: .continuous)
                    .fill(section.kind.color.opacity(isCurrent ? 0.45 : 0.14))
            )
            .overlay(
                RoundedRectangle(cornerRadius: 16, style: .continuous)
                    .strokeBorder(section.kind.color.opacity(isCurrent ? 1 : 0.35), lineWidth: isCurrent ? 2 : 1)
            )
            .shadow(color: isCurrent ? section.kind.color.opacity(0.5) : .clear, radius: 12)
        }
        .buttonStyle(.plain)
        .contextMenu {
            Button { session.loopSection(section.id) } label: { Label("Repetir esta sección", systemImage: "repeat") }
        }
        .sensoryFeedback(.impact(weight: .light), trigger: isCurrent)
    }
}
