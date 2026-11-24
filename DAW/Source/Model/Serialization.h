#pragma once

#include "Model.h"

/*  Proyecto en disco = carpeta "Nombre.worship/":
        project.json      (texto legible, versionado)
        project.json.bak  (copia anterior, como .rpp-bak de REAPER)
        Backups/          (copias con fecha, las últimas 10)
        Audio/            (grabaciones e importaciones, WAV)
        Exports/          (mezclas renderizadas)
*/
namespace wdaw::io
{

juce::var toVar (const Project&);
/** Lee la versión actual y también los proyectos v1 (app Swift, posiciones en segundos). */
Project fromVar (const juce::var&);

juce::String toJSON (const Project&);
std::optional<Project> fromJSON (const juce::String&);

inline const juce::String extension = ".worship";

juce::File audioFolder (const juce::File& projectFolder);
juce::File exportsFolder (const juce::File& projectFolder);

/** Guarda de forma atómica, rotando una copia .bak y backups con fecha (como REAPER). */
juce::Result save (const Project&, const juce::File& projectFolder);
std::optional<Project> load (const juce::File& projectFolder, juce::String* error = nullptr);

/** Crea una carpeta de proyecto con nombre único dentro de `root`. */
juce::File createProjectFolder (const juce::File& root, const juce::String& name);

/** Nombre seguro para archivos (sin / \ : ? * etc.). */
juce::String sanitize (const juce::String& name);

struct Entry { juce::File folder; juce::String name; double bpm; juce::String key; int trackCount; juce::Time modified; };
std::vector<Entry> listProjects (const juce::File& root);

/** Mueve el proyecto a la Papelera del sistema (no lo borra definitivamente). */
bool moveToTrash (const juce::File& projectFolder);

/** Archivos de Audio/ que ningún ítem usa (para "Limpiar proyecto"). */
std::vector<juce::File> unusedAudioFiles (const Project&, const juce::File& projectFolder);

} // namespace wdaw::io
