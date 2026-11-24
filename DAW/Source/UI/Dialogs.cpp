#include "Dialogs.h"
#include "Model/Serialization.h"

namespace wdaw::ui
{

AppSettings::AppSettings()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "Worship DAW";
    o.filenameSuffix = ".settings";
    o.folderName = PluginManager::settingsFolder().getFullPathName();
    o.osxLibrarySubFolder = "Application Support";
    o.storageFormat = juce::PropertiesFile::storeAsXML;
    props.setStorageParameters (o);
}

juce::File AppSettings::projectsFolder()
{
    auto stored = file().getValue ("projectsFolder");
    auto f = stored.isNotEmpty() ? juce::File (stored)
                                 : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Worship DAW");
    f.createDirectory();
    return f;
}

//==============================================================================
namespace
{
    /** Audio: el selector de dispositivo de JUCE (CoreAudio en Mac; ASIO, WASAPI y DirectSound en Windows)
        más los ajustes de grabación. */
    class AudioTab : public juce::Component, private juce::ChangeListener
    {
    public:
        AudioTab (AudioEngine& e, AppSettings& s) : engine (e), settings (s),
            selector (e.devices, 0, 256, 0, 256, false, false, false, false)
        {
            addAndMakeVisible (selector);
            useLatency.setToggleState (settings.file().getBoolValue ("useReportedLatency", true), juce::dontSendNotification);
            useLatency.onClick = [this] { settings.file().setValue ("useReportedLatency", useLatency.getToggleState()); engine.setUseReportedLatency (useLatency.getToggleState()); };
            addAndMakeVisible (useLatency);
            offset.setRange (-200, 200, 1);
            offset.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 22);
            offset.textFromValueFunction = [] (double v) { return (v > 0 ? "+" : "") + juce::String ((int) v) + " ms"; };
            offset.setValue (settings.file().getDoubleValue ("recordOffsetMs", 0), juce::dontSendNotification);
            offset.onValueChange = [this] { settings.file().setValue ("recordOffsetMs", offset.getValue()); engine.setRecordingOffsetMs (offset.getValue()); };
            addAndMakeVisible (offset);
            engine.devices.addChangeListener (this);
        }
        ~AudioTab() override { engine.devices.removeChangeListener (this); }

