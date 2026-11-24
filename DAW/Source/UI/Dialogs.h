#pragma once

#include "Shared.h"
#include "Engine/Exporter.h"

namespace wdaw::ui
{

/** Ajustes de la app que se guardan entre sesiones. */
struct AppSettings
{
    juce::ApplicationProperties props;
    AppSettings();
    juce::PropertiesFile& file() { return *props.getUserSettings(); }
    juce::File projectsFolder();
    /** Entradas MIDI que no van a los instrumentos (desactivadas + la de la superficie de control). */
    juce::StringArray excludedMidiInputs();
    /** Abre la superficie de control con lo guardado; devuelve el estado. Lo define la ventana principal. */
    std::function<juce::String()> applySurface;
    std::function<juce::String()> surfaceStatus;
};

/** Preferencias (Ctrl/Cmd+,): Audio (dispositivo, buffer, latencia), Grabación, MIDI, Plugins y Atajos. */
class PreferencesPanel : public juce::Component
{
public:
    PreferencesPanel (AudioEngine&, PluginManager&, MidiLearn&, AppSettings&, int initialTab = 0);
    void resized() override;
    void paint (juce::Graphics&) override;
    static void show (AudioEngine&, PluginManager&, MidiLearn&, AppSettings&, int initialTab = 0);
private:
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
};

/** Ventana con la interfaz propia del plugin (tamaño según el plugin, redimensionable si el plugin lo permite). */
class PluginWindow : public juce::DocumentWindow
{
public:
    PluginWindow (PluginInstance::Ptr, const juce::String& title, std::function<void()> onClose);
    ~PluginWindow() override;
    void closeButtonPressed() override { if (onCloseFn) onCloseFn(); }
    juce::Uuid slotId() const { return instance->slotId; }
private:
    PluginInstance::Ptr instance;
    std::function<void()> onCloseFn;
};

/** Menú para elegir un plugin (Worship Sounds primero, luego por fabricante) + instrumentos incluidos. */
void choosePlugin (PluginManager&, bool instruments, juce::Component* target, std::function<void (PluginSlot)> onChosen,
                   std::function<void()> onScan);

/** Render: formato, bits, frecuencia, normalizar, stems, rango. */
void showExportDialog (Session&, AppSettings&);

/** Nuevo proyecto: nombre, plantilla, tempo, tonalidad, compás. */
void showNewProjectDialog (std::function<void (juce::String name, edit::Template, double bpm, juce::String key, int beats)>,
                           edit::Template initial = edit::Template::fullBand);

/** Pide un texto (renombrar, etc.). */
void askText (const juce::String& title, const juce::String& current, std::function<void (juce::String)>);

/** Confirmación con dos botones. */
void confirm (const juce::String& title, const juce::String& message, const juce::String& okText, std::function<void()> onOk);

} // namespace wdaw::ui
