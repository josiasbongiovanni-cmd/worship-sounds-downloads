#include "Exporter.h"
#include "Model/Serialization.h"

namespace wdaw
{

juce::String extensionFor (ExportOptions::Format f)
{
    switch (f)
    {
        case ExportOptions::Format::flac: return ".flac";
        case ExportOptions::Format::aiff: return ".aiff";
        case ExportOptions::Format::ogg:  return ".ogg";
        default:                          return ".wav";
    }
}

static std::unique_ptr<juce::AudioFormat> makeFormat (ExportOptions::Format f)
{
    switch (f)
    {
        case ExportOptions::Format::flac: return std::make_unique<juce::FlacAudioFormat>();
        case ExportOptions::Format::aiff: return std::make_unique<juce::AiffAudioFormat>();
        case ExportOptions::Format::ogg:  return std::make_unique<juce::OggVorbisAudioFormat>();
        default:                          return std::make_unique<juce::WavAudioFormat>();
    }
}

static std::unique_ptr<juce::AudioFormatWriter> makeWriter (const juce::File& f, juce::AudioFormat& fmt, double sr, int channels, int bits, int quality)
{
    f.deleteFile();
    std::unique_ptr<juce::OutputStream> stream (f.createOutputStream());
    if (stream == nullptr) return {};
    std::unique_ptr<juce::AudioFormatWriter> w (fmt.createWriterFor (stream.get(), sr, (unsigned) channels, bits, {}, quality));
    if (w != nullptr) stream.release();
    return w;
}

/** Segunda pasada: normaliza, cambia la frecuencia, aplica dither a 16 bits y escribe el formato final. */
static juce::String finalise (const juce::File& tempFloat, const juce::File& dest, const ExportOptions& o, double renderRate, float gain)
{
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (tempFloat.createInputStream().release(), true));
    if (reader == nullptr) return "No se pudo leer el render temporal.";
    auto fmt = makeFormat (o.format);
    const double outRate = o.sampleRate > 0 ? o.sampleRate : renderRate;
    int bits = o.bitDepth;
    if (o.format != ExportOptions::Format::wav && bits == 32) bits = 24;
    if (o.format == ExportOptions::Format::ogg) bits = 16;
    auto writer = makeWriter (dest, *fmt, outRate, 2, bits, o.format == ExportOptions::Format::ogg ? 8 : 0);
    if (writer == nullptr) return "No se pudo crear \"" + dest.getFileName() + "\" (¿carpeta sin permisos?)."_u;

    const double ratio = renderRate / outRate;
    juce::LagrangeInterpolator interp[2];
    juce::Random rng;
    const int chunk = 16384;
    juce::AudioBuffer<float> in (2, (int) std::ceil (chunk * ratio) + 16), out (2, chunk);
    juce::int64 readPos = 0;
    const auto total = reader->lengthInSamples;
    while (readPos < total)
    {
        int produced;
        if (std::abs (ratio - 1.0) < 1.0e-9)
        {
            produced = (int) juce::jmin ((juce::int64) chunk, total - readPos);
            reader->read (&out, 0, produced, readPos, true, true);
            readPos += produced;
        }
        else
        {
            const int need = (int) std::ceil (chunk * ratio) + 8;
            in.clear();
            reader->read (&in, 0, need, readPos, true, true);
            produced = (int) juce::jmin ((juce::int64) chunk, (juce::int64) std::ceil ((double) (total - readPos) / ratio));
            if (produced <= 0) break;
            int used = 0;
            for (int c = 0; c < 2; ++c) used = interp[c].process (ratio, in.getReadPointer (c), out.getWritePointer (c), produced);
            readPos += juce::jmax (1, used);
        }
        out.applyGain (0, produced, gain);
        if (bits == 16)
        {
            // Dither triangular (TPDF) al reducir a 16 bits
            const float lsb = 1.0f / 32768.0f;
            for (int c = 0; c < 2; ++c)
            {
                auto* d = out.getWritePointer (c);
                for (int i = 0; i < produced; ++i) d[i] += (rng.nextFloat() - rng.nextFloat()) * lsb;
            }
        }
        writer->writeFromAudioSampleBuffer (out, 0, produced);
    }
    return {};
}