        void changeListenerCallback (juce::ChangeBroadcaster*) override
        {
            engine.enableAllChannels();
            if (auto xml = engine.devices.createStateXml()) settings.file().setValue ("audioDevice", xml.get());
            repaint();
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (16);
            selector.setBounds (r.removeFromTop (juce::jmax (220, r.getHeight() - 170)));
            r.removeFromTop (34);
            useLatency.setBounds (r.removeFromTop (26));
            r.removeFromTop (8);
            offset.setBounds (r.removeFromTop (26).withTrimmedLeft (160));
        }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().reduced (16);
            r.removeFromTop (selector.getHeight() + 6);
            g.setColour (colours::text3);
            g.setFont (font (11, true));
            g.drawText (juce::String::fromUTF8 ("GRABACIÓN"), r.removeFromTop (20), juce::Justification::centredLeft);
            g.setColour (colours::text2);
            g.setFont (font (12));
            const auto ms = engine.reportedRoundTripSeconds() * 1000;
            g.drawText (juce::String::fromUTF8 ("Latencia reportada (ida y vuelta): ") + juce::String (ms, 1) + " ms", r.removeFromTop (8 + 26).withTrimmedTop (34), juce::Justification::centredRight);
            g.drawText ("Ajuste manual", offset.getBounds().withX (16).withWidth (150), juce::Justification::centredLeft);
            g.setColour (colours::text3);
            g.setFont (font (11));
            g.drawFittedText (juce::String::fromUTF8 ("Para calibrar: graba el click con un micrófono y ajusta hasta que la toma quede sobre la grilla. "
                                                      "En Windows, para la menor latencia usa un driver ASIO de tu interfaz."),
                              getLocalBounds().reduced (16).removeFromBottom (36), juce::Justification::topLeft, 3);
        }

        AudioEngine& engine;
        AppSettings& settings;
        juce::AudioDeviceSelectorComponent selector;
        juce::ToggleButton useLatency { "Compensar la latencia que reporta el driver" };
        juce::Slider offset;
    };

    /** MIDI: controladores (activar/desactivar), último mensaje, Aprender MIDI del transporte y asignaciones. */
    class MidiTab : public juce::Component, private juce::Timer
    {
    public:
        MidiTab (AudioEngine& e, MidiLearn& l, AppSettings& s) : engine (e), learn (l), settings (s)
        {
            refreshButton.onClick = [this] { rebuild(); };
            panicButton.setButtonText ("Pánico: apagar todas las notas"_u);
            panicButton.onClick = [this] { engine.panic(); };
            addAndMakeVisible (refreshButton);
            addAndMakeVisible (panicButton);
            using T = MidiMapping::Target;
            for (auto t : { T::playStop, T::record, T::repeat, T::goToStart, T::previousMarker, T::nextMarker, T::panic })
            {
                auto* b = learnButtons.add (new juce::TextButton ("Aprender"));
                b->onClick = [this, t] { learn.startLearning (t); };
                addAndMakeVisible (b);
                targets.push_back (t);
            }
            rebuild();
            startTimerHz (5);
        }

        void rebuild()
        {
            toggles.clear();
            const auto disabled = juce::StringArray::fromTokens (settings.file().getValue ("midiDisabled"), "|", "");
            for (auto& info : juce::MidiInput::getAvailableDevices())
            {
                auto* t = toggles.add (new juce::ToggleButton (info.name));
                t->setToggleState (! disabled.contains (info.identifier), juce::dontSendNotification);
                t->onClick = [this, id = info.identifier, t]
                {
                    auto list = juce::StringArray::fromTokens (settings.file().getValue ("midiDisabled"), "|", "");
                    if (t->getToggleState()) list.removeString (id); else list.addIfNotAlreadyThere (id);
                    settings.file().setValue ("midiDisabled", list.joinIntoString ("|"));
                    engine.refreshMidiInputs (list);
                };
                addAndMakeVisible (t);
            }
            resized();
            repaint();
        }

        void timerCallback() override { repaint(); }

        void resized() override
        {
            auto r = getLocalBounds().reduced (16);
            r.removeFromTop (24);
            for (auto* t : toggles) t->setBounds (r.removeFromTop (26));
            if (toggles.isEmpty()) r.removeFromTop (26);
            auto row = r.removeFromTop (30);
            refreshButton.setBounds (row.removeFromLeft (170));
            row.removeFromLeft (8);
            panicButton.setBounds (row.removeFromLeft (260));
            r.removeFromTop (54);
            for (auto* b : learnButtons) { b->setBounds (r.removeFromTop (28).removeFromRight (100)); r.removeFromTop (4); }
            mappingsTop = r.getY() + 34;
        }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().reduced (16);
            g.setColour (colours::text3);
            g.setFont (font (11, true));
            g.drawText ("CONTROLADORES MIDI", r.removeFromTop (20), juce::Justification::centredLeft);
            if (toggles.isEmpty())
            {
                g.setColour (colours::text2);
                g.setFont (font (12));
                g.drawText ("No hay controladores conectados (USB, Bluetooth o red).", r.removeFromTop (26).withTrimmedTop (4), juce::Justification::centredLeft);
            }
            g.setColour (colours::text3);
            g.setFont (font (11, true));
            auto title = getLocalBounds().reduced (16);
            title.setY (refreshButton.getBottom() + 18);
            g.drawText ("TRANSPORTE (APRENDER MIDI)", title.removeFromTop (20), juce::Justification::centredLeft);
            g.setFont (font (12.5f));
            for (int i = 0; i < learnButtons.size(); ++i)
            {
                auto b = learnButtons[i]->getBounds();
                g.setColour (colours::text);
                g.drawText (MidiMapping::targetLabel (targets[(size_t) i]), b.withX (16).withWidth (260), juce::Justification::centredLeft);
                juce::String src = "Sin asignar";
                if (auto* m = learn.find (targets[(size_t) i], {})) src = m->sourceLabel();
                g.setColour (colours::text2);
                g.drawText (src, b.withX (280).withWidth (b.getX() - 290), juce::Justification::centredRight);
            }
            g.setColour (learn.isLearning() ? colours::accent2 : colours::text3);
            g.setFont (font (12));
            g.drawFittedText (learn.isLearning() ? "Esperando un control MIDI para: " + learn.learningLabel
                                                 : juce::String::fromUTF8 ("Para pistas: clic derecho en la cabecera o en el canal del mezclador › Aprender MIDI."),
                              getLocalBounds().reduced (16).removeFromBottom (40), juce::Justification::topLeft, 2);
        }

        AudioEngine& engine;
        MidiLearn& learn;
        AppSettings& settings;
        juce::OwnedArray<juce::ToggleButton> toggles;
        juce::OwnedArray<juce::TextButton> learnButtons;
        std::vector<MidiMapping::Target> targets;
        juce::TextButton refreshButton { "Buscar controladores" }, panicButton { "Panico: apagar todas las notas" };
        int mappingsTop = 0;
    };

    /** Plugins: lista y escaneo (VST3 en Mac y Windows, AU en Mac) con la lista negra de plugins que fallan. */
    class PluginsTab : public juce::Component
    {
    public:
        PluginsTab (PluginManager& pm, AppSettings& s)
            : list (pm.formats, pm.known, PluginManager::settingsFolder().getChildFile ("plugin-scan-crash.txt"), &s.file(), true), manager (pm)
        {
            addAndMakeVisible (list);
            list.setOptionsButtonText ("Opciones… (Escanear plugins nuevos)"_u);
        }
        ~PluginsTab() override { manager.saveKnownList(); }
        void resized() override { list.setBounds (getLocalBounds().reduced (12)); }
        juce::PluginListComponent list;
        PluginManager& manager;
    };

    class ShortcutsTab : public juce::Component
    {
    public:
        void paint (juce::Graphics& g) override
        {
            static const char* rows[][2] = {
                { "Espacio", "Reproducir / Detener (vuelve al cursor de edición)" },   // utf8-ok
                { "Ctrl/Cmd + R", "Grabar (si está sonando: punch-in)" },   // utf8-ok
                { "R", "Repetir (loop)" },
                { "Inicio / Fin", "Ir al inicio / final" },
                { "S", "Dividir ítems en el cursor" },   // utf8-ok
                { "Ctrl/Cmd + C / X / V", "Copiar / Cortar / Pegar ítems" },   // utf8-ok
                { "Ctrl/Cmd + D", "Duplicar ítems" },   // utf8-ok
                { "Supr / Retroceso", "Eliminar ítems" },   // utf8-ok
                { "Ctrl/Cmd + A", "Seleccionar todos los ítems" },   // utf8-ok
                { "Ctrl/Cmd + T", "Insertar pista" },
                { "Shift + Ctrl/Cmd + T", "Insertar pista de instrumento (MIDI)" },
                { "M  /  1 … 9", "Insertar marcador  /  ir al marcador" },   // utf8-ok
                { "[  /  ]", "Marcador o sección anterior / siguiente" },   // utf8-ok
                { "Flechas izquierda / derecha", "Mover el cursor un compás" },   // utf8-ok
                { "Esc", "Quitar selección de tiempo y de ítems" },   // utf8-ok
                { "+  /  -", "Zoom" },
                { "Ctrl/Cmd + Z  /  Shift + Ctrl/Cmd + Z", "Deshacer / Rehacer" },
                { "Ctrl/Cmd + M", "Mezclador" },
                { "Ctrl/Cmd + Alt + R", "Renderizar" },
                { "Ctrl/Cmd + I", "Importar audio" },
                { "Ctrl/Cmd + S", "Guardar" },
                { "Ctrl/Cmd + ,", "Preferencias" },
                { "Doble clic en un ítem MIDI", "Abrir el piano roll" },   // utf8-ok
                { "Arrastrar en la regla", "Selección de tiempo (el loop la sigue)" },   // utf8-ok
                { "Arrastrar en zona vacía", "Seleccionar ítems con un rectángulo" },   // utf8-ok
                { "Clic derecho en R", "Entrada de grabación, monitoreo y salida" } };   // utf8-ok
            auto r = getLocalBounds().reduced (18, 14);
            for (auto& row : rows)
            {
                auto line = r.removeFromTop (22);
                g.setColour (colours::accent2);
                g.setFont (mono (12));
                g.drawText (juce::String::fromUTF8 (row[0]), line.removeFromLeft (280), juce::Justification::centredLeft);
                g.setColour (colours::text);
                g.setFont (font (12.5f));
                g.drawText (juce::String::fromUTF8 (row[1]), line, juce::Justification::centredLeft);
            }
            g.setColour (colours::text3);
            g.setFont (font (11));
            g.drawText (juce::String::fromUTF8 ("Atajos por defecto de REAPER. En Mac, Ctrl de Windows es ⌘ (Cmd)."), r.removeFromTop (30), juce::Justification::centredLeft);
        }
    };
}

