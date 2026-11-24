#include "MainComponent.h"
#include "Model/Serialization.h"

namespace wdaw::ui
{

MainComponent::MainComponent()
{
    pluginManager.loadKnownList();
    auto deviceXml = settings.file().getXmlValue ("audioDevice");
    if (auto err = engine.openDevices (deviceXml.get()); err.isNotEmpty())
        juce::MessageManager::callAsync ([this, err] { showError ("No se pudo abrir el dispositivo de audio: " + err + "\nElige otro en Preferencias › Audio."_u); });
    engine.setUseReportedLatency (settings.file().getBoolValue ("useReportedLatency", true));
    engine.setRecordingOffsetMs (settings.file().getDoubleValue ("recordOffsetMs", 0));
    engine.refreshMidiInputs (juce::StringArray::fromTokens (settings.file().getValue ("midiDisabled"), "|", ""));
    engine.midiFilter = [this] (const juce::MidiMessage& m) { return midiLearn.filter (m); };
    engine.onError = [this] (juce::String e) { showError (e); };
    midiLearn.onAction = [this] (const MidiMapping& m, float v) { if (session) session->handleMidiAction (m, v); };
    midiLearn.onLearned = [this] { if (session) session->sendChangeMessage(); };

    home = std::make_unique<HomeView> (settings);
    home->onOpen = [this] (const juce::File& f) { openProject (f); };
    home->onCreate = [this] (auto n, auto t, auto b, auto k, auto m) { createProject (n, t, b, k, m); };
    home->onPreferences = [this] { PreferencesPanel::show (engine, pluginManager, midiLearn, settings); };
    addAndMakeVisible (*home);

    commands.registerAllCommandsForTarget (this);
    commands.setFirstCommandTarget (this);
    meters.onUpdate = [this] { if (mixer != nullptr && mixerVisible) mixer->repaintMeters(); };
    setSize (1440, 900);
    startTimer (1000);
}

MainComponent::~MainComponent()
{
#if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu (nullptr);
#endif
    pluginWindows.clear();
    pianoRollWindow.reset();
    closeProject();
    if (auto xml = engine.devices.createStateXml()) settings.file().setValue ("audioDevice", xml.get());
    settings.props.saveIfNeeded();
}

void MainComponent::parentHierarchyChanged()
{
    if (auto* top = getTopLevelComponent())
    {
        top->removeKeyListener (commands.getKeyMappings());
        top->addKeyListener (commands.getKeyMappings());   // atajos para toda la ventana, tenga el foco quien lo tenga
    }
}

void MainComponent::paint (juce::Graphics& g) { g.fillAll (colours::background); }

void MainComponent::resized()
{
    auto r = getLocalBounds();
    if (session == nullptr) { if (home) home->setBounds (r); return; }
    transport->setBounds (r.removeFromTop (70));
    if (inspectorVisible) inspector->setBounds (r.removeFromRight (300));
    inspector->setVisible (inspectorVisible);
    if (mixerVisible) mixer->setBounds (r.removeFromBottom (juce::jlimit (300, 500, r.getHeight() / 2)));
    mixer->setVisible (mixerVisible);
    arrangement->setBounds (r);
}

void MainComponent::showError (const juce::String& message)
{
    juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::WarningIcon)
                                      .withTitle (juce::String::fromUTF8 ("Atención")).withMessage (message).withButton ("OK"), nullptr);
}

//==============================================================================
void MainComponent::createProject (juce::String name, edit::Template t, double bpm, juce::String key, int beats)
{
    if (name.isEmpty()) name = "Nueva canción"_u;
    auto project = edit::makeProject (t, name, bpm, key, beats);
    for (auto& tr : project.tracks)
        if (tr.isInstrument() && ! tr.instrument)
            tr.instrument = PluginManager::internalSlot (tr.name.containsIgnoreCase ("pad") ? "pad" : "piano");
    auto folder = io::createProjectFolder (settings.projectsFolder(), name);
    if (auto r = io::save (project, folder); r.failed()) { showError (r.getErrorMessage()); return; }
    openProject (folder);
}

