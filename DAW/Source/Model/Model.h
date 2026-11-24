#pragma once

#include <juce_core/juce_core.h>
#include <optional>
#include <vector>

/*  Modelo de datos de Worship DAW (independiente del audio y de la interfaz).

    Unidad de tiempo: posiciones en **beats** (negras), como el timebase "beats" de REAPER.
    Así, al cambiar el BPM, ítems MIDI, secciones y marcadores se mantienen en la grilla.
    El audio fuente se mide en segundos (offset y duración dentro del archivo).
*/
namespace wdaw
{

using Beats = double;

/** Texto UTF-8 (acentos, ñ): usar siempre "Canción"_u para literales con caracteres no ASCII. */
inline juce::String operator""_u (const char* s, size_t n) { return juce::String::fromUTF8 (s, (int) n); }

//==============================================================================
struct Timeline
{
    double bpm = 120.0;
    int beatsPerBar = 4;

    double secondsPerBeat() const noexcept       { return 60.0 / bpm; }
    double secondsPerBar() const noexcept        { return secondsPerBeat() * beatsPerBar; }
    double toSeconds (Beats b) const noexcept    { return b * secondsPerBeat(); }
    Beats toBeats (double seconds) const noexcept { return seconds / secondsPerBeat(); }

    /** Ajusta a la grilla. `division` en beats (1 = negra, 0.25 = semicorchea, beatsPerBar = compás). */
    static Beats snap (Beats b, Beats division) noexcept;
    /** Ajusta hacia abajo (para dibujar notas desde el inicio de la celda). */
    static Beats snapDown (Beats b, Beats division) noexcept;

    /** "12.3.50" (compás.tiempo.centésimas), base 1. */
    juce::String barBeatString (Beats b) const;
    /** "1:23.456" */
    static juce::String timecode (double seconds);
};

//==============================================================================
enum class TrackRole { audio, guide, instrument, bus };
enum class MonitorMode { off, on, autoTape };
/** Modos de automatización (como REAPER): apagado = se ignoran los carriles (Trim/Read). */
enum class AutomationMode { off, read, touch, latch };
enum class SectionKind { intro, verse, preChorus, chorus, bridge, interlude, tag, vamp, outro };

juce::String toString (TrackRole);
juce::String toString (MonitorMode);
juce::String toString (AutomationMode);
AutomationMode automationModeFromString (const juce::String&);
juce::String displayName (AutomationMode);
juce::String toString (SectionKind);
TrackRole trackRoleFromString (const juce::String&);
MonitorMode monitorModeFromString (const juce::String&);
SectionKind sectionKindFromString (const juce::String&);

juce::String displayName (SectionKind);
juce::String shortName (SectionKind);
juce::String defaultColourId (SectionKind);
juce::String displayName (MonitorMode);

/** Entrada de grabación (clic derecho en el botón de armar, como en REAPER). */
struct TrackInput
{
    enum class Kind { off, mono, stereo };
    Kind kind = Kind::mono;
    int channel = 0;   // canal base 0 de la interfaz

