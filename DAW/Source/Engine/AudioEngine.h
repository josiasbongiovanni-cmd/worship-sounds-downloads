#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_dsp/juce_dsp.h>
#include <map>
#include <set>
#include "PluginManager.h"
#include "AudioFiles.h"
#include "Model/Model.h"

namespace wdaw
{

//==============================================================================
/** Línea de retardo para compensar la latencia de plugins (PDC) de una pista. */
struct DelayLine : juce::ReferenceCountedObject
{
    using Ptr = juce::ReferenceCountedObjectPtr<DelayLine>;
    explicit DelayLine (int capacity);
    void process (float* const* channels, int numChannels, int numSamples, int delaySamples) noexcept;
    int capacity() const noexcept { return buffer.getNumSamples(); }
    juce::AudioBuffer<float> buffer;
    int writePos = 0;
};

/** Estado de DSP de una pista que persiste entre cambios del proyecto (filtros, suavizados, voces). */
struct TrackDSP : juce::ReferenceCountedObject
{
    using Ptr = juce::ReferenceCountedObjectPtr<TrackDSP>;
    void prepare (double sampleRate, int maxBlock);

    juce::AudioBuffer<float> buffer, instrumentBuffer, scratch;
    juce::MidiBuffer midi, emptyMidi;
    std::array<juce::dsp::IIR::Filter<float>, 6> eq;     // 3 bandas × 2 canales
    std::array<juce::dsp::IIR::Coefficients<float>*, 3> eqCurrent {};
    juce::LinearSmoothedValue<float> gainL { 1.0f }, gainR { 1.0f };
    std::atomic<float> peak { 0.0f };
    bool notesOffPending = false;
    DelayLine::Ptr pdc;

    struct Voice
    {
        juce::Uuid clip;
        juce::int64 nextOut = -1;     // muestra de la línea de tiempo esperada en el próximo bloque
        juce::int64 inputPos = 0;     // próxima muestra del archivo a leer
        juce::LagrangeInterpolator interp[2];
        juce::uint32 lastUsed = 0;
    };
    std::array<Voice, 8> voices;
    juce::uint32 voiceClock = 0;
};

//==============================================================================
/** Lo que el hilo de audio necesita para reproducir. Inmutable: se reemplaza entero al editar. */
struct PlaySnapshot : juce::ReferenceCountedObject
{
    using Ptr = juce::ReferenceCountedObjectPtr<PlaySnapshot>;

    struct Clip
    {
        juce::Uuid id;
        juce::int64 start = 0, length = 0;     // en muestras del dispositivo
        double fileStart = 0;                  // muestra del archivo al inicio del ítem
        double ratio = 1;                      // frecuencia del archivo / frecuencia del dispositivo
        MappedAudioFile::Ptr file;
        float gain = 1;
        int fadeIn = 0, fadeOut = 0;
    };

    struct MidiEvent { juce::int64 pos; juce::MidiMessage message; };

    struct Track
    {
        juce::Uuid id;
        TrackDSP::Ptr dsp;
        float gainL = 1, gainR = 1;
        std::vector<Clip> clips;
        std::vector<MidiEvent> midi;
        PluginInstance::Ptr instrument;
        std::vector<PluginInstance::Ptr> inserts;
        std::vector<bool> bypassed;
        std::array<juce::dsp::IIR::Coefficients<float>::Ptr, 3> eq;
        bool eqOn = false;
        int pdcDelay = 0;
        bool armed = false, isInstrument = false;
        TrackInput input;
        MonitorMode monitor = MonitorMode::off;
        int directOutPair = 0;
    };

