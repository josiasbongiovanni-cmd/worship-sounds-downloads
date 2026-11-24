#pragma once

#include "Arrangement.h"
#include "TransportBar.h"
#include "Mixer.h"
#include "Inspector.h"
#include "PianoRoll.h"
#include "HomeView.h"

namespace wdaw::ui
{

namespace cmd
{
    enum : juce::CommandID
    {
        playStop = 0x2000, record, repeat, toStart, toEnd, split, deleteItems, duplicate, copy, cut, paste, selectAll,
        undo, redo, insertTrack, insertInstrumentTrack, insertMarker, prevStop, nextStop, clearSelection, zoomIn, zoomOut,
        toggleMixer, render, importAudio, save, preferences, panic, closeProject, newProject, openProject, cursorLeft,
        cursorRight, metronome, cleanProject, marker1, marker9 = marker1 + 8
    };
}

/** Ventana principal: inicio ↔ editor (transporte, arreglo, mezclador, inspector), comandos y menús. */
class MainComponent : public juce::Component,
                      public juce::ApplicationCommandTarget,
                      public juce::MenuBarModel,
                      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void parentHierarchyChanged() override;

    void openProject (const juce::File& folder);
    void createProject (juce::String name, edit::Template, double bpm, juce::String key, int beats);
    void closeProject();
    bool hasProject() const { return session != nullptr; }
    /** Para la captura de pantalla de prueba (--screenshot). */
    void showMixer (bool);
    void openPianoRollForFirstMidiClip();

    // ApplicationCommandTarget
    juce::ApplicationCommandTarget* getNextCommandTarget() override { return nullptr; }
    void getAllCommands (juce::Array<juce::CommandID>&) override;
    void getCommandInfo (juce::CommandID, juce::ApplicationCommandInfo&) override;
    bool perform (const InvocationInfo&) override;

    // MenuBarModel
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int, const juce::String&) override;
    void menuItemSelected (int, int) override {}

    juce::ApplicationCommandManager commands;
    juce::Component* pianoRollComponent() const { return pianoRollWindow != nullptr ? pianoRollWindow->getContentComponent() : nullptr; }

private:
    AppSettings settings;
    PluginManager pluginManager;
    AudioEngine engine { pluginManager };
    MidiLearn midiLearn;
    ThumbnailStore thumbnails;
    MeterModel meters { engine };
    juce::TooltipWindow tooltips { this, 600 };

    std::unique_ptr<Session> session;
    std::unique_ptr<Context> ctx;
    std::unique_ptr<HomeView> home;
    std::unique_ptr<TransportBar> transport;
    std::unique_ptr<Arrangement> arrangement;
    std::unique_ptr<Mixer> mixer;
    std::unique_ptr<Inspector> inspector;
    std::unique_ptr<juce::DocumentWindow> pianoRollWindow;
    std::map<juce::Uuid, std::unique_ptr<PluginWindow>> pluginWindows;
    bool mixerVisible = false, inspectorVisible = true;

    void buildEditor();
    void openPluginWindow (const juce::Uuid& trackId, const juce::Uuid& slotId);
    void choosePluginFor (const juce::Uuid& trackId, bool instrument);
    void openPianoRoll();
    void importDialog();
    void showError (const juce::String&);
    void timerCallback() override;
};

} // namespace wdaw::ui
