#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "Engine/AudioEngine.h"
#include "Model/Editing.h"

namespace wdaw
{

//==============================================================================
/** "Aprender MIDI" (Learn en REAPER). Las asignaciones de pistas se guardan por id de pista
    (no por número), así que reordenar o borrar pistas no mueve el control a otra pista. */
struct MidiMapping
{
    enum class Kind { cc, note, pitch };
    enum class Target { trackVolume, trackPan, trackMute, trackSolo, trackArm, masterVolume,
                        playStop, record, repeat, nextMarker, previousMarker, goToStart, panic };
    Kind kind = Kind::cc;
    int channel = 1, number = 0;
    Target target = Target::playStop;
    juce::Uuid trackId;   // para destinos de pista

    bool isContinuous() const { return target == Target::trackVolume || target == Target::trackPan || target == Target::masterVolume; }
    bool isTrackTarget() const { return target <= Target::trackArm; }
    juce::String sourceLabel() const;
    static juce::String targetLabel (Target);
};

class MidiLearn
{
public:
    MidiLearn();
    std::vector<MidiMapping> global;    // transporte (para todos los proyectos)
    std::vector<MidiMapping> project;   // pistas (guardado en la carpeta del proyecto)

    void loadProject (const juce::File& folder);
    void saveProject (const juce::File& folder) const;
    void saveGlobal() const;

    /** Hilo MIDI: ¿lo consume una asignación o el modo aprender? Si es así, lo manda al hilo de mensajes. */
    bool filter (const juce::MidiMessage&);

    void startLearning (MidiMapping::Target, juce::Uuid trackId = {});
    void cancelLearning();
    bool isLearning() const { return learning.load(); }
    juce::String learningLabel;
    std::function<void (const MidiMapping&, float value)> onAction;   // hilo de mensajes
    std::function<void()> onLearned;
    juce::String lastMessage;

    void remove (const MidiMapping&);
    const MidiMapping* find (MidiMapping::Target, juce::Uuid trackId) const;

private:
    std::atomic<bool> learning { false };
    MidiMapping pending;
    juce::SpinLock lock;
    std::set<int> keys;       // copia para el hilo MIDI
    void rebuildKeys();
    static int key (MidiMapping::Kind, int channel, int number);
};

//==============================================================================
/** Proyecto abierto: modelo, deshacer, guardado, transporte, selección y edición.
    La interfaz escucha los cambios (ChangeBroadcaster). */
class Session : public juce::ChangeBroadcaster, private juce::Timer
{
public:
    Session (AudioEngine&, PluginManager&, MidiLearn&, juce::File folder, Project);
    ~Session() override;

    AudioEngine& engine;
    PluginManager& plugins;
    MidiLearn& midiLearn;
    const juce::File folder;

    const Project& project() const noexcept { return proj; }
    Timeline timeline() const noexcept { return proj.timeline(); }

    //--- Edición con deshacer
    void edit (const std::function<void (Project&)>&, bool undoable = true);
    void beginGesture();      // punto de deshacer antes de un gesto continuo (fader, perilla, arrastre)
    void undo();
    void redo();
    bool canUndo() const noexcept { return ! undoStack.empty(); }
    bool canRedo() const noexcept { return ! redoStack.empty(); }

    //--- Guardado (automático; captura el estado de los plugins solo al guardar a mano, al cerrar o cada 2 minutos)
    void saveNow (bool capturePlugins = true);
    bool isDirty() const noexcept { return dirty; }

    //--- Transporte (cursor de edición y de reproducción separados, como REAPER)
    Beats editCursor = 0;
    Beats playhead() const;
    bool isPlaying() const { return engine.isPlaying(); }
    bool isRecording() const { return engine.isRecording(); }
    void togglePlay();
    void play();
    void stop();
    void seek (Beats);
    void goToStart();
    void goToEnd();
    void toggleRecord();
    int countInBars = 0;
    bool followPlayhead = true;

    //--- Selección
    std::vector<juce::Uuid> selectedClips;
    juce::Uuid selectedTrack;
    bool isSelected (const juce::Uuid& clip) const;
    void selectClip (const juce::Uuid&, bool add);
    void selectClips (std::vector<juce::Uuid>);
    void clearClipSelection();
    void selectTrack (const juce::Uuid&);