PreferencesPanel::PreferencesPanel (AudioEngine& e, PluginManager& pm, MidiLearn& ml, AppSettings& s, int initialTab)
{
    tabs.addTab ("Audio", colours::panel, new AudioTab (e, s), true);
    tabs.addTab ("MIDI", colours::panel, new MidiTab (e, ml, s), true);
    tabs.addTab ("Plugins", colours::panel, new PluginsTab (pm, s), true);
    tabs.addTab ("Atajos", colours::panel, new ShortcutsTab(), true);
    tabs.setCurrentTabIndex (initialTab);
    tabs.setTabBarDepth (34);
    addAndMakeVisible (tabs);
    setSize (720, 640);
}

void PreferencesPanel::resized() { tabs.setBounds (getLocalBounds()); }
void PreferencesPanel::paint (juce::Graphics& g) { g.fillAll (colours::panel); }

void PreferencesPanel::show (AudioEngine& e, PluginManager& pm, MidiLearn& ml, AppSettings& s, int initialTab)
{
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new PreferencesPanel (e, pm, ml, s, initialTab));
    o.dialogTitle = "Preferencias";
    o.dialogBackgroundColour = colours::panel;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.launchAsync();
}

//==============================================================================
PluginWindow::PluginWindow (PluginInstance::Ptr inst, const juce::String& title, std::function<void()> onClose)
    : juce::DocumentWindow (title, colours::panel, juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton),
      instance (inst), onCloseFn (std::move (onClose))
{
    setUsingNativeTitleBar (true);
    juce::AudioProcessorEditor* editor = nullptr;
    if (instance->processor->hasEditor()) editor = instance->processor->createEditorIfNeeded();
    if (editor == nullptr) editor = new juce::GenericAudioProcessorEditor (*instance->processor);
    setContentOwned (editor, true);          // la ventana toma el tamaño que pide el plugin
    setResizable (editor->isResizable(), false);
    setAlwaysOnTop (true);
    centreWithSize (getWidth(), getHeight());
    setVisible (true);
}

