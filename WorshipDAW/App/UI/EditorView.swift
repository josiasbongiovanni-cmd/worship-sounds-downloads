import SwiftUI
import UniformTypeIdentifiers
import WorshipDAWCore

/// Editor de proyecto. En Mac/iPad: arreglo + mezclador + inspector.
/// En iPhone: transporte compacto y pestañas (Arreglo / Mezcla / En vivo / Canción).
struct EditorView: View {
    @Bindable var app: AppModel
    @Bindable var session: Session

    @State private var showMixer = false
    @State private var showInspector = true
    @State private var showImporter = false
    @State private var compactTab: CompactTab = .arrange
    @State private var inspectorSheet = false
    @State private var showPreferences = false
    @FocusState private var editorFocused: Bool

    #if os(iOS)
    @Environment(\.horizontalSizeClass) private var sizeClass
    private var isCompact: Bool { sizeClass == .compact }
    #else
    private let isCompact = false
    #endif

    enum CompactTab: String, CaseIterable {
        case arrange = "Arreglo"
        case mixer = "Mezcla"
        case live = "En vivo"
        case song = "Canción"

        var symbol: String {
            switch self {
            case .arrange: "rectangle.split.3x1"
            case .mixer: "slider.vertical.3"
            case .live: "square.grid.2x2.fill"
            case .song: "music.note.list"
            }
        }
    }

    var body: some View {
        Group {
            if isCompact { compactLayout } else { regularLayout }
        }
        .background(Theme.background)
        .preferredColorScheme(.dark)
        .tint(Theme.accent)
        .focusable()
        .focusEffectDisabled()
        .focused($editorFocused)
        .onAppear { editorFocused = true }
        .onKeyPress(phases: .down) { press in handleKey(press) }
        .fileImporter(isPresented: $showImporter, allowedContentTypes: [.audio], allowsMultipleSelection: true) { result in
            if case .success(let urls) = result {
                session.importAudio(urls: urls)
            }
        }
        .alert("Atención", isPresented: Binding(
            get: { session.errorMessage != nil },
            set: { if !$0 { session.errorMessage = nil } })) {
            Button("OK", role: .cancel) {}
        } message: {
            Text(session.errorMessage ?? "")
        }
        .onReceive(NotificationCenter.default.publisher(for: .worshipImportAudio)) { _ in showImporter = true }
        .onReceive(NotificationCenter.default.publisher(for: .worshipToggleMixer)) { _ in
            withAnimation(.snappy) { showMixer.toggle() }
        }
        .onReceive(NotificationCenter.default.publisher(for: .worshipShowPreferences)) { _ in showPreferences = true }
        .onChange(of: session.selectedTrackID) { _, _ in session.refreshLiveMIDI() }
        .sheet(item: Binding(get: { session.editingMIDIClipID.map(IdentifiedUUID.init) },
                             set: { session.editingMIDIClipID = $0?.id })) { item in
            PianoRollView(session: session, clipID: item.id)
        }
        .overlay(alignment: .bottom) { MIDILearnBanner(midi: app.midi) }
        .sheet(isPresented: $showPreferences) {
            NavigationStack {
                PreferencesView(app: app)
                    .toolbar {
                        ToolbarItem(placement: .confirmationAction) { Button("Listo") { showPreferences = false } }
                    }
            }
        }
    }

    // MARK: Mac / iPad

    private var regularLayout: some View {
        VStack(spacing: 0) {
            TransportBar(session: session, showMixer: $showMixer, showInspector: $showInspector,
                         onClose: { app.closeSession() }, onImport: { showImporter = true })
            HStack(spacing: 0) {
                VStack(spacing: 0) {
                    ArrangementView(session: session, onShowTrackInspector: { _ in
                        withAnimation(.snappy) { showInspector = true }
                    })
                    if showMixer {
                        MixerView(session: session, midi: app.midi)
                            .frame(height: 470)
                            .overlay(alignment: .top) { Rectangle().fill(Theme.strokeStrong).frame(height: 1) }
                            .transition(.move(edge: .bottom).combined(with: .opacity))
                    }
                }
                if showInspector {
                    InspectorView(session: session)
                        .frame(width: 300)
                        .transition(.move(edge: .trailing).combined(with: .opacity))
                }
            }
        }
    }

