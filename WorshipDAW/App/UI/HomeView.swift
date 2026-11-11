import SwiftUI
import WorshipDAWCore

/// Pantalla de inicio: proyectos recientes + crear desde plantilla.
struct HomeView: View {
    @Bindable var app: AppModel
    @State private var showNew = false

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 28) {
                header
                VStack(alignment: .leading, spacing: 12) {
                    Text("Empezar")
                        .font(Theme.display(13, weight: .bold))
                        .foregroundStyle(Theme.textSecondary)
                    LazyVGrid(columns: [GridItem(.adaptive(minimum: 200), spacing: 12)], spacing: 12) {
                        ForEach(ProjectTemplate.allCases) { template in
                            TemplateCard(template: template) {
                                app.create(name: "Nueva canción", template: template, bpm: 72, key: "G", beatsPerBar: 4)
                            }
                        }
                    }
                }
                VStack(alignment: .leading, spacing: 12) {
                    HStack {
                        Text("Proyectos")
                            .font(Theme.display(13, weight: .bold))
                            .foregroundStyle(Theme.textSecondary)
                        Spacer()
                        Button { app.refresh() } label: { Image(systemName: "arrow.clockwise") }
                            .buttonStyle(.plain)
                            .foregroundStyle(Theme.textSecondary)
                    }
                    if app.entries.isEmpty {
                        Text("Todavía no hay proyectos. Crea uno desde una plantilla.")
                            .font(.system(size: 13))
                            .foregroundStyle(Theme.textTertiary)
                            .padding(.vertical, 20)
                    } else {
                        LazyVGrid(columns: [GridItem(.adaptive(minimum: 240), spacing: 12)], spacing: 12) {
                            ForEach(app.entries) { entry in
                                ProjectCard(entry: entry) { app.open(entry.url) }
                                    .contextMenu {
                                        Button { app.duplicate(entry) } label: { Label("Duplicar", systemImage: "plus.square.on.square") }
                                        Button(role: .destructive) { app.delete(entry) } label: { Label("Eliminar", systemImage: "trash") }
                                    }
                            }
                        }
                    }
                }
            }
            .padding(24)
            .frame(maxWidth: 1100)
            .frame(maxWidth: .infinity)
        }
        .background(
            ZStack {
                Theme.background
                RadialGradient(colors: [Theme.accent.opacity(0.22), .clear], center: .topLeading, startRadius: 0, endRadius: 600)
                RadialGradient(colors: [Theme.accent2.opacity(0.12), .clear], center: .topTrailing, startRadius: 0, endRadius: 500)
            }
            .ignoresSafeArea()
        )
        .sheet(isPresented: $showNew) {
            NewProjectSheet(app: app)
        }
    }

    private var header: some View {
        HStack(alignment: .center, spacing: 14) {
            ZStack {
                RoundedRectangle(cornerRadius: 14, style: .continuous)
                    .fill(Theme.accentGradient)
                    .frame(width: 52, height: 52)
                    .shadow(color: Theme.accent.opacity(0.6), radius: 14)
                Image(systemName: "waveform")
                    .font(.system(size: 24, weight: .bold))
                    .foregroundStyle(.white)
            }
            VStack(alignment: .leading, spacing: 3) {
                Text("Worship DAW")
                    .font(Theme.display(28, weight: .heavy))
                Text("Producción para equipos de alabanza")
                    .font(.system(size: 13, weight: .medium))
                    .foregroundStyle(Theme.textSecondary)
            }
            Spacer()
            Button {
                showNew = true
            } label: {
                Label("Nuevo proyecto", systemImage: "plus")
                    .font(.system(size: 13, weight: .bold))
                    .padding(.horizontal, 14)
                    .padding(.vertical, 9)
                    .background(Capsule().fill(Theme.accentGradient))
                    .foregroundStyle(.white)
            }
            .buttonStyle(.plain)
            .keyboardShortcut("n", modifiers: .command)
        }
    }
}

struct TemplateCard: View {
    let template: ProjectTemplate
    let action: () -> Void
    @State private var hovering = false