    juce::String displayName() const;
    juce::String shortName() const;
    /** Canales de entrada usados (izquierdo, derecho). Mono duplica el canal. */
    std::pair<int, int> channels() const noexcept { return { channel, kind == Kind::stereo ? channel + 1 : channel }; }
    bool isOff() const noexcept { return kind == Kind::off; }
    bool operator== (const TrackInput& o) const noexcept { return kind == o.kind && (kind == Kind::off || channel == o.channel); }
};

/** Referencia serializable a un plugin (VST3 / AU) con su estado. */
struct PluginSlot
{
    juce::Uuid id;
    juce::String name;
    juce::String manufacturer;
    juce::String descriptionXml;   // juce::PluginDescription::createXml()
    juce::MemoryBlock state;       // getStateInformation()
    bool bypassed = false;
};

struct AudioClip
{
    juce::Uuid id;
    juce::String name;
    juce::String file;            // nombre del archivo dentro de Audio/
    Beats start = 0;
    double offset = 0;            // segundos dentro del archivo
    double length = 1;            // segundos audibles
    double sourceLength = 1;      // duración total del archivo
    float gainDb = 0;
    double fadeIn = 0.005;        // segundos (fundidos cortos por defecto: sin clicks al cortar)
    double fadeOut = 0.005;
};

struct MidiNote
{
    juce::Uuid id;
    int pitch = 60;
    int velocity = 100;
    int channel = 1;              // 1…16
    Beats start = 0;              // relativo al ítem
    Beats length = 1;
    Beats end() const noexcept { return start + length; }
};

/** Controlador continuo grabado (sustain, modulación, expresión, pitch bend…). */
struct MidiControl
{
    Beats time = 0;               // relativo al ítem
    int channel = 1;
    int controller = 64;          // 0…127 = CC; 128 = pitch bend; 129 = aftertouch de canal
    int value = 0;                // CC 0…127; pitch bend 0…16383
};

struct MidiClip
{
    juce::Uuid id;
    juce::String name;
    Beats start = 0;
    Beats length = 4;
    std::vector<MidiNote> notes;
    std::vector<MidiControl> controls;

    Beats end() const noexcept { return start + length; }
    std::pair<int, int> pitchRange() const;
    void sortNotes();
    void fitToNotes();
};

struct ChannelEQ
{
    float lowGain = 0, midGain = 0, highGain = 0;
    bool enabled = true;
    bool isFlat() const noexcept { return lowGain == 0 && midGain == 0 && highGain == 0; }
};

/** Envío a un bus (reverb, delay, mezcla de in-ears…). */
struct Send
{
    juce::Uuid id;
    juce::Uuid target = juce::Uuid::null();   // pista de bus
    double levelDb = -12;
    double pan = 0;
    bool preFader = false;        // pre-fader: no depende del volumen de la pista (mezclas de monitoreo)
    bool muted = false;
};

/** Qué controla un carril de automatización. */
struct AutomationTarget
{
    enum class Kind { volume, pan, sendLevel, pluginParam };
    Kind kind = Kind::volume;
    juce::Uuid ref = juce::Uuid::null();   // id del envío o del slot de plugin
    int param = -1;               // índice del parámetro del plugin
    juce::String paramName;       // para mostrarlo aunque el plugin no esté cargado

    bool operator== (const AutomationTarget& o) const noexcept { return kind == o.kind && ref == o.ref && param == o.param; }
    double minValue() const noexcept;
    double maxValue() const noexcept;
    double defaultValue() const noexcept;
    /** Volumen y envíos: escala de fader (más resolución cerca de 0 dB). Resto: lineal. */
    double toNormalised (double value) const noexcept;
    double fromNormalised (double norm) const noexcept;
    juce::String formatValue (double value) const;
};

struct AutomationPoint
{
    Beats time = 0;
    double value = 0;
};

struct AutomationLane
{
    juce::Uuid id;
    AutomationTarget target;
    std::vector<AutomationPoint> points;   // ordenados por tiempo; interpolación lineal
    bool visible = true;
    bool active = true;                    // desactivado = se ignora (como "bypass" del envolvente)
    bool writing = false;                  // transitorio (no se guarda): se está escribiendo en touch/latch

    /** Valor en `t` (antes del primer punto = primer valor; después del último = último valor). */
    double valueAt (Beats t) const noexcept;
    void sortPoints();
};

struct Track
{
    juce::Uuid id;
    juce::String name;
    juce::String colour = "violet";
    TrackRole role = TrackRole::audio;
    double volumeDb = 0;
    double pan = 0;
    bool muted = false, solo = false, armed = false;
    TrackInput input;
    MonitorMode monitor = MonitorMode::off;
    ChannelEQ eq;
    std::vector<PluginSlot> inserts;
    std::optional<PluginSlot> instrument;
    std::vector<AudioClip> clips;
    std::vector<MidiClip> midiClips;
    /** 0 = al Master. N = salida directa de hardware N (par estéreo N, base 1), p. ej. click o guía a los in-ears. */
    int directOutPair = 0;
    /** Bus de destino (vacío = Master). Como la salida de una pista hija hacia su carpeta en REAPER. */
    juce::Uuid outputBus = juce::Uuid::null();
    std::vector<Send> sends;
    std::vector<AutomationLane> automation;
    AutomationMode automationMode = AutomationMode::read;