void MainComponent::openProject (const juce::File& folder)
{
    juce::String error;
    auto project = io::load (folder, &error);
    if (! project) { showError ("No se pudo abrir el proyecto: " + error); return; }
    closeProject();
    session = std::make_unique<Session> (engine, pluginManager, midiLearn, folder, *project);
    session->showError = [this] (juce::String e) { showError (e); };
    session->openMidiEditor = [this] { openPianoRoll(); };
    if (error.isNotEmpty()) showError (error);
    buildEditor();
}

void MainComponent::buildEditor()
{
    ctx = std::make_unique<Context> (Context { *session, meters, thumbnails });
    paramWatcher = std::make_unique<ParamWatcher> (*session);
    ctx->openPluginWindow = [this] (const juce::Uuid& t, const juce::Uuid& s) { openPluginWindow (t, s); };
    ctx->choosePlugin = [this] (const juce::Uuid& t, bool inst) { choosePluginFor (t, inst); };
    ctx->addMasterPlugin = [this] { choosePluginFor (juce::Uuid::null(), false); };
    ctx->openPianoRoll = [this] { openPianoRoll(); };
    ctx->askText = [] (const juce::String& t, const juce::String& c, std::function<void (juce::String)> d) { askText (t, c, std::move (d)); };

    home->setVisible (false);
    transport = std::make_unique<TransportBar> (*ctx);
    transport->onHome = [this] { closeProject(); };
    transport->onImport = [this] { importDialog(); };
    transport->onExport = [this] { if (session) showExportDialog (*session, settings); };
    transport->onPreferences = [this] { PreferencesPanel::show (engine, pluginManager, midiLearn, settings); };
    transport->onToggleMixer = [this] { showMixer (! mixerVisible); };
    transport->onToggleInspector = [this] { inspectorVisible = ! inspectorVisible; transport->inspectorVisible = inspectorVisible; transport->update(); resized(); };
    arrangement = std::make_unique<Arrangement> (*ctx);
    mixer = std::make_unique<Mixer> (*ctx);
    inspector = std::make_unique<Inspector> (*ctx);
    for (auto* c : std::initializer_list<juce::Component*> { transport.get(), arrangement.get(), mixer.get(), inspector.get() })
        addAndMakeVisible (c);
    transport->mixerVisible = mixerVisible;
    transport->inspectorVisible = inspectorVisible;
    transport->update();
    if (auto* w = findParentComponentOfClass<juce::DocumentWindow>()) w->setName (session->project().name + " - Worship DAW");
    resized();
}

void MainComponent::closeProject()
{
    if (session == nullptr) return;
    pianoRollWindow.reset();
    pluginWindows.clear();
    arrangement.reset();
    mixer.reset();
    inspector.reset();
    transport.reset();
    ctx.reset();
    paramWatcher.reset();
    session.reset();          // guarda (con el estado de los plugins) al cerrarse
    if (home) { home->refresh(); home->setVisible (true); }
    if (auto* w = findParentComponentOfClass<juce::DocumentWindow>()) w->setName ("Worship DAW");
    resized();
}

void MainComponent::showMixer (bool show)
{
    mixerVisible = show;
    if (transport) { transport->mixerVisible = show; transport->update(); }
    resized();
}

//==============================================================================
void MainComponent::openPluginWindow (const juce::Uuid& trackId, const juce::Uuid& slotId)
{
    juce::ignoreUnused (trackId);
    if (auto it = pluginWindows.find (slotId); it != pluginWindows.end()) { it->second->toFront (true); return; }
    auto inst = engine.findPlugin (slotId);
    if (inst == nullptr) { showError (juce::String::fromUTF8 ("El plugin todavía se está cargando o no está disponible en esta computadora.")); return; }
    auto title = inst->name;
    if (session != nullptr)
        if (auto* t = session->project().findTrack (trackId)) title = t->name + " - " + inst->name;
    pluginWindows[slotId] = std::make_unique<PluginWindow> (inst, title, [this, slotId]
    {
        juce::MessageManager::callAsync ([this, slotId] { pluginWindows.erase (slotId); });
    });
}