PluginWindow::~PluginWindow()
{
    if (auto* ed = dynamic_cast<juce::AudioProcessorEditor*> (getContentComponent()))
        instance->processor->editorBeingDeleted (ed);
    clearContentComponent();
}

//==============================================================================
void choosePlugin (PluginManager& pm, bool instruments, juce::Component* target, std::function<void (PluginSlot)> onChosen,
                   std::function<void()> onScan)
{
    juce::PopupMenu m;
    auto list = instruments ? pm.instruments() : pm.effects();
    if (instruments)
    {
        m.addSectionHeader ("Incluidos");
        m.addItem (juce::String::fromUTF8 ("Piano básico"), [onChosen] { onChosen (PluginManager::internalSlot ("piano")); });
        m.addItem (juce::String::fromUTF8 ("Pad básico"), [onChosen] { onChosen (PluginManager::internalSlot ("pad")); });
    }
    else
    {
        m.addSectionHeader ("Incluidos");
        m.addItem (juce::String::fromUTF8 ("Reverb básica"), [onChosen] { onChosen (PluginManager::internalSlot ("reverb")); });
        m.addItem (juce::String::fromUTF8 ("Delay básico"), [onChosen] { onChosen (PluginManager::internalSlot ("delay")); });
    }
    std::map<juce::String, juce::PopupMenu> byMaker;
    juce::PopupMenu worship;
    for (auto& d : list)
    {
        auto label = d.name + (d.pluginFormatName.isNotEmpty() ? "  (" + d.pluginFormatName + ")" : juce::String());
        auto item = [onChosen, d] { onChosen (PluginManager::makeSlot (d)); };
        if (d.manufacturerName.containsIgnoreCase ("worship")) worship.addItem (label, item);
        else byMaker[d.manufacturerName.isEmpty() ? juce::String ("Otros") : d.manufacturerName].addItem (label, item);
    }
    if (worship.getNumItems() > 0) { m.addSectionHeader ("Worship Sounds"); m.addSubMenu ("Worship Sounds", worship); }
    if (! byMaker.empty())
    {
        m.addSectionHeader (instruments ? "Instrumentos instalados" : "Efectos instalados");
        for (auto& [maker, sub] : byMaker) m.addSubMenu (maker, sub);
    }
    if (list.empty())
        m.addItem (juce::String::fromUTF8 ("No hay plugins escaneados todavía"), false, false, nullptr);
    m.addSeparator();
    m.addItem ("Escanear plugins…"_u, [onScan] { if (onScan) onScan(); });
    auto opts = juce::PopupMenu::Options();
    if (target != nullptr) opts = opts.withTargetComponent (target);
    m.showMenuAsync (opts);
}

