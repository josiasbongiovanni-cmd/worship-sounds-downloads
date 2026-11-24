#pragma once

#include "Model.h"

/*  Operaciones de edición puras sobre el modelo (testeables sin audio ni interfaz). */
namespace wdaw::edit
{

// Pistas
Track& addTrack (Project&, const juce::String& name, TrackRole = TrackRole::audio, juce::String colour = {});
void removeTrack (Project&, const juce::Uuid&);
/** Mueve una pista a la posición `newIndex` (arrastrar para reordenar). */
void moveTrack (Project&, const juce::Uuid&, int newIndex);

// Ruteo: buses y envíos
/** Crea un envío de `from` al bus (si no crea un bucle). Devuelve el id del envío. */
std::optional<juce::Uuid> addSend (Project&, const juce::Uuid& from, const juce::Uuid& bus, double levelDb = -12, bool preFader = false);
void removeSend (Project&, const juce::Uuid& trackId, const juce::Uuid& sendId);
/** Salida de la pista: bus (o nulo = Master). Falla si crearía un bucle. */
bool setOutputBus (Project&, const juce::Uuid& trackId, const juce::Uuid& bus);

// Automatización
namespace automation
{
    /** Busca el carril o lo crea con un punto inicial en `initialValue`. */
    AutomationLane& ensureLane (Track&, const AutomationTarget&, double initialValue);
    /** Agrega (o reemplaza si ya hay uno casi en el mismo tiempo) un punto. Devuelve su índice. */
    int addPoint (AutomationLane&, Beats time, double value);
    void removePoints (AutomationLane&, Beats from, Beats to);   // [from, to]
    /** Escritura (touch/latch): reemplaza lo que había entre `from` y `to` por un punto en `to`. */
    void writeValue (AutomationLane&, Beats from, Beats to, double value);
    /** Quita puntos redundantes (colineales) dentro de una tolerancia, para que lo escrito quede liviano. */
    void simplify (AutomationLane&, double tolerance);
    juce::String laneName (const Project&, const Track&, const AutomationLane&);
}

// Ítems de audio
struct ClipRef { int track = -1; int clip = -1; bool isMidi = false; explicit operator bool() const { return track >= 0; } };
ClipRef findClip (const Project&, const juce::Uuid&);

void addClip (Project&, const juce::Uuid& trackId, AudioClip);
void removeClip (Project&, const juce::Uuid&);
void moveClip (Project&, const juce::Uuid&, Beats newStart, std::optional<juce::Uuid> toTrack = {});
void trimClipStart (Project&, const juce::Uuid&, Beats newStart);
void trimClipEnd (Project&, const juce::Uuid&, Beats newEnd);
/** Divide en `at`. Devuelve el id del ítem derecho. */
std::optional<juce::Uuid> splitClip (Project&, const juce::Uuid&, Beats at);
/** Divide todos los ítems (audio y MIDI) que cruzan `at` (tecla S sin selección). */
void splitAll (Project&, Beats at);
std::optional<juce::Uuid> duplicateClip (Project&, const juce::Uuid&);

/** Libera el rango [from, to) de una pista recortando/dividiendo lo que haya (al grabar una toma nueva encima). */
void clearRange (Project&, const juce::Uuid& trackId, Beats from, Beats to);

// Ítems MIDI
void addMidiClip (Project&, const juce::Uuid& trackId, MidiClip);
MidiClip* findMidiClip (Project&, const juce::Uuid&);

// Copiar / pegar
struct Clipboard
{
    struct Entry { int trackOffset = 0; std::optional<AudioClip> audio; std::optional<MidiClip> midi; };
    std::vector<Entry> entries;   // posiciones relativas al primer ítem
    bool empty() const { return entries.empty(); }
};
Clipboard copyClips (const Project&, const std::vector<juce::Uuid>& ids);
/** Pega en `at` empezando por la pista `firstTrack`. Devuelve los ids nuevos. */
std::vector<juce::Uuid> paste (Project&, const Clipboard&, Beats at, int firstTrack);

// Marcadores y secciones
Marker& addMarker (Project&, Beats at, const juce::String& name = {});
void removeMarker (Project&, const juce::Uuid&);
const Marker* markerNumber (const Project&, int number);
Section& addSection (Project&, SectionKind, int bars, std::optional<Beats> at = {});
const Section* sectionAt (const Project&, Beats);
std::optional<Beats> nextStop (const Project&, Beats after);
std::optional<Beats> previousStop (const Project&, Beats before);

// Tempo: el modelo está en beats, así que basta con cambiar el BPM.
// Los ítems de audio conservan su posición musical; su duración en segundos no cambia.
void setTempo (Project&, double bpm);

// Edición MIDI (piano roll)
namespace midi
{
    /** Cuantiza notas a la grilla del proyecto. strength 0…1. ids vacíos = todas. */
    void quantize (MidiClip&, Beats clipStart, const std::vector<juce::Uuid>& ids, Beats grid, double strength = 1.0, bool lengths = false);
    void transpose (MidiClip&, const std::vector<juce::Uuid>& ids, int semitones);
    void legato (MidiClip&, const std::vector<juce::Uuid>& ids);
    void setVelocity (MidiClip&, const std::vector<juce::Uuid>& ids, int velocity);
    void move (MidiClip&, const std::vector<juce::Uuid>& ids, Beats delta, int semitones);
    void resize (MidiClip&, const std::vector<juce::Uuid>& ids, Beats delta, Beats minimum);
    void remove (MidiClip&, const std::vector<juce::Uuid>& ids);
    std::vector<juce::Uuid> duplicate (MidiClip&, const std::vector<juce::Uuid>& ids);
    juce::Uuid addNote (MidiClip&, int pitch, Beats at, Beats length, int velocity = 100);
    /** Nombre en notación latina: 60 = Do4. */
    juce::String noteName (int pitch);
    bool isBlackKey (int pitch);
}

// Plantillas worship
enum class Template { empty, fullBand, acoustic, vocals, multitracks };
juce::String displayName (Template);
juce::String subtitle (Template);
Project makeProject (Template, const juce::String& name, double bpm, const juce::String& key, int beatsPerBar);

} // namespace wdaw::edit
