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
                Button("Exportar mezcla (WAV)") { app.session?.exportMix() }
                    .keyboardShortcut("e", modifiers: .command)
                    .disabled(app.session == nil)
                Button("Guardar") { app.session?.saveNow() }
                    .keyboardShortcut("s", modifiers: .command)
                    .disabled(app.session == nil)
                Button("Cerrar proyecto") { app.closeSession() }
                    .keyboardShortcut("w", modifiers: [.command, .shift])
                    .disabled(app.session == nil)
            }
            CommandMenu("Transporte") {
                Button("Reproducir / Detener  (Espacio)") { app.session?.togglePlay() }
                Button("Grabar  (R)") { app.session?.toggleRecord() }
                Button("Volver al inicio") { app.session?.returnToStart() }
                    .keyboardShortcut(.home, modifiers: .command)
                Divider()
                Button("Sección anterior  ([)") { app.session?.jumpToPreviousSection() }
                Button("Sección siguiente  (])") { app.session?.jumpToNextSection() }
                Button("Loop  (L)") { app.session?.toggleLoop() }
                Button("Click  (K)") { app.session?.edit { $0.isClickEnabled.toggle() } }
            }
            CommandMenu("Editar clips") {
                Button("Dividir en el cursor  (S)") { app.session?.splitAtPlayhead() }
                Button("Duplicar") { app.session?.duplicateSelection() }
                    .keyboardShortcut("d", modifiers: .command)
                Button("Eliminar  (⌫)") { app.session?.deleteSelection() }
                Divider()
                Button("Acercar") { app.session?.zoom(by: 1.4) }
                    .keyboardShortcut("+", modifiers: .command)
                Button("Alejar") { app.session?.zoom(by: 1 / 1.4) }
                    .keyboardShortcut("-", modifiers: .command)
                Button("Mostrar mezclador  (M)") {
                    NotificationCenter.default.post(name: .worshipToggleMixer, object: nil)
                }
            }
        }
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