void MainComponent::choosePluginFor (const juce::Uuid& trackId, bool instrument)
{
    if (session == nullptr) return;
    choosePlugin (pluginManager, instrument, nullptr, [this, trackId, instrument] (PluginSlot slot)
    {
        if (session == nullptr) return;
        const auto id = slot.id;
        if (trackId.isNull())
            session->edit ([slot] (Project& p) { p.masterInserts.push_back (slot); });
        else if (instrument)
            session->updateTrack (trackId, [slot] (Track& t) { t.instrument = slot; t.role = TrackRole::instrument; });
        else
            session->updateTrack (trackId, [slot] (Track& t) { t.inserts.push_back (slot); });
        // Abrir la ventana del plugin cuando termine de cargar
        juce::Timer::callAfterDelay (400, [this, trackId, id] { if (session && engine.findPlugin (id)) openPluginWindow (trackId, id); });
    },
    [this] { PreferencesPanel::show (engine, pluginManager, midiLearn, settings, 2); });
}

void MainComponent::openPianoRoll()
{
    if (session == nullptr || session->editingMidiClip.isNull()) return;
    pianoRollWindow.reset();
    struct Window : juce::DocumentWindow
    {
        Window (std::function<void()> c) : juce::DocumentWindow ("Editor MIDI", colours::background, allButtons), onClose (std::move (c)) {}
        void closeButtonPressed() override { onClose(); }
        std::function<void()> onClose;
    };
    auto* w = new Window ([this] { juce::MessageManager::callAsync ([this] { pianoRollWindow.reset(); }); });
    w->setUsingNativeTitleBar (true);
    w->setContentOwned (new PianoRoll (*ctx), false);
    w->setResizable (true, true);
    w->centreWithSize (juce::jmin (1200, getWidth() - 80), juce::jmin (760, getHeight() - 80));
    w->addKeyListener (commands.getKeyMappings());
    w->setVisible (true);
    pianoRollWindow.reset (w);
    w->getContentComponent()->grabKeyboardFocus();
}

void MainComponent::openPianoRollForFirstMidiClip()
{
    if (session == nullptr) return;
    for (auto& t : session->project().tracks)
        if (! t.midiClips.empty()) { session->editingMidiClip = t.midiClips.front().id; openPianoRoll(); return; }
}

void MainComponent::importDialog()
{
    if (session == nullptr) return;
    auto chooser = std::make_shared<juce::FileChooser> ("Importar audio", juce::File::getSpecialLocation (juce::File::userMusicDirectory), importWildcard());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
                          [this, chooser] (const juce::FileChooser& fc)
    {
        if (session != nullptr && ! fc.getResults().isEmpty()) session->importFiles (fc.getResults());
    });
}

void MainComponent::timerCallback()
{
    // Conectar controladores MIDI enchufados mientras la app está abierta
    engine.refreshMidiInputs (juce::StringArray::fromTokens (settings.file().getValue ("midiDisabled"), "|", ""));
}

//==============================================================================
void MainComponent::getAllCommands (juce::Array<juce::CommandID>& c)
{
    for (juce::CommandID id = cmd::playStop; id <= cmd::marker9; ++id) c.add (id);
}

