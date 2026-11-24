#pragma once

#include "Shared.h"

namespace wdaw::ui
{

/** Editor MIDI (piano roll), como el MIDI Editor de REAPER. */
class PianoRoll : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    explicit PianoRoll (Context&);
    ~PianoRoll() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    Context& ctx;
    double ppb = 90;                 // píxeles por beat
    static constexpr int rowH = 14, keyW = 64, toolbarH = 46, rulerH = 22, velocityH = 90;
    enum class Tool { select, draw } tool = Tool::select;
    Beats grid = 0.25;
    double strength = 1.0;
    bool quantizeLengths = false;
    std::vector<juce::Uuid> selection;

    MidiClip* clip();
    const Track* track() const;
    void quantize();
    void transpose (int semis);
    void nudge (int dir);
    void duplicate();
    void removeSelected();
    void selectAll();
    void preview (int pitch, int velocity = 100);

private:
    class Grid;
    class Keys;
    class Velocity;
    std::unique_ptr<Grid> grid_;
    std::unique_ptr<Keys> keys;
    std::unique_ptr<Velocity> velocity;
    juce::Viewport view;
    juce::TextButton selectTool { "Seleccionar (S)" }, drawTool { "Dibujar (D)" }, gridButton, quantizeButton { "Cuantizar (Q)" },
                     editButton { "Editar" }, playButton { "Reproducir" };
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void updateButtons();
};

} // namespace wdaw::ui
