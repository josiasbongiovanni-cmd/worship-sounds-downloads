import SwiftUI
import WorshipDAWCore

/// Preferencias (⌘, en Mac), organizadas como las de REAPER: Audio › Dispositivo, MIDI, Teclado.
struct PreferencesView: View {
    @Bindable var app: AppModel
    @State private var tab: Tab = .audio

    enum Tab: String, CaseIterable, Identifiable {
        case audio = "Audio"
        case midi = "MIDI"
        case keys = "Atajos"
        var id: String { rawValue }
        var symbol: String {
            switch self {
            case .audio: "hifispeaker.2"
            case .midi: "pianokeys"
            case .keys: "keyboard"
            }
        }
    }

    var body: some View {
        TabView(selection: $tab) {
            AudioPreferences(app: app)
                .tabItem { Label(Tab.audio.rawValue, systemImage: Tab.audio.symbol) }
                .tag(Tab.audio)
            MIDIPreferences(app: app)
                .tabItem { Label(Tab.midi.rawValue, systemImage: Tab.midi.symbol) }
                .tag(Tab.midi)
            ShortcutsReference()
                .tabItem { Label(Tab.keys.rawValue, systemImage: Tab.keys.symbol) }
                .tag(Tab.keys)
        }
        .macSheetFrame(width: 560, height: 520)
        .preferredColorScheme(.dark)
    }
}

// MARK: - Audio

struct AudioPreferences: View {
    @Bindable var app: AppModel
    private var settings: AudioSettings { app.audioSettings }

    var body: some View {
        Form {
            Section {
                Picker("Entrada", selection: binding(\.inputDeviceID)) {
                    Text("Predeterminada del sistema").tag(String?.none)
                    ForEach(settings.inputDevices) { d in
                        Text("\(d.name) (\(d.inputChannels) entradas)").tag(String?.some(d.id))
                    }
                }
                #if os(macOS)
                Picker("Salida", selection: binding(\.outputDeviceID)) {
                    Text("Predeterminada del sistema").tag(String?.none)
                    ForEach(settings.outputDevices) { d in
                        Text("\(d.name) (\(d.outputChannels) salidas)").tag(String?.some(d.id))
                    }
                }
                #else
                LabeledContent("Salida", value: settings.outputDevices.map(\.name).joined(separator: ", "))
                #endif
            } header: {
                Text("Dispositivo")
            } footer: {
                Text("Elige tu interfaz de audio. En Mac usa la misma interfaz para entrada y salida para evitar desfases.")
            }

            Section {
                Picker("Frecuencia de muestreo", selection: binding(\.sampleRate)) {
                    Text("La del dispositivo").tag(0.0)
                    ForEach(AudioSettings.sampleRates, id: \.self) { r in
                        Text(String(format: "%.1f kHz", r / 1000)).tag(r)
                    }
                }
                Picker("Tamaño de buffer", selection: binding(\.bufferSize)) {
                    Text("El del dispositivo").tag(0)
                    ForEach(AudioSettings.bufferSizes, id: \.self) { b in
                        Text("\(b) muestras").tag(b)
                    }
                }
                LabeledContent("Latencia de salida", value: settings.latencyDescription)
                LabeledContent("Canales de entrada", value: "\(settings.inputChannelCount)")
            } header: {
                Text("Solicitar al dispositivo")
            } footer: {
                Text("Buffers chicos (64–128) bajan la latencia al grabar; buffers grandes (512–1024) dan más estabilidad al mezclar con muchos plugins.")
            }

            if let error = settings.lastError {
                Section { Label(error, systemImage: "exclamationmark.triangle").foregroundStyle(.orange) }
            }

            Section {
                HStack {
                    Button("Buscar dispositivos") { settings.refreshDevices() }
                    Spacer()
                    Button("Aplicar") { app.applyAudioSettings() }
                        .buttonStyle(.borderedProminent)
                }
            }
        }
        .formStyle(.grouped)
        .onAppear { settings.refreshDevices() }
    }

    private func binding<T>(_ keyPath: ReferenceWritableKeyPath<AudioSettings, T>) -> Binding<T> {
        Binding(get: { settings[keyPath: keyPath] }, set: { settings[keyPath: keyPath] = $0 })
    }
}

// MARK: - MIDI

struct MIDIPreferences: View {
    @Bindable var app: AppModel
    private var midi: MIDIManager { app.midi }