void MainComponent::getCommandInfo (juce::CommandID id, juce::ApplicationCommandInfo& info)
{
    using KP = juce::KeyPress;
    const auto cmdKey = juce::ModifierKeys::commandModifier;
    const bool open = session != nullptr;
    auto set = [&] (const juce::String& name, const char* category, bool enabled = true) { info.setInfo (name, name, category, 0); info.setActive (enabled); };
    switch (id)
    {
        case cmd::playStop:     set ("Reproducir / Detener", "Transporte", open); info.addDefaultKeypress (KP::spaceKey, 0); break;
        case cmd::record:       set ("Grabar", "Transporte", open); info.addDefaultKeypress ('r', cmdKey); break;
        case cmd::repeat:       set ("Repetir", "Transporte", open); info.addDefaultKeypress ('r', 0); info.setTicked (open && session->project().loopEnabled); break;
        case cmd::toStart:      set ("Ir al inicio", "Transporte", open); info.addDefaultKeypress (KP::homeKey, 0); break;
        case cmd::toEnd:        set ("Ir al final", "Transporte", open); info.addDefaultKeypress (KP::endKey, 0); break;
        case cmd::prevStop:     set ("Marcador o sección anterior"_u, "Transporte", open); info.addDefaultKeypress ('[', 0); break;
        case cmd::nextStop:     set ("Marcador o sección siguiente"_u, "Transporte", open); info.addDefaultKeypress (']', 0); break;
        case cmd::insertMarker: set ("Insertar marcador", "Transporte", open); info.addDefaultKeypress ('m', 0); break;
        case cmd::metronome:    set ("Metrónomo"_u, "Transporte", open); info.setTicked (open && session->project().clickEnabled); break;
        case cmd::panic:        set ("Pánico (apagar todas las notas)"_u, "Transporte"); info.addDefaultKeypress (KP::escapeKey, juce::ModifierKeys::shiftModifier); break;
        case cmd::cursorLeft:   set ("Cursor un compás atrás"_u, "Transporte", open); info.addDefaultKeypress (KP::leftKey, 0); break;
        case cmd::cursorRight:  set ("Cursor un compás adelante"_u, "Transporte", open); info.addDefaultKeypress (KP::rightKey, 0); break;
        case cmd::split:        set ("Dividir en el cursor", "Editar", open); info.addDefaultKeypress ('s', 0); break;
        case cmd::deleteItems:  set ("Eliminar", "Editar", open); info.addDefaultKeypress (KP::deleteKey, 0); info.addDefaultKeypress (KP::backspaceKey, 0); break;
        case cmd::duplicate:    set ("Duplicar", "Editar", open); info.addDefaultKeypress ('d', cmdKey); break;
        case cmd::copy:         set ("Copiar", "Editar", open); info.addDefaultKeypress ('c', cmdKey); break;
        case cmd::cut:          set ("Cortar", "Editar", open); info.addDefaultKeypress ('x', cmdKey); break;
        case cmd::paste:        set ("Pegar", "Editar", open); info.addDefaultKeypress ('v', cmdKey); break;
        case cmd::selectAll:    set ("Seleccionar todo", "Editar", open); info.addDefaultKeypress ('a', cmdKey); break;
        case cmd::undo:         set ("Deshacer", "Editar", open && session->canUndo()); info.addDefaultKeypress ('z', cmdKey); break;
        case cmd::redo:         set ("Rehacer", "Editar", open && session->canRedo()); info.addDefaultKeypress ('z', cmdKey | juce::ModifierKeys::shiftModifier); break;
        case cmd::clearSelection: set ("Quitar selección"_u, "Editar", open); info.addDefaultKeypress (KP::escapeKey, 0); break;
        case cmd::insertTrack:  set ("Insertar pista", "Pista", open); info.addDefaultKeypress ('t', cmdKey); break;
        case cmd::insertInstrumentTrack: set ("Insertar pista de instrumento (MIDI)", "Pista", open); info.addDefaultKeypress ('t', cmdKey | juce::ModifierKeys::shiftModifier); break;
        case cmd::zoomIn:       set ("Acercar", "Ver", open); info.addDefaultKeypress ('=', 0); info.addDefaultKeypress ('+', 0); break;
        case cmd::zoomOut:      set ("Alejar", "Ver", open); info.addDefaultKeypress ('-', 0); break;
        case cmd::toggleMixer:  set ("Mezclador", "Ver", open); info.addDefaultKeypress ('m', cmdKey); info.setTicked (mixerVisible); break;
        case cmd::render:       set ("Renderizar…"_u, "Archivo", open); info.addDefaultKeypress ('r', cmdKey | juce::ModifierKeys::altModifier); break;
        case cmd::importAudio:  set ("Importar audio…"_u, "Archivo", open); info.addDefaultKeypress ('i', cmdKey); break;
        case cmd::save:         set ("Guardar", "Archivo", open); info.addDefaultKeypress ('s', cmdKey); break;
        case cmd::preferences:  set ("Preferencias…"_u, "Archivo"); info.addDefaultKeypress (',', cmdKey); break;
        case cmd::closeProject: set ("Cerrar proyecto", "Archivo", open); info.addDefaultKeypress ('w', cmdKey | juce::ModifierKeys::shiftModifier); break;
        case cmd::newProject:   set ("Nuevo proyecto…"_u, "Archivo"); info.addDefaultKeypress ('n', cmdKey); break;
        case cmd::openProject:  set ("Proyectos…"_u, "Archivo"); info.addDefaultKeypress ('o', cmdKey); break;
        case cmd::cleanProject: set ("Limpiar proyecto (audio sin usar)…"_u, "Archivo", open); break;
        default:
            if (id >= cmd::marker1 && id <= cmd::marker9)
            {
                const int n = (int) (id - cmd::marker1) + 1;
                set ("Ir al marcador " + juce::String (n), "Transporte", open);
                info.addDefaultKeypress ('0' + n, 0);
            }
            break;
    }
}