    var body: some View {
        Button(action: action) {
            VStack(alignment: .leading, spacing: 10) {
                Image(systemName: template.symbolName)
                    .font(.system(size: 20, weight: .semibold))
                    .foregroundStyle(Theme.accentGradient)
                Text(template.displayName)
                    .font(Theme.display(15, weight: .bold))
                    .foregroundStyle(Theme.textPrimary)
                Text(template.subtitle)
                    .font(.system(size: 12))
                    .foregroundStyle(Theme.textSecondary)
                    .lineLimit(2)
                    .multilineTextAlignment(.leading)
            }
            .frame(maxWidth: .infinity, minHeight: 110, alignment: .topLeading)
            .padding(14)
            .background(
                RoundedRectangle(cornerRadius: 16, style: .continuous)
                    .fill(hovering ? Theme.elevated : Theme.panel)
            )
            .overlay(
                RoundedRectangle(cornerRadius: 16, style: .continuous)
                    .strokeBorder(hovering ? Theme.accent.opacity(0.5) : Theme.stroke)
            )
        }
        .buttonStyle(.plain)
        .onHover { hovering = $0 }
        .animation(.easeOut(duration: 0.15), value: hovering)
    }
}

struct ProjectCard: View {
    let entry: ProjectStore.Entry
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            HStack(spacing: 12) {
                RoundedRectangle(cornerRadius: 10, style: .continuous)
                    .fill(LinearGradient(colors: [Theme.accent.opacity(0.5), Theme.accent2.opacity(0.3)], startPoint: .topLeading, endPoint: .bottomTrailing))
                    .frame(width: 46, height: 46)
                    .overlay(Text(entry.key).font(Theme.display(14, weight: .heavy)).foregroundStyle(.white))
                VStack(alignment: .leading, spacing: 3) {
                    Text(entry.name)
                        .font(Theme.display(14, weight: .bold))
                        .foregroundStyle(Theme.textPrimary)
                        .lineLimit(1)
                    Text("\(Int(entry.bpm)) BPM · \(entry.trackCount) pistas")
                        .font(.system(size: 11))
                        .foregroundStyle(Theme.textSecondary)
                    Text(entry.modifiedAt, style: .relative)
                        .font(.system(size: 10))
                        .foregroundStyle(Theme.textTertiary)
                }
                Spacer(minLength: 0)
            }
            .padding(12)
            .panel(corner: 14)
        }
        .buttonStyle(.plain)
    }
}

struct NewProjectSheet: View {
    @Bindable var app: AppModel
    @Environment(\.dismiss) private var dismiss
    @State private var name = ""
    @State private var template: ProjectTemplate = .fullBand
    @State private var bpm: Double = 72
    @State private var key = "G"
    @State private var beatsPerBar = 4

    var body: some View {
        NavigationStack {
            Form {
                Section("Canción") {
                    TextField("Nombre (ej. Gracia Sublime)", text: $name)
                    Stepper(value: $bpm, in: 20...300, step: 1) {
                        HStack {
                            Text("Tempo")
                            Spacer()
                            Text("\(Int(bpm)) BPM").monospacedDigit().foregroundStyle(.secondary)
                        }
                    }
                    Picker("Tonalidad", selection: $key) {
                        ForEach(MusicalKey.all, id: \.self) { Text($0).tag($0) }
                    }
                    Picker("Compás", selection: $beatsPerBar) {
                        ForEach([2, 3, 4, 6], id: \.self) { Text("\($0)/4").tag($0) }
                    }
                }
                Section("Plantilla") {
                    ForEach(ProjectTemplate.allCases) { t in
                        Button { template = t } label: {
                            HStack {
                                Image(systemName: t.symbolName).frame(width: 24)
                                VStack(alignment: .leading) {
                                    Text(t.displayName)
                                    Text(t.subtitle).font(.caption).foregroundStyle(.secondary)
                                }
                                Spacer()
                                if template == t { Image(systemName: "checkmark").foregroundStyle(Theme.accent) }
                            }
                            .contentShape(Rectangle())
                        }
                        .buttonStyle(.plain)
                    }
                }
            }
            .formStyle(.grouped)
            .navigationTitle("Nuevo proyecto")
            .toolbar {
                ToolbarItem(placement: .cancellationAction) { Button("Cancelar") { dismiss() } }
                ToolbarItem(placement: .confirmationAction) {
                    Button("Crear") {
                        app.create(name: name, template: template, bpm: bpm, key: key, beatsPerBar: beatsPerBar)
                        dismiss()
                    }
                }
            }
        }
        .macSheetFrame(width: 440, height: 560)
    }
}
