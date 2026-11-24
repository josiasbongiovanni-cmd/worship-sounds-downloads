#pragma once

#include "Shared.h"

namespace wdaw::ui
{

/** Fader vertical en dB con medidor integrado. Doble clic = 0 dB. */
class Fader : public juce::Component
{
public:
    std::function<void()> onBegin, onEnd;
    std::function<void (double)> onChange;
    double db = 0;
    float level = 0;
    juce::Colour tint = colours::accent;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool dragging = false;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
private:
    double startPos = 0;
};

class ChannelStrip;

class Mixer : public juce::Component, private juce::ChangeListener
{
public:
    explicit Mixer (Context&);
    ~Mixer() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void rebuild();
    void repaintMeters();

private:
    Context& ctx;
    juce::Viewport viewport;
    juce::Component content;
    juce::OwnedArray<ChannelStrip> strips;
    std::unique_ptr<ChannelStrip> master;
    void changeListenerCallback (juce::ChangeBroadcaster*) override { rebuild(); }
};

} // namespace wdaw::ui