bool MainComponent::perform (const InvocationInfo& info)
{
    auto id = info.commandID;
    if (id == cmd::preferences) { PreferencesPanel::show (engine, pluginManager, midiLearn, settings); return true; }
    if (id == cmd::newProject)
    {
        showNewProjectDialog ([this] (auto n, auto t, auto b, auto k, auto m) { createProject (n, t, b, k, m); });
        return true;
    }
    if (id == cmd::openProject) { closeProject(); return true; }
    if (id == cmd::panic) { engine.panic(); return true; }
    if (session == nullptr) return false;
    auto& s = *session;
    const auto tl = s.timeline();
    switch (id)
    {
        case cmd::playStop:     s.togglePlay(); break;
        case cmd::record:       s.toggleRecord(); break;
        case cmd::repeat:       s.toggleRepeat(); break;
        case cmd::toStart:      s.goToStart(); break;
        case cmd::toEnd:        s.goToEnd(); break;
        case cmd::prevStop:     s.goToPreviousStop(); break;
        case cmd::nextStop:     s.goToNextStop(); break;
        case cmd::insertMarker: s.insertMarker(); break;
        case cmd::metronome:    s.edit ([] (Project& p) { p.clickEnabled = ! p.clickEnabled; }); break;
        case cmd::cursorLeft:   s.seek (juce::jmax (0.0, s.editCursor - tl.beatsPerBar)); break;
        case cmd::cursorRight:  s.seek (s.editCursor + tl.beatsPerBar); break;
        case cmd::split:        s.splitAtCursor(); break;
        case cmd::deleteItems:  s.deleteSelection(); break;
        case cmd::duplicate:    s.duplicateSelection(); break;
        case cmd::copy:         s.copySelection(); break;
        case cmd::cut:          s.cutSelection(); break;
        case cmd::paste:        s.paste(); break;
        case cmd::selectAll:    s.selectAllClips(); break;
        case cmd::undo:         s.undo(); break;
        case cmd::redo:         s.redo(); break;
        case cmd::clearSelection: s.clearClipSelection(); s.clearTimeSelection(); s.midiLearn.cancelLearning(); break;
        case cmd::insertTrack:  s.addTrack (TrackRole::audio); break;
        case cmd::insertInstrumentTrack: s.addTrack (TrackRole::instrument); break;
        case cmd::zoomIn:       if (arrangement) arrangement->zoom (1.4); break;
        case cmd::zoomOut:      if (arrangement) arrangement->zoom (1 / 1.4); break;
        case cmd::toggleMixer:  showMixer (! mixerVisible); break;
        case cmd::render:       showExportDialog (s, settings); break;
        case cmd::importAudio:  importDialog(); break;
        case cmd::save:         s.saveNow (true); break;
        case cmd::closeProject: closeProject(); break;
        case cmd::cleanProject:
        {
            auto unused = io::unusedAudioFiles (s.project(), s.folder);
            if (unused.empty()) { showError ("No hay audio sin usar en este proyecto."); break; }
            juce::int64 bytes = 0;
            for (auto& f : unused) bytes += f.getSize();
            confirm ("Limpiar proyecto", juce::String ((int) unused.size()) + " archivos de audio no los usa ningún ítem ("_u
                     + juce::File::descriptionOfSizeInBytes (bytes) + ").\nSe moverán a la Papelera."_u,
                     "Mover a la Papelera", [unused] { for (auto f : unused) f.moveToTrash(); });
            break;
        }
        default:
            if (id >= cmd::marker1 && id <= cmd::marker9) s.goToMarker ((int) (id - cmd::marker1) + 1);
            else return false;
    }
    return true;
}

