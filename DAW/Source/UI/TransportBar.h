#pragma once

#include "Shared.h"

namespace wdaw::ui
{

class TransportBar : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    explicit TransportBar (Context&);
    ~TransportBar() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    std::function<void()> onHome, onImport, onExport, onPreferences, onToggleMixer, onToggleInspector;
    bool mixerVisible = false, inspectorVisible = true;
    void update();

private:
    Context& ctx;
    IconButton home { IconButton::Icon::home, "Proyectos" },
               toStart { IconButton::Icon::toStart, "Ir al inicio (Inicio)" },
               prev { IconButton::Icon::prev, "Marcador o sección anterior ([)"_u },
               play { IconButton::Icon::play, "Reproducir / Detener (Espacio)" },
               record { IconButton::Icon::record, "Grabar (Ctrl/Cmd+R)" },
               next { IconButton::Icon::next, "Marcador o sección siguiente (])"_u },
               loop { IconButton::Icon::loop, "Repetir (R)" },
               click { IconButton::Icon::metronome, "Metrónomo"_u },
               undo { IconButton::Icon::undo, "Deshacer (Ctrl/Cmd+Z)" },
               importBtn { IconButton::Icon::importAudio, "Importar audio (Ctrl/Cmd+I)" },
               exportBtn { IconButton::Icon::exportAudio, "Renderizar mezcla (Ctrl/Cmd+Alt+R)" },
               prefs { IconButton::Icon::settings, "Preferencias: audio, MIDI y plugins (Ctrl/Cmd+,)" },
               mixer { IconButton::Icon::mixer, "Mezclador (Ctrl/Cmd+M)" },
               inspector { IconButton::Icon::inspector, "Inspector" };
    juce::Rectangle<int> chipsArea, positionArea, titleArea;
    void changeListenerCallback (juce::ChangeBroadcaster*) override { update(); }
    void timerCallback() override { repaint (positionArea); if (ctx.session.isRecording() != record.active) update(); }
    void showSongMenu();
};

} // namespace wdaw::ui