ExportResult exportProject (AudioEngine& engine, const Project& original, const juce::File& folder, const ExportOptions& o,
                            std::function<bool (float)> progress)
{
    ExportResult result;
    auto p = original;
    p.clickEnabled = false;            // el click nunca va en la mezcla
    p.loopEnabled = false;
    for (auto& t : p.tracks) t.armed = false;   // sin monitoreo de entrada

    const auto endBeats = o.end ? *o.end : p.contentEnd();
    if (endBeats <= o.start)
    {
        result.error = juce::String::fromUTF8 ("El proyecto está vacío: no hay nada para renderizar.");
        return result;
    }

    auto runOnMessageThread = [] (std::function<void()> fn)
    {
        if (juce::MessageManager::getInstance()->isThisTheMessageThread()) fn();
        else juce::MessageManager::callSync (fn);
    };

    runOnMessageThread ([&] { engine.setProject (p, folder); engine.beginOffline(); });

    const double sr = engine.getSampleRate();
    const auto snap = engine.currentSnapshot();
    const int latency = snap != nullptr ? snap->maxLatency : 0;
    const auto seconds = p.timeline().toSeconds (endBeats - o.start) + o.tailSeconds;
    const auto total = (juce::int64) (seconds * sr);

    const auto tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("wdaw-render-" + juce::Uuid().toString());
    tempDir.createDirectory();
    juce::WavAudioFormat wav;
    auto mixTemp = tempDir.getChildFile ("mix.wav");
    auto mixWriter = makeWriter (mixTemp, wav, sr, 2, 32, 0);
    std::vector<std::unique_ptr<juce::AudioFormatWriter>> stemWriters;
    std::vector<juce::AudioBuffer<float>> stemBuffers;
    const int block = 1024;
    if (o.stems)
        for (size_t i = 0; i < p.tracks.size(); ++i)
        {
            stemWriters.push_back (makeWriter (tempDir.getChildFile ("stem" + juce::String ((int) i) + ".wav"), wav, sr, 2, 32, 0));
            stemBuffers.emplace_back (2, block);
        }

    float peak = 0;
    std::vector<float> stemPeaks (p.tracks.size(), 0.0f);
    juce::AudioBuffer<float> buf (juce::jmax (2, engine.numOutputChannels()), block);
    engine.play (o.start);
    juce::int64 done = -latency;   // las primeras muestras son la latencia de los plugins: se descartan
    bool cancelled = false;
    while (done < total && mixWriter != nullptr)
    {
        for (auto& sb : stemBuffers) sb.clear();
        engine.renderOffline (buf, block, o.stems ? &stemBuffers : nullptr);
        int from = 0, count = block;
        if (done < 0) { from = (int) juce::jmin ((juce::int64) block, -done); count = block - from; }
        count = (int) juce::jmin ((juce::int64) count, total - juce::jmax ((juce::int64) 0, done));
        if (count > 0)
        {
            peak = juce::jmax (peak, buf.getMagnitude (0, from, count), buf.getMagnitude (1, from, count));
            mixWriter->writeFromAudioSampleBuffer (buf, from, count);
            for (size_t i = 0; i < stemWriters.size(); ++i)
                if (stemWriters[i] != nullptr)
                {
                    stemPeaks[i] = juce::jmax (stemPeaks[i], stemBuffers[i].getMagnitude (from, count));
                    stemWriters[i]->writeFromAudioSampleBuffer (stemBuffers[i], from, count);
                }
        }
        done += block;
        if (progress && ! progress ((float) juce::jlimit (0.0, 0.95, (double) done / (double) total * 0.95)))
        {
            cancelled = true;
            break;
        }
    }
    mixWriter.reset();
    stemWriters.clear();
    runOnMessageThread ([&] { engine.endOffline(); engine.setProject (original, folder); });

    if (cancelled) { result.error = "Render cancelado."; tempDir.deleteRecursively(); return result; }

    const auto targetGain = [&] (float pk)
    {
        if (! o.normalize || pk <= 1.0e-6f) return 1.0f;
        return juce::Decibels::decibelsToGain ((float) o.normalizePeakDb) / pk;
    };

    auto dest = o.destination.withFileExtension (extensionFor (o.format));
    if (auto err = finalise (mixTemp, dest, o, sr, targetGain (peak)); err.isNotEmpty())
    {
        result.error = err;
        tempDir.deleteRecursively();
        return result;
    }
    result.files.push_back (dest);
    if (o.stems)
    {
        auto stemDir = dest.getParentDirectory().getChildFile (dest.getFileNameWithoutExtension() + " - Stems");
        stemDir.createDirectory();
        for (size_t i = 0; i < p.tracks.size(); ++i)
        {
            auto f = stemDir.getChildFile (juce::String ((int) i + 1).paddedLeft ('0', 2) + " " + io::sanitize (p.tracks[i].name) + extensionFor (o.format));
            if (finalise (tempDir.getChildFile ("stem" + juce::String ((int) i) + ".wav"), f, o, sr, targetGain (stemPeaks[i])).isEmpty())
                result.files.push_back (f);
        }
    }
    tempDir.deleteRecursively();
    if (progress) progress (1.0f);
    result.ok = true;
    result.peakDb = juce::Decibels::gainToDecibels (peak * targetGain (peak));
    return result;
}

} // namespace wdaw