//==============================================================================
juce::StringArray MainComponent::getMenuBarNames()
{
    return { "Archivo", "Editar", "Pista", "Transporte", "Ver" };
}

juce::PopupMenu MainComponent::getMenuForIndex (int index, const juce::String&)
{
    juce::PopupMenu m;
    auto add = [&] (juce::CommandID id) { m.addCommandItem (&commands, id); };
    switch (index)
    {
        case 0: for (auto id : { cmd::newProject, cmd::openProject, cmd::save }) add (id);
                m.addSeparator(); add (cmd::importAudio); add (cmd::render); add (cmd::cleanProject);
                m.addSeparator(); add (cmd::closeProject);
            #if ! JUCE_MAC
                m.addSeparator(); add (cmd::preferences);
            #endif
                break;
        case 1: for (auto id : { cmd::undo, cmd::redo }) add (id);
                m.addSeparator(); for (auto id : { cmd::cut, cmd::copy, cmd::paste, cmd::duplicate, cmd::deleteItems }) add (id);
                m.addSeparator(); for (auto id : { cmd::split, cmd::selectAll, cmd::clearSelection }) add (id);
                break;
        case 2: add (cmd::insertTrack); add (cmd::insertInstrumentTrack);
                m.addItem ("Insertar bus (submezcla o efectos)", session != nullptr, false, [this] { if (session) session->addTrack (TrackRole::bus); });
                m.addItem ("Bus de reverb para la pista seleccionada", session != nullptr, false, [this]
                {
                    if (session == nullptr || session->project().findTrack (session->selectedTrack) == nullptr) return;
                    const auto bus = session->createBusFor ({ session->selectedTrack }, true, "Reverb");
                    session->updateTrack (bus, [] (Track& t) { t.inserts.push_back (PluginManager::internalSlot ("reverb")); });
                });
                break;
        case 3: for (auto id : { cmd::playStop, cmd::record, cmd::repeat, cmd::metronome }) add (id);
                m.addSeparator(); for (auto id : { cmd::toStart, cmd::toEnd, cmd::prevStop, cmd::nextStop, cmd::insertMarker }) add (id);
                m.addSeparator(); add (cmd::panic);
                break;
        case 4: add (cmd::toggleMixer); add (cmd::zoomIn); add (cmd::zoomOut); break;
        default: break;
    }
    return m;
}

} // namespace wdaw::ui