    double sampleRate = 48000, samplesPerBeat = 24000;
    int beatsPerBar = 4;
    std::vector<Track> tracks;
    std::vector<PluginInstance::Ptr> master;
    std::vector<bool> masterBypassed;
    float masterGain = 1;
    bool clickOn = true;
    float clickGain = 0.5f;
    int clickOutPair = 0;
    bool loopOn = false;
    juce::int64 loopStart = 0, loopEnd = 0;
    std::vector<int> liveTargets;   // índices de pistas que reciben el MIDI en vivo
    int maxLatency = 0;
};

//==============================================================================
/** Grabación en curso: un archivo WAV por pista armada, escrito en un hilo aparte. */
struct RecordSession : juce::ReferenceCountedObject
{
    using Ptr = juce::ReferenceCountedObjectPtr<RecordSession>;
    struct Take
    {
        juce::Uuid trackId;
        int chL = 0, chR = 0, numChannels = 1;
        juce::File file;
        std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writer;
    };
    std::vector<Take> takes;
    juce::int64 recordStart = 0;
    bool recordMidi = false;
    std::atomic<juce::int64> samplesWritten { 0 };
    std::atomic<bool> overflow { false };
};

//==============================================================================
class AudioEngine : public juce::AudioIODeviceCallback,
                    private juce::MidiInputCallback,
                    private juce::Timer
{
public:
    explicit AudioEngine (PluginManager&);
    ~AudioEngine() override;

    //--- Dispositivos (Preferencias › Audio, como REAPER: CoreAudio / ASIO / WASAPI)
    juce::AudioDeviceManager devices;
    juce::String openDevices (const juce::XmlElement* savedState);
    /** Activa todas las entradas y salidas del dispositivo (para elegir Entrada N y salidas directas). */
    void enableAllChannels();
    int numInputChannels() const noexcept  { return numInputs.load(); }
    int numOutputChannels() const noexcept { return numOutputs.load(); }
    double getSampleRate() const noexcept  { return sampleRate.load(); }
    int getBlockSize() const noexcept      { return blockSize.load(); }
    /** Latencia de ida y vuelta que reporta el driver (segundos). */
    double reportedRoundTripSeconds() const;
    float cpuLoad() const;

    //--- MIDI de entrada
    /** Se llama en el hilo MIDI; devuelve true si el mensaje se usó (Aprender MIDI / asignaciones) y no debe sonar. */
    std::function<bool (const juce::MidiMessage&)> midiFilter;
    void refreshMidiInputs (const juce::StringArray& disabledIdentifiers);
    juce::Array<juce::MidiDeviceInfo> availableMidiInputs() const { return juce::MidiInput::getAvailableDevices(); }

    //--- Proyecto
    void setProject (const Project&, const juce::File& folder);
    void setLiveMidiTargets (const std::vector<juce::Uuid>&);
    /** Latencia manual adicional para alinear grabaciones (ms, positivo = adelanta la toma). */
    void setRecordingOffsetMs (double ms) { recordingOffsetMs = ms; }
    void setUseReportedLatency (bool b) { useReportedLatency = b; }

    //--- Transporte (hilo de mensajes)
    void play (Beats from);
    void stop();
    void seek (Beats position);
    bool isPlaying() const noexcept { return playing.load(); }
    /** Posición que se escucha (descontando la latencia de salida), en beats. */
    Beats playheadBeats() const;
    juce::int64 positionSamples() const noexcept { return position.load(); }

    //--- Grabación
    struct RecordRequest
    {
        Beats recordStart = 0;         // donde empieza la toma (cursor de edición)
        Beats playFrom = 0;            // donde arranca el transporte (antes, si hay cuenta previa)
        std::vector<juce::Uuid> audioTracks, midiTracks;
        bool keepTransport = false;    // punch-in: grabar sin reiniciar la reproducción
    };
    juce::Result startRecording (const RecordRequest&, const Project&, const juce::File& projectFolder);
    struct TakeResult { juce::Uuid trackId; juce::String fileName; double offsetSeconds = 0, lengthSeconds = 0, sourceSeconds = 0; };
    struct RecordingResult
    {
        Beats start = 0, end = 0;
        std::vector<TakeResult> audio;
        std::vector<std::pair<juce::Uuid, MidiClip>> midi;
        bool overflow = false;
    };
    RecordingResult stopRecording();
    bool isRecording() const noexcept { return recording.load(); }

    //--- Medidores (la interfaz los lee y aplica su propia caída)
    float takeTrackPeak (const juce::Uuid&);
    float takeMasterPeak();
    float takeInputPeak (int channel);

    //--- Plugins
    PluginInstance::Ptr findPlugin (const juce::Uuid& slotId) const;
    /** Estado actual de todos los plugins (para guardar). Solo al guardar o cerrar: puede ser lento. */
    std::map<juce::Uuid, juce::MemoryBlock> capturePluginStates() const;
    void previewNote (const juce::Uuid& trackId, int pitch, int velocity = 100);
    /** Apaga todas las notas (botón de pánico). */
    void panic() noexcept { panicRequested = true; }
    std::function<void (juce::String)> onError;
    std::function<void()> onPluginsLoaded;

    //--- Render offline (exportar)
    void beginOffline();
    void endOffline();
    /** Renderiza `numSamples` del transporte (que debe estar en reproducción). `stems` = una salida por pista. */
    void renderOffline (juce::AudioBuffer<float>& out, int numSamples, std::vector<juce::AudioBuffer<float>>* stems = nullptr);
    PlaySnapshot::Ptr currentSnapshot() const;

    //--- Pruebas sin dispositivo
    void prepareForTesting (double sampleRate, int blockSize, int numInputs, int numOutputs);
    void processForTesting (const float* const* inputs, int numIns, float* const* outputs, int numOuts, int numSamples);
    void injectMidiForTesting (const juce::MidiMessage& m) { handleIncomingMidiMessage (nullptr, m); }

    // juce::AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext (const float* const* in, int numIns, float* const* out, int numOuts,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;
    void audioDeviceError (const juce::String&) override;

private:
    PluginManager& pluginManager;
    AudioFileCache fileCache;
    Project project;
    juce::File projectFolder;

    std::atomic<double> sampleRate { 48000 };
    std::atomic<int> blockSize { 512 }, numInputs { 0 }, numOutputs { 2 };
    int maxBlock = 4096;

    // Plugins cargados (por id de slot) y DSP por pista (por id de pista)
    std::map<juce::Uuid, PluginInstance::Ptr> plugins;
    std::set<juce::Uuid> pendingPlugins;
    std::map<juce::Uuid, TrackDSP::Ptr> trackDSP;
    std::vector<juce::Uuid> liveTargetIds;

    // Snapshot: el hilo de mensajes publica, el hilo de audio toma con try-lock
    juce::SpinLock snapshotLock;
    PlaySnapshot::Ptr published;      // protegido por snapshotLock
    PlaySnapshot::Ptr audioSnapshot;  // solo hilo de audio
    std::vector<juce::ReferenceCountedObjectPtr<juce::ReferenceCountedObject>> retired;
    void rebuildSnapshot();
    void loadMissingPlugins();
    void timerCallback() override;

    // Transporte
    std::atomic<bool> playing { false }, recording { false }, offline { false }, panicRequested { false };
    std::atomic<juce::int64> position { 0 }, seekRequest { std::numeric_limits<juce::int64>::min() };
    bool wasPlaying = false;
    std::atomic<int> callbacksDone { 0 };
    double recordingOffsetMs = 0;
    bool useReportedLatency = true;

    // Grabación
    juce::SpinLock recordLock;
    RecordSession::Ptr pendingRecord;     // protegido por recordLock
    RecordSession::Ptr audioRecord;       // hilo de audio
    std::atomic<RecordSession*> recordInUse { nullptr };
    juce::TimeSliceThread writerThread { "Grabacion a disco" };
    RecordSession::Ptr activeSession;     // hilo de mensajes
    Beats recordStartBeats = 0;

    struct RecordedMidi { juce::int64 pos; juce::uint8 data[3]; int size; };
    juce::AbstractFifo midiFifo { 65536 };
    std::vector<RecordedMidi> midiFifoData = std::vector<RecordedMidi> (65536);
    std::vector<RecordedMidi> recordedMidi;  // hilo de mensajes
    void drainRecordedMidi();

    // MIDI en vivo y escucha previa
    juce::MidiMessageCollector liveMidi;
    juce::MidiBuffer liveBlock;
    struct Preview { int trackIndex; juce::uint8 data[3]; };
    juce::AbstractFifo previewFifo { 256 };
    std::array<Preview, 256> previewData {};
    std::vector<int> lastLiveTargets;
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;

    // Click
    double clickPhase = 0, clickFreq = 1000, clickEnv = 0, clickAmp = 0, clickDecay = 0.999;

    // Medidores
    std::atomic<float> masterPeak { 0.0f };
    std::array<std::atomic<float>, 64> inputPeaks {};

    // Buffers de mezcla (preparados fuera del hilo de audio)
    juce::AudioBuffer<float> masterBus, directBus, zeros;
    juce::MidiBuffer emptyMidi;

    // Precarga de páginas de audio por delante del cursor
    struct Prefetcher;
    std::unique_ptr<Prefetcher> prefetcher;

    void prepareBuffers (double sampleRate, int blockSize, int numIns, int numOuts);
    void preparePlugins();
    void renderBlock (const float* const* in, int numIns, float* const* out, int numOuts, int numSamples,
                      std::vector<juce::AudioBuffer<float>>* stems);
    void renderSegment (PlaySnapshot& s, const float* const* in, int numIns, float* const* out, int numOuts,
                        int offset, int len, juce::int64 pos, bool isPlaying, RecordSession* rec,
                        std::vector<juce::AudioBuffer<float>>* stems);
    void renderClip (const PlaySnapshot::Clip&, TrackDSP&, juce::int64 segStart, int len) noexcept;
    void renderClick (PlaySnapshot& s, float* L, float* R, int len, juce::int64 timelinePos) noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};

} // namespace wdaw
