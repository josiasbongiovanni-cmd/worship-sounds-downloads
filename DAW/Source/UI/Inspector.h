#pragma once

#include "Shared.h"

namespace wdaw::ui
{

/** Panel lateral: Pista (nombre, color, tipo, grabación, salida, EQ), Canción (tempo, tono, click) y Estructura (secciones). */
class Inspector : public juce::Component, private juce::ChangeListener
{
public:
    explicit Inspector (Context&);
    ~Inspector() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void update();

private:
    Context& ctx;
    int tab = 0;
    juce::TextEditor trackName, songName;
    juce::ComboBox role, input, monitor, output, keyBox, meterBox, countIn, clickOut, automationMode;
    juce::Slider bpm, clickVolume, low, mid, high, volume, pan;
    juce::ToggleButton armed { "Armada para grabar" }, eqOn { "EQ activo" }, clickOn { juce::String::fromUTF8 ("Metrónomo") };
    juce::TextButton chooseInstrument, routing, automationLanes;
    juce::OwnedArray<juce::TextButton> addSectionButtons;
    juce::Rectangle<int> tabsArea, swatchesArea, sectionsArea;
    bool updating = false;
    void changeListenerCallback (juce::ChangeBroadcaster*) override { update(); }
    void layoutTrack (juce::Rectangle<int>);
    void layoutSong (juce::Rectangle<int>);
    void layoutStructure (juce::Rectangle<int>);
    std::vector<std::pair<juce::Rectangle<int>, juce::Uuid>> sectionRows, sectionLoopButtons, sectionDeleteButtons;
};

} // namespace wdaw::ui
