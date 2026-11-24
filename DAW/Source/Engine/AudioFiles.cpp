#include "AudioFiles.h"
#include "Model/Serialization.h"

namespace wdaw
{

void MappedAudioFile::read (float* const* dest, int numDestChannels, juce::int64 start, int num) const
{
    if (reader == nullptr || num <= 0) return;
    if (start >= length || start + num <= 0)
    {
        for (int c = 0; c < numDestChannels; ++c) juce::FloatVectorOperations::clear (dest[c], num);
        return;
    }
    reader->read (dest, numDestChannels, start, num);
}

void MappedAudioFile::touch (juce::int64 start, juce::int64 num) const
{
    if (reader == nullptr) return;
    const auto end = juce::jmin (length, start + num);
    for (auto s = juce::jmax ((juce::int64) 0, start); s < end; s += 2048)
        reader->touchSample (s);
}

//==============================================================================
/** Lista de formatos compartida mientras haya alguien usándola (sin estáticos que sobrevivan a JUCE). */
struct Formats { juce::AudioFormatManager fm; Formats() { fm.registerBasicFormats(); } };

void AudioFileCache::setProjectFolder (const juce::File& f)
{
    if (f != folder) { files.clear(); folder = f; }
}

MappedAudioFile::Ptr AudioFileCache::get (const juce::String& fileName)
{
    if (auto it = files.find (fileName); it != files.end()) return it->second;
    const auto file = io::audioFolder (folder).getChildFile (fileName);
    if (! file.existsAsFile()) return {};

    MappedAudioFile::Ptr m (new MappedAudioFile());
    m->file = file;
    juce::WavAudioFormat wav;
    m->reader.reset (wav.createMemoryMappedReader (file));
    if (m->reader == nullptr || ! m->reader->mapEntireFile())
    {
        // Archivos que no son WAV (p. ej. .caf de la versión anterior en Mac): convertir una sola vez a WAV.
        auto wavFile = file.withFileExtension ("wav");
        if (! wavFile.existsAsFile())
        {
            auto converted = importAudioFile (file, folder);
            if (! converted.ok) return {};
            io::audioFolder (folder).getChildFile (converted.fileName).moveFileTo (wavFile);
        }
        m->file = wavFile;
        m->reader.reset (wav.createMemoryMappedReader (wavFile));
        if (m->reader == nullptr || ! m->reader->mapEntireFile()) return {};
    }
    m->length = m->reader->lengthInSamples;
    m->sampleRate = m->reader->sampleRate;
    m->numChannels = (int) m->reader->numChannels;
    files[fileName] = m;
    return m;
}

void AudioFileCache::forget (const juce::String& fileName)
{
    files.erase (fileName);
}

//==============================================================================
juce::String importWildcard()
{
    juce::SharedResourcePointer<Formats> f;
    return f->fm.getWildcardForAllFormats();
}

std::optional<ImportResult> probeAudioFile (const juce::File& f)
{
    juce::SharedResourcePointer<Formats> formats;
    std::unique_ptr<juce::AudioFormatReader> r (formats->fm.createReaderFor (f));
    if (r == nullptr) return {};
    ImportResult res;
    res.ok = true;
    res.fileName = f.getFileName();
    res.sampleRate = r->sampleRate;
    res.numChannels = (int) r->numChannels;
    res.lengthSeconds = r->sampleRate > 0 ? (double) r->lengthInSamples / r->sampleRate : 0;
    return res;
}

ImportResult importAudioFile (const juce::File& source, const juce::File& projectFolder, std::function<bool (float)> progress)
{
    ImportResult res;
    juce::SharedResourcePointer<Formats> formats;
    std::unique_ptr<juce::AudioFormatReader> reader (formats->fm.createReaderFor (source));
    if (reader == nullptr)
    {
        res.error = "No se puede leer \"" + source.getFileName() + juce::String::fromUTF8 ("\" (formato no compatible o archivo dañado).");
        return res;
    }
    const auto folder = io::audioFolder (projectFolder);
    folder.createDirectory();
    const auto dest = folder.getNonexistentChildFile (io::sanitize (source.getFileNameWithoutExtension()), ".wav", false);
    const int channels = (int) juce::jlimit (1u, 2u, reader->numChannels == 1 ? 1u : 2u);

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream (dest.createOutputStream());
    if (stream == nullptr) { res.error = "No se pudo crear el archivo en el proyecto (¿sin espacio o sin permisos?)."_u; return res; }
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), reader->sampleRate, (unsigned) channels, 32, {}, 0));
    if (writer == nullptr) { res.error = "No se pudo escribir el WAV."; return res; }
    stream.release();

    const int chunk = 65536;
    juce::AudioBuffer<float> buf ((int) reader->numChannels, chunk);
    for (juce::int64 pos = 0; pos < reader->lengthInSamples; pos += chunk)
    {
        const int n = (int) juce::jmin ((juce::int64) chunk, reader->lengthInSamples - pos);
        reader->read (&buf, 0, n, pos, true, true);
        if (reader->numChannels > 2)
        {
            // Más de dos canales: se queda con los dos primeros (frente izquierdo y derecho).
            juce::AudioBuffer<float> st (2, n);
            st.copyFrom (0, 0, buf, 0, 0, n);
            st.copyFrom (1, 0, buf, 1, 0, n);
            writer->writeFromAudioSampleBuffer (st, 0, n);
        }
        else writer->writeFromAudioSampleBuffer (buf, 0, n);
        if (progress && ! progress ((float) pos / (float) juce::jmax ((juce::int64) 1, reader->lengthInSamples)))
        {
            writer.reset();
            dest.deleteFile();
            res.error = "Importación cancelada."_u;
            return res;
        }
    }
    writer.reset();
    res.ok = true;
    res.fileName = dest.getFileName();
    res.sampleRate = reader->sampleRate;
    res.numChannels = channels;
    res.lengthSeconds = (double) reader->lengthInSamples / reader->sampleRate;
    return res;
}

} // namespace wdaw