//==============================================================================
void askText (const juce::String& title, const juce::String& current, std::function<void (juce::String)> done)
{
    auto* w = new juce::AlertWindow (title, {}, juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("text", current);
    w->addButton ("Aceptar", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancelar", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([w, done] (int r)
    {
        if (r == 1) done (w->getTextEditorContents ("text").trim());
    }), true);
}

void confirm (const juce::String& title, const juce::String& message, const juce::String& okText, std::function<void()> onOk)
{
    juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::WarningIcon)
                                      .withTitle (title).withMessage (message).withButton (okText).withButton ("Cancelar"),
                                  [onOk] (int r) { if (r == 1) onOk(); });
}

//==============================================================================
namespace
{
    class NewProjectContent : public juce::Component
    {
    public:
        NewProjectContent (std::function<void (juce::String, edit::Template, double, juce::String, int)> d, edit::Template initial) : done (std::move (d))
        {
            name.setText (juce::String::fromUTF8 ("Nueva canción"));
            name.setFont (font (15, true));
            addAndMakeVisible (name);
            for (int i = 0; i < 5; ++i) templateBox.addItem (edit::displayName ((edit::Template) i) + "  —  "_u + edit::subtitle ((edit::Template) i), i + 1);
            templateBox.setSelectedId ((int) initial + 1);
            bpm.setRange (20, 300, 1); bpm.setValue (72);
            bpm.setSliderStyle (juce::Slider::IncDecButtons);
            bpm.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 70, 26);
            int id = 1;
            for (auto k : { "C", "C#", "Db", "D", "Eb", "E", "F", "F#", "Gb", "G", "Ab", "A", "Bb", "B" })
                for (auto s : { "", "m" }) keyBox.addItem (juce::String (k) + s, id++);
            keyBox.setText ("G");
            for (int b : { 2, 3, 4, 6 }) meterBox.addItem (juce::String (b) + "/4", b);
            meterBox.setSelectedId (4);
            for (auto* c : std::initializer_list<juce::Component*> { &templateBox, &bpm, &keyBox, &meterBox, &create, &cancel }) addAndMakeVisible (c);
            create.onClick = [this]
            {
                done (name.getText().trim(), (edit::Template) (templateBox.getSelectedId() - 1), bpm.getValue(), keyBox.getText(), meterBox.getSelectedId());
                if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) dw->exitModalState (1);
            };
            cancel.onClick = [this] { if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) dw->exitModalState (0); };
            create.setColour (juce::TextButton::buttonColourId, colours::accent);
            setSize (520, 330);
        }
        void paint (juce::Graphics& g) override
        {
            g.fillAll (colours::panel);
            g.setColour (colours::text3);
            g.setFont (font (11, true));
            for (auto [c, label] : std::initializer_list<std::pair<juce::Component*, const char*>> {
                     { &name, "NOMBRE" }, { &templateBox, "PLANTILLA" }, { &bpm, "TEMPO (BPM)" }, { &keyBox, "TONALIDAD" }, { &meterBox, "COMPÁS" } })   // utf8-ok
                g.drawText (juce::String::fromUTF8 (label), c->getX(), c->getY() - 18, 200, 14, juce::Justification::centredLeft);
        }
        void resized() override
        {
            auto r = getLocalBounds().reduced (20);
            r.removeFromTop (18);
            name.setBounds (r.removeFromTop (34));
            r.removeFromTop (28);
            templateBox.setBounds (r.removeFromTop (30));
            r.removeFromTop (28);
            auto row = r.removeFromTop (30);
            bpm.setBounds (row.removeFromLeft (150));
            row.removeFromLeft (16);
            keyBox.setBounds (row.removeFromLeft (120));
            row.removeFromLeft (16);
            meterBox.setBounds (row.removeFromLeft (100));
            auto buttons = r.removeFromBottom (34);
            create.setBounds (buttons.removeFromRight (140));
            buttons.removeFromRight (8);
            cancel.setBounds (buttons.removeFromRight (110));
        }
        std::function<void (juce::String, edit::Template, double, juce::String, int)> done;
        juce::TextEditor name;
        juce::ComboBox templateBox, keyBox, meterBox;
        juce::Slider bpm;
        juce::TextButton create { "Crear proyecto" }, cancel { "Cancelar" };
    };
}

