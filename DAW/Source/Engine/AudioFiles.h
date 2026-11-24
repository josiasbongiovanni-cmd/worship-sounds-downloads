#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include "Model/Model.h"

namespace wdaw
{

/** Archivo WAV del proyecto mapeado en memoria: el hilo de audio lee sin bloquearse
    y varios ítems pueden leer el mismo archivo a la vez (ítems divididos o duplicados). */
struct MappedAudioFile : juce::ReferenceCountedObject
{
    using Ptr = juce::ReferenceCountedObjectPtr<MappedAudioFile>;
    juce::File file;
    std::unique_ptr<juce::MemoryMappedAudioFormatReader> reader;
    juce::int64 length = 0;
    double sampleRate = 48000;
    int numChannels = 0;

    /** Lee al buffer (rellena con ceros fuera de rango). Seguro en el hilo de audio. */
    void read (float* const* dest, int numDestChannels, juce::int64 start, int num) const;
    /** Trae a memoria las páginas de un rango (lo usa el hilo de precarga, no el de audio). */
    void touch (juce::int64 start, juce::int64 num) const;
};

/** Caché de archivos abiertos del proyecto (hilo de mensajes). */
class AudioFileCache
{
public:
    void setProjectFolder (const juce::File&);
    MappedAudioFile::Ptr get (const juce::String& fileName);
    void forget (const juce::String& fileName);
    void clear() { files.clear(); }

private:
    juce::File folder;
    std::map<juce::String, MappedAudioFile::Ptr> files;
};

/** Importa audio (wav, aiff, mp3, flac, ogg, m4a/aac en Mac y Windows) a Audio/ como WAV de 32 bits flotante. */
struct ImportResult
{
    bool ok = false;
    juce::String error;
    juce::String fileName;      // dentro de Audio/
    double lengthSeconds = 0;
    double sampleRate = 0;
    int numChannels = 0;
};

ImportResult importAudioFile (const juce::File& source, const juce::File& projectFolder,
                              std::function<bool (float progress)> progress = {});

/** Formatos que se pueden importar (para el diálogo de archivos). */
juce::String importWildcard();

/** Duración, frecuencia y canales de un archivo del proyecto. */
std::optional<ImportResult> probeAudioFile (const juce::File&);

} // namespace wdaw