    bool isInstrument() const noexcept { return role == TrackRole::instrument; }
    bool isBus() const noexcept { return role == TrackRole::bus; }
    AutomationLane* findLane (const AutomationTarget&);
    const AutomationLane* findLane (const AutomationTarget&) const;
    /** Carril que manda en la reproducción (modo distinto de apagado, activo, con puntos y sin escritura en curso). */
    const AutomationLane* playingLane (const AutomationTarget&) const;
};

struct Section
{
    juce::Uuid id;
    juce::String name;
    SectionKind kind = SectionKind::verse;
    Beats start = 0, length = 32;
    Beats end() const noexcept { return start + length; }
};

struct Marker
{
    juce::Uuid id;
    int number = 1;
    juce::String name;
    Beats position = 0;
};

struct Project
{
    static constexpr int currentVersion = 2;

    juce::Uuid id;
    juce::String name = juce::String::fromUTF8 ("Nueva canción");
    double bpm = 72;
    int beatsPerBar = 4;
    juce::String key = "G";
    std::vector<Track> tracks;
    std::vector<Section> sections;
    std::vector<Marker> markers;
    Beats loopStart = 0, loopEnd = 0;
    bool loopEnabled = false;
    bool clickEnabled = true;
    double clickVolumeDb = -6;
    int clickOutPair = 0;                // 0 = Master; N = salida directa N (in-ears)
    double masterVolumeDb = 0;
    std::vector<PluginSlot> masterInserts;
    juce::Time created = juce::Time::getCurrentTime(), modified = juce::Time::getCurrentTime();

    Timeline timeline() const noexcept { return { bpm, beatsPerBar }; }

    Track* findTrack (const juce::Uuid&);
    const Track* findTrack (const juce::Uuid&) const;
    int trackIndex (const juce::Uuid&) const;
    bool hasSolo() const;
    bool isAudible (const Track&) const;

    /** Buses a los que manda una pista (salida + envíos). */
    std::vector<juce::Uuid> destinations (const Track&) const;
    /** ¿La señal de `from` llega (directa o indirectamente) a `to`? */
    bool feedsInto (const juce::Uuid& from, const juce::Uuid& to) const;
    /** ¿Se puede mandar `from` a `bus` sin crear un bucle? */
    bool canRouteTo (const juce::Uuid& from, const juce::Uuid& bus) const;
    std::vector<const Track*> buses() const;

    /** Final del contenido en beats. */
    Beats contentEnd() const;
    Beats audioClipEnd (const AudioClip&) const;
    std::vector<juce::String> referencedAudioFiles() const;
};

//==============================================================================
/** Utilidades de dB. */
struct Decibels
{
    static constexpr double silence = -60.0;
    static float toGain (double db) noexcept { return db <= silence ? 0.0f : (float) std::pow (10.0, db / 20.0); }
    /** Escala de fader: más resolución cerca de 0 dB (0 dB ≈ 78 % del recorrido, +6 dB arriba). */
    static double faderPosition (double db) noexcept { return db <= silence ? 0.0 : std::pow (juce::jmin (66.0, db - silence) / 66.0, 2.2); }
    static double faderDb (double pos) noexcept { return pos <= 0.0005 ? silence : silence + std::pow (juce::jlimit (0.0, 1.0, pos), 1.0 / 2.2) * 66.0; }
};

} // namespace wdaw