void showNewProjectDialog (std::function<void (juce::String, edit::Template, double, juce::String, int)> done, edit::Template initial)
{
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new NewProjectContent (std::move (done), initial));
    o.dialogTitle = "Nuevo proyecto";
    o.dialogBackgroundColour = colours::panel;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.launchAsync();
}

//==============================================================================
namespace
{
    class ExportContent : public juce::Component
    {
    public:
        ExportContent (Session& s, AppSettings& st) : session (s), settings (st)
        {
            format.addItem ("WAV", 1); format.addItem ("FLAC (sin pérdida)"_u, 2); format.addItem ("AIFF", 3); format.addItem ("OGG Vorbis", 4);
            format.setSelectedId (settings.file().getIntValue ("exportFormat", 1));
            bits.addItem ("16 bits (CD)", 16); bits.addItem ("24 bits", 24); bits.addItem ("32 bits flotante", 32);
            bits.setSelectedId (settings.file().getIntValue ("exportBits", 24));
            rate.addItem ("Frecuencia del dispositivo", 1); rate.addItem ("44,1 kHz", 44100); rate.addItem ("48 kHz", 48000); rate.addItem ("96 kHz", 96000);
            rate.setSelectedId (settings.file().getIntValue ("exportRate", 1));
            range.addItem ("Toda la canción"_u, 1);
            range.addItem (juce::String::fromUTF8 ("Selección de tiempo"), 2);
            range.setSelectedId (1);
            range.setItemEnabled (2, s.project().loopEnd > s.project().loopStart + 0.01);
            normalize.setToggleState (settings.file().getBoolValue ("exportNormalize", false), juce::dontSendNotification);
            stems.setToggleState (false, juce::dontSendNotification);
            for (auto* c : std::initializer_list<juce::Component*> { &format, &bits, &rate, &range, &normalize, &stems, &render, &cancel }) addAndMakeVisible (c);
            render.setColour (juce::TextButton::buttonColourId, colours::accent);
            render.onClick = [this] { start(); };
            cancel.onClick = [this] { close(); };
            setSize (480, 360);
        }

        void close() { if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) dw->exitModalState (0); }