    var body: some View {
        Form {
            Section {
                if midi.sources.isEmpty {
                    Text("No hay controladores MIDI conectados.")
                        .foregroundStyle(.secondary)
                }
                ForEach(midi.sources) { source in
                    Toggle(isOn: Binding(get: { source.isEnabled }, set: { midi.setEnabled($0, source: source) })) {
                        Label(source.name, systemImage: "pianokeys")
                    }
                }
                LabeledContent("Último mensaje", value: midi.lastMessage?.label ?? "—")
            } header: {
                Text("Entradas MIDI")
            } footer: {
                Text("Se detectan al conectarlos (USB, Bluetooth o red). Desactiva los que no quieras usar.")
            }

            Section {
                ForEach(MIDITarget.transportTargets, id: \.self) { target in
                    LearnRow(midi: midi, target: target)
                }
            } header: {
                Text("Transporte")
            } footer: {
                Text("Toca \"Aprender\" y mueve o presiona el control. Los faders, paneos, mute, solo y armar de cada pista se aprenden con clic derecho (o pulsación larga) en el mezclador.")
            }

            if !midi.mappings.isEmpty {
                Section("Asignaciones") {
                    ForEach(midi.mappings) { m in
                        HStack {
                            VStack(alignment: .leading, spacing: 2) {
                                Text(m.target.displayName)
                                Text(m.sourceLabel).font(.caption).foregroundStyle(.secondary)
                            }
                            Spacer()
                            Button(role: .destructive) { midi.removeMapping(m.id) } label: { Image(systemName: "trash") }
                                .buttonStyle(.borderless)
                        }
                    }
                }
            }

            if let error = midi.errorMessage {
                Section { Label(error, systemImage: "exclamationmark.triangle").foregroundStyle(.orange) }
            }
        }
        .formStyle(.grouped)
        .onAppear { midi.refreshSources() }
    }
}

struct LearnRow: View {
    let midi: MIDIManager
    let target: MIDITarget

    var body: some View {
        HStack {
            Text(target.displayName)
            Spacer()
            if midi.learning == target {
                Text("Esperando MIDI…").foregroundStyle(Theme.accent2)
                Button("Cancelar") { midi.learning = nil }
            } else {
                Text(midi.mapping(for: target)?.sourceLabel ?? "Sin asignar")
                    .foregroundStyle(.secondary)
                Button("Aprender") { midi.learning = target }
            }
        }
    }
}

/// Banner flotante mientras se espera un control MIDI.
struct MIDILearnBanner: View {
    let midi: MIDIManager

    var body: some View {
        if let target = midi.learning {
            HStack(spacing: 10) {
                Image(systemName: "dot.radiowaves.left.and.right")
                    .symbolEffect(.pulse)
                Text("Mueve un control MIDI para asignarlo a \(target.displayName)")
                    .font(.system(size: 13, weight: .semibold))
                Button("Cancelar") { midi.learning = nil }
                    .buttonStyle(.bordered)
                    .controlSize(.small)
            }
            .padding(.horizontal, 14)
            .padding(.vertical, 10)
            .background(Capsule().fill(Theme.elevated))
            .overlay(Capsule().strokeBorder(Theme.accent2.opacity(0.6)))
            .shadow(color: .black.opacity(0.4), radius: 12, y: 4)
            .padding(.bottom, 16)
        }
    }
}

// MARK: - Atajos

struct ShortcutsReference: View {
    static let rows: [(String, String)] = [
        ("Espacio", "Reproducir / Detener (vuelve al cursor de edición)"),
        ("⌘R", "Grabar"),
        ("R", "Repetir (loop) on/off"),
        ("Inicio / Fin", "Ir al inicio / final del proyecto"),
        ("S", "Dividir ítems en el cursor"),
        ("⌘D", "Duplicar ítem"),
        ("⌫ / Supr", "Eliminar ítem"),
        ("⌘T", "Insertar pista"),
        ("M", "Insertar marcador"),
        ("1 … 9", "Ir al marcador 1 … 9"),
        ("[ / ]", "Sección / marcador anterior · siguiente"),
        ("Esc", "Quitar selección de tiempo"),
        ("+ / −", "Zoom horizontal"),
        ("← / →", "Mover el cursor un compás"),
        ("⌘Z / ⇧⌘Z", "Deshacer / Rehacer"),
        ("⌘M", "Mezclador"),
        ("⌘I", "Importar audio"),
        ("⌥⌘R", "Renderizar (exportar mezcla)"),
        ("⌘,", "Preferencias"),
        ("Arrastrar en la regla", "Selección de tiempo (el loop la sigue)"),
        ("Clic derecho en R", "Elegir la entrada de grabación de la pista"),
    ]

    var body: some View {
        List {
            Section {
                ForEach(Self.rows, id: \.0) { row in
                    HStack {
                        Text(row.0)
                            .font(Theme.mono(12, weight: .semibold))
                            .frame(width: 150, alignment: .leading)
                        Text(row.1)
                    }
                }
            } footer: {
                Text("Atajos por defecto de REAPER. En Mac, Ctrl de Windows es ⌘.")
            }
        }
    }
}
