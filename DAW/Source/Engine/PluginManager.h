#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Model/Model.h"

namespace wdaw
{

/** Instancia de plugin compartida entre el motor (hilo de audio) y la interfaz (ventana del plugin). */
struct PluginInstance : juce::ReferenceCountedObject
{
    using Ptr = juce::ReferenceCountedObjectPtr<PluginInstance>;
    juce::Uuid slotId;
    juce::String name;
    std::unique_ptr<juce::AudioProcessor> processor;
    bool isInstrument = false;
};

/** Escaneo, lista de plugins conocidos (VST3 en Mac/Windows, AU en Mac) e instanciación. */
class PluginManager
{
public:
    PluginManager();

    juce::AudioPluginFormatManager formats;
    juce::KnownPluginList known;

    /** Carpeta de configuración de la app (lista de plugins, preferencias, asignaciones MIDI). */
    static juce::File settingsFolder();
    void loadKnownList();
    void saveKnownList();

    /** Escaneo completo en segundo plano. Un plugin que cuelga la app queda en la lista negra
        (archivo "dead man's pedal"), como hace REAPER, y no se vuelve a escanear. */
    void scanAsync (std::function<void (juce::String currentPlugin, float progress)> onProgress,
                    std::function<void()> onFinished);
    bool isScanning() const { return scanning.load(); }

    std::vector<juce::PluginDescription> effects() const;
    std::vector<juce::PluginDescription> instruments() const;

    // Slots
    static PluginSlot makeSlot (const juce::PluginDescription&);
    static PluginSlot internalSlot (const juce::String& internalId);   // "piano", "pad", "latency:256"
    static PluginSlot defaultInstrumentSlot() { return internalSlot ("piano"); }
    static bool isInternal (const PluginSlot& s) { return s.descriptionXml.startsWith ("internal:"); }

    /** Crea y prepara una instancia (asíncrono para plugins externos; inmediato para los internos).
        Restaura el estado guardado del slot. Siempre llama a `done` en el hilo de mensajes. */
    void createInstance (const PluginSlot&, double sampleRate, int blockSize,
                         std::function<void (PluginInstance::Ptr, juce::String error)> done);
    /** Versión síncrona (pruebas y procesadores internos). */
    PluginInstance::Ptr createInstanceSync (const PluginSlot&, double sampleRate, int blockSize, juce::String& error);

    static void prepare (juce::AudioProcessor&, double sampleRate, int blockSize, bool isInstrument);

private:
    std::atomic<bool> scanning { false };
    std::unique_ptr<juce::Thread> scanThread;
};

} // namespace wdaw