    // MARK: iPhone

    private var compactLayout: some View {
        VStack(spacing: 0) {
            CompactTransportBar(session: session, onClose: { app.closeSession() }, onImport: { showImporter = true })
            Group {
                switch compactTab {
                case .arrange:
                    ArrangementView(session: session, headerWidth: 128, trackHeight: 64, compact: true,
                                    onShowTrackInspector: { _ in inspectorSheet = true })
                case .mixer:
                    MixerView(session: session, midi: app.midi)
                case .live:
                    LivePadsView(session: session)
                case .song:
                    ScrollView {
                        VStack(alignment: .leading, spacing: 24) {
                            SongSettingsForm(session: session)
                            Divider()
                            StructureEditor(session: session)
                        }
                        .padding(16)
                    }
                }
            }
            .frame(maxHeight: .infinity)
            compactTabBar
        }
        .sheet(isPresented: $inspectorSheet) {
            NavigationStack {
                ScrollView { TrackInspector(session: session).padding(16) }
                    .navigationTitle("Pista")
                    .toolbar {
                        ToolbarItem(placement: .confirmationAction) { Button("Listo") { inspectorSheet = false } }
                    }
            }
            .presentationDetents([.medium, .large])
            .preferredColorScheme(.dark)
        }
    }

    private var compactTabBar: some View {
        HStack(spacing: 0) {
            ForEach(CompactTab.allCases, id: \.self) { tab in
                Button {
                    withAnimation(.snappy) { compactTab = tab }
                } label: {
                    VStack(spacing: 3) {
                        Image(systemName: tab.symbol)
                            .font(.system(size: 17, weight: .semibold))
                        Text(tab.rawValue)
                            .font(.system(size: 10, weight: .semibold))
                    }
                    .frame(maxWidth: .infinity)
                    .padding(.vertical, 8)
                    .foregroundStyle(compactTab == tab ? Theme.accent : Theme.textTertiary)
                }
                .buttonStyle(.plain)
            }
        }
        .padding(.horizontal, 8)
        .background(Theme.panel.ignoresSafeArea(edges: .bottom))
        .overlay(alignment: .top) { Rectangle().fill(Theme.stroke).frame(height: 1) }
    }

    // MARK: Atajos de teclado por defecto de REAPER (teclas sin ⌘; los de ⌘ están en los menús)

    private func handleKey(_ press: KeyPress) -> KeyPress.Result {
        guard press.modifiers.isEmpty || press.modifiers == .shift else { return .ignored }
        let timeline = session.project.timeline
        switch press.key {
        case .space:
            session.togglePlay()
            return .handled
        case .home:
            session.returnToStart()
            return .handled
        case .end:
            session.goToEnd()
            return .handled
        case .delete, .deleteForward:
            session.deleteSelection()
            return .handled
        case .escape:
            session.clearTimeSelection()
            session.selectedClipID = nil
            return .handled
        case .leftArrow:
            session.seek(to: max(0, session.editCursor - timeline.secondsPerBar))
            return .handled
        case .rightArrow:
            session.seek(to: session.editCursor + timeline.secondsPerBar)
            return .handled
        default:
            break
        }
        let ch = press.characters.lowercased()
        if let digit = Int(ch), (1...9).contains(digit) {
            session.goToMarker(digit)
            return .handled
        }
        switch ch {
        case "r": session.toggleLoop()            // Repetir
        case "s": session.splitAtPlayhead()       // Dividir
        case "m": session.insertMarker()          // Marcador
        case "[": session.goToPreviousMarker()
        case "]": session.goToNextMarker()
        case "+", "=": session.zoom(by: 1.4)
        case "-": session.zoom(by: 1 / 1.4)
        default: return .ignored
        }
        return .handled
    }
}

extension Notification.Name {
    static let worshipImportAudio = Notification.Name("worship.importAudio")
    static let worshipToggleMixer = Notification.Name("worship.toggleMixer")
    static let worshipShowPreferences = Notification.Name("worship.showPreferences")
}
