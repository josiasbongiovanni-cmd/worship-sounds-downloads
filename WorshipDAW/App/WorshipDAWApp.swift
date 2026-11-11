import SwiftUI
import WorshipDAWCore

@main
struct WorshipDAWApp: App {
    @State private var app = AppModel()

    var body: some Scene {
        WindowGroup {
            RootView(app: app)
                #if os(macOS)
                .frame(minWidth: 1000, minHeight: 640)
                #endif
        }
        .commands {
            CommandGroup(replacing: .undoRedo) {
                Button("Deshacer") { app.session?.undo() }
                    .keyboardShortcut("z", modifiers: .command)
                    .disabled(app.session?.canUndo != true)
                Button("Rehacer") { app.session?.redo() }
                    .keyboardShortcut("z", modifiers: [.command, .shift])
                    .disabled(app.session?.canRedo != true)
            }
            CommandGroup(after: .newItem) {
                Button("Importar audio…") {
                    NotificationCenter.default.post(name: .worshipImportAudio, object: nil)
                }
                .keyboardShortcut("i", modifiers: .command)
                .disabled(app.session == nil)
                Button("Renderizar mezcla (WAV)…") { app.session?.exportMix() }
                    .keyboardShortcut("r", modifiers: [.command, .option])
                    .disabled(app.session == nil)
                Button("Guardar proyecto") { app.session?.saveNow() }
                    .keyboardShortcut("s", modifiers: .command)
                    .disabled(app.session == nil)
                Button("Cerrar proyecto") { app.closeSession() }
                    .keyboardShortcut("w", modifiers: [.command, .shift])
                    .disabled(app.session == nil)
            }
            #if os(macOS)
            // ⌘M abre el mezclador, como en REAPER (reemplaza "Minimizar").
            CommandGroup(replacing: .windowSize) {
                Button("Mezclador") {
                    NotificationCenter.default.post(name: .worshipToggleMixer, object: nil)
                }
                .keyboardShortcut("m", modifiers: .command)
            }
            #else
            CommandGroup(replacing: .appSettings) {
                Button("Preferencias…") {
                    NotificationCenter.default.post(name: .worshipShowPreferences, object: nil)
                }
                .keyboardShortcut(",", modifiers: .command)
            }
            #endif
            CommandMenu("Transporte") {
                Button("Reproducir / Detener   (Espacio)") { app.session?.togglePlay() }
                    .disabled(app.session == nil)
                Button("Grabar") { app.session?.toggleRecord() }
                    .keyboardShortcut("r", modifiers: .command)
                    .disabled(app.session == nil)
                Button("Repetir   (R)") { app.session?.toggleLoop() }
                    .disabled(app.session == nil)
                Button("Metrónomo") { app.session?.edit { $0.isClickEnabled.toggle() } }
                    .disabled(app.session == nil)
                Divider()
                Button("Ir al inicio   (Inicio)") { app.session?.returnToStart() }
                Button("Ir al final   (Fin)") { app.session?.goToEnd() }
                Button("Marcador anterior   ([)") { app.session?.goToPreviousMarker() }
                Button("Marcador siguiente   (])") { app.session?.goToNextMarker() }
                Button("Insertar marcador   (M)") { app.session?.insertMarker() }
            }
            CommandMenu("Pista") {
                Button("Insertar pista nueva") { app.session?.addTrack() }
                    .keyboardShortcut("t", modifiers: .command)
                    .disabled(app.session == nil)
                Button("Insertar pista de instrumento (MIDI)") { app.session?.addTrack(role: .instrument) }
                    .keyboardShortcut("t", modifiers: [.command, .shift])
                    .disabled(app.session == nil)
                Button("Armar / desarmar pista seleccionada") {
                    if let s = app.session, let id = s.selectedTrackID { s.updateTrack(id) { $0.isArmed.toggle() } }
                }
                .disabled(app.session?.selectedTrackID == nil)
            }
            CommandMenu("Ítem") {
                Button("Dividir en el cursor   (S)") { app.session?.splitAtPlayhead() }
                Button("Duplicar") { app.session?.duplicateSelection() }
                    .keyboardShortcut("d", modifiers: .command)
                Button("Eliminar   (⌫)") { app.session?.deleteSelection() }
                Divider()
                Button("Acercar   (+)") { app.session?.zoom(by: 1.4) }
                Button("Alejar   (−)") { app.session?.zoom(by: 1 / 1.4) }
            }
        }

        #if os(macOS)
        Settings {
            PreferencesView(app: app)
        }
        #endif
    }
}

struct RootView: View {
    @Bindable var app: AppModel
    @Environment(\.scenePhase) private var scenePhase

    var body: some View {
        Group {
            if let session = app.session {
                EditorView(app: app, session: session)
                    .id(session.url)
                    .transition(.opacity)
            } else {
                HomeView(app: app)
                    .transition(.opacity)
            }
        }
        .animation(.easeInOut(duration: 0.2), value: app.session?.url)
        .preferredColorScheme(.dark)
        .tint(Theme.accent)
        .alert("Error", isPresented: Binding(
            get: { app.errorMessage != nil },
            set: { if !$0 { app.errorMessage = nil } })) {
            Button("OK", role: .cancel) {}
        } message: {
            Text(app.errorMessage ?? "")
        }
        .onChange(of: scenePhase) { _, phase in
            if phase != .active { app.session?.saveNow() }
        }
    }
}