    //--- Ítems
    void splitAtCursor();          // S
    void deleteSelection();        // Supr
    void duplicateSelection();     // Ctrl/Cmd+D
    void copySelection();          // Ctrl/Cmd+C
    void cutSelection();           // Ctrl/Cmd+X
    void paste();                  // Ctrl/Cmd+V (en el cursor, en la pista seleccionada)
    void selectAllClips();

    //--- Pistas
    juce::Uuid addTrack (TrackRole = TrackRole::audio);
    void removeTrack (const juce::Uuid&);
    void updateTrack (const juce::Uuid&, const std::function<void (Track&)>&, bool undoable = true);
    void moveTrack (const juce::Uuid&, int newIndex);

    //--- Ruteo (buses y envíos)
    std::optional<juce::Uuid> addSend (const juce::Uuid& from, const juce::Uuid& bus, bool preFader = false);
    void removeSend (const juce::Uuid& track, const juce::Uuid& send);
    void setOutputBus (const juce::Uuid& track, const juce::Uuid& bus);
    /** Crea un bus y le manda las pistas indicadas (salida o envío). */
    juce::Uuid createBusFor (const std::vector<juce::Uuid>& tracks, bool asSend, juce::String name = {});

    //--- Controles automatizables (faders, perillas, envíos, parámetros de plugins).
    //    Escriben el valor fijo y, en modo Touch/Latch con el transporte en marcha, también el carril.
    void touchBegin (const juce::Uuid& track, const AutomationTarget&);
    void touchEnd (const juce::Uuid& track, const AutomationTarget&);
    void setControl (const juce::Uuid& track, const AutomationTarget&, double value);
    /** Valor que suena ahora (el del carril si manda, si no el fijo). */
    double effectiveValue (const Track&, const AutomationTarget&) const;
    double staticValue (const Track&, const AutomationTarget&) const;
    void showLane (const juce::Uuid& track, const AutomationTarget&);
    bool isWriting (const juce::Uuid& track) const;
    /** Último parámetro de plugin tocado (para "Automatizar último parámetro"). */
    struct TouchedParam { juce::Uuid track; AutomationTarget target; };
    std::optional<TouchedParam> lastTouchedParam;

    //--- Marcadores, secciones, selección de tiempo
    void insertMarker();
    void goToMarker (int number);
    void goToNextStop();
    void goToPreviousStop();
    void setTimeSelection (Beats a, Beats b);
    void clearTimeSelection();
    void toggleRepeat();
    void loopSection (const juce::Uuid&);

    //--- Importar
    void importFiles (const juce::Array<juce::File>&, std::optional<juce::Uuid> track = {}, std::optional<Beats> at = {});
    juce::String statusText;

    //--- MIDI
    void refreshLiveMidi();
    void handleMidiAction (const MidiMapping&, float value);
    juce::Uuid editingMidiClip;     // ítem abierto en el piano roll
    juce::Uuid createMidiClip (const juce::Uuid& track, std::optional<Beats> at = {});

    //--- Vista
    double pixelsPerBeat = 16;
    bool snapEnabled = true;
    Beats gridDivision = 1;   // en beats
    Beats snap (Beats) const;
    Beats timelineLength() const;

    std::function<void (juce::String)> showError;
    std::function<void()> openMidiEditor;
    /** Cuenta cambios de contenido (para refrescar la interfaz y redibujar formas de onda). */
    int revision = 0;

    /** Para tareas en segundo plano: deja de ser true cuando la sesión se cierra. */
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);

private:
    Project proj;
    std::vector<Project> undoStack, redoStack;
    edit::Clipboard clipboard;
    bool dirty = false;
    int ticksSinceEdit = -1, ticksSincePluginCapture = 0;
    std::map<juce::Uuid, juce::MemoryBlock> lastPluginStates;
    void setProject (Project);
    void applyPluginStates (Project&);
    void finishRecording();
    void timerCallback() override;

    struct Writer
    {
        juce::Uuid track;
        AutomationTarget target;
        Beats lastBeat = 0;
        double value = 0;
        bool held = false;
        juce::uint32 lastChange = 0;
    };
    std::vector<Writer> writers;
    bool wasPlayingForWriters = false;
    std::unique_ptr<juce::TimedCallback> writeTimer;
    void writeTick();
    void finishWriters();
    Writer* findWriter (const juce::Uuid& track, const AutomationTarget&);
    void applyStatic (Track&, const AutomationTarget&, double value) const;
};

} // namespace wdaw