        void start()
        {
            settings.file().setValue ("exportFormat", format.getSelectedId());
            settings.file().setValue ("exportBits", bits.getSelectedId());
            settings.file().setValue ("exportRate", rate.getSelectedId());
            settings.file().setValue ("exportNormalize", normalize.getToggleState());
            ExportOptions o;
            o.format = (ExportOptions::Format) (format.getSelectedId() - 1);
            o.bitDepth = bits.getSelectedId();
            o.sampleRate = rate.getSelectedId() > 1 ? (double) rate.getSelectedId() : 0.0;
            o.normalize = normalize.getToggleState();
            o.stems = stems.getToggleState();
            auto& p = session.project();
            if (range.getSelectedId() == 2) { o.start = p.loopStart; o.end = p.loopEnd; }
            const auto dir = io::exportsFolder (session.folder);
            dir.createDirectory();
            o.destination = dir.getNonexistentChildFile (io::sanitize (p.name) + " - Mezcla " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H%M"), extensionFor (o.format), false);
            session.stop();

            struct Job : juce::ThreadWithProgressWindow
            {
                Job (Session& s, ExportOptions opts) : juce::ThreadWithProgressWindow ("Renderizando…"_u, true, true), session (s), o (std::move (opts)) {}
                void run() override
                {
                    result = exportProject (session.engine, session.project(), session.folder, o,
                                            [this] (float p) { setProgress (p); return ! threadShouldExit(); });
                }
                void threadComplete (bool cancelled) override
                {
                    if (cancelled || ! result.ok)
                    {
                        juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::WarningIcon)
                            .withTitle ("Render").withMessage (cancelled ? juce::String ("Render cancelado.") : result.error).withButton ("OK"), nullptr);
                    }
                    else
                    {
                        auto file = result.files.front();
                        juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::InfoIcon)
                            .withTitle ("Render terminado")
                            .withMessage (juce::String ((int) result.files.size()) + " archivo(s) en la carpeta Exports.\nPico: " + juce::String (result.peakDb, 1) + " dBFS")
                            .withButton ("Mostrar en la carpeta").withButton ("OK"),
                            [file] (int r) { if (r == 1) file.revealToUser(); });
                    }
                    delete this;
                }
                Session& session;
                ExportOptions o;
                ExportResult result;
            };
            (new Job (session, o))->launchThread();
            close();
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (colours::panel);
            g.setColour (colours::text3);
            g.setFont (font (11, true));
            for (auto [c, label] : std::initializer_list<std::pair<juce::Component*, const char*>> {
                     { &format, "FORMATO" }, { &bits, "PROFUNDIDAD" }, { &rate, "FRECUENCIA" }, { &range, "RANGO" } })
                g.drawText (label, c->getX(), c->getY() - 18, 200, 14, juce::Justification::centredLeft);
            g.setColour (colours::text2);
            g.setFont (font (11.5f));
            g.drawFittedText (juce::String::fromUTF8 ("Se renderiza más rápido que en tiempo real, con todos los plugins y la compensación de latencia. "
                                                      "El click nunca va en la mezcla. Los archivos quedan en la carpeta Exports del proyecto."),
                              getLocalBounds().reduced (20).withTrimmedTop (220).removeFromTop (60), juce::Justification::topLeft, 3);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (20);
            r.removeFromTop (18);
            auto row = r.removeFromTop (30);
            format.setBounds (row.removeFromLeft (row.getWidth() / 2 - 8));
            row.removeFromLeft (16);
            bits.setBounds (row);
            r.removeFromTop (28);
            row = r.removeFromTop (30);
            rate.setBounds (row.removeFromLeft (row.getWidth() / 2 - 8));
            row.removeFromLeft (16);
            range.setBounds (row);
            r.removeFromTop (14);
            normalize.setBounds (r.removeFromTop (26));
            stems.setBounds (r.removeFromTop (26));
            auto buttons = r.removeFromBottom (34);
            render.setBounds (buttons.removeFromRight (150));
            buttons.removeFromRight (8);
            cancel.setBounds (buttons.removeFromRight (110));
        }

        Session& session;
        AppSettings& settings;
        juce::ComboBox format, bits, rate, range;
        juce::ToggleButton normalize { "Normalizar a -0,3 dBFS" }, stems { "Además, un archivo por pista (stems)"_u };
        juce::TextButton render { "Renderizar" }, cancel { "Cancelar" };
    };
}

void showExportDialog (Session& s, AppSettings& st)
{
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new ExportContent (s, st));
    o.dialogTitle = "Renderizar mezcla";
    o.dialogBackgroundColour = colours::panel;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.launchAsync();
}

} // namespace wdaw::ui
