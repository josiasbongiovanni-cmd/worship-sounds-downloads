#pragma once

#include "AudioEngine.h"

namespace wdaw
{

struct ExportOptions
{
    enum class Format { wav, flac, aiff, ogg };
    juce::File destination;          // archivo de la mezcla (para stems: carpeta con un archivo por pista)
    Format format = Format::wav;
    int bitDepth = 24;               // 16, 24 o 32 (flotante, solo WAV)
    double sampleRate = 0;           // 0 = la del dispositivo
    bool normalize = false;
    double normalizePeakDb = -0.3;
    bool stems = false;              // además de la mezcla, un archivo por pista
    Beats start = 0;
    std::optional<Beats> end;        // vacío = hasta el final del contenido
    double tailSeconds = 2.0;        // para colas de reverb
};

struct ExportResult
{
    bool ok = false;
    juce::String error;
    std::vector<juce::File> files;
    float peakDb = -100;
};

juce::String extensionFor (ExportOptions::Format);

/** Renderiza fuera de tiempo real (más rápido que la reproducción, con todos los plugins y la compensación
    de latencia). `progress` devuelve false para cancelar. Puede llamarse desde un hilo en segundo plano. */
ExportResult exportProject (AudioEngine&, const Project&, const juce::File& projectFolder, const ExportOptions&,
                            std::function<bool (float)> progress = {});

} // namespace wdaw
