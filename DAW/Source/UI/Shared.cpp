#include "Shared.h"

namespace wdaw::ui
{

void drawMeter (juce::Graphics& g, juce::Rectangle<float> r, float level, bool vertical)
{
    g.setColour (juce::Colour (0x66000000));
    g.fillRoundedRectangle (r, 1.5f);
    const auto pos = (float) faderPosition (juce::Decibels::gainToDecibels (level, (float) Decibels::silence));
    if (pos <= 0) return;
    juce::ColourGradient grad = vertical
        ? juce::ColourGradient (juce::Colour (0xff4de699), r.getBottomLeft(), colours::record, r.getTopLeft(), false)
        : juce::ColourGradient (juce::Colour (0xff4de699), r.getTopLeft(), colours::record, r.getTopRight(), false);
    grad.addColour (0.75, juce::Colour (0xfff2d94d));
    g.setGradientFill (grad);
    if (vertical) g.fillRoundedRectangle (r.withTop (r.getBottom() - r.getHeight() * juce::jmin (1.0f, pos)), 1.5f);
    else          g.fillRoundedRectangle (r.withWidth (r.getWidth() * juce::jmin (1.0f, pos)), 1.5f);
    if (level >= 1.0f)
    {
        g.setColour (colours::record);
        if (vertical) g.fillRect (r.withHeight (3.0f));
        else g.fillRect (r.withLeft (r.getRight() - 3.0f));
    }
}

void addTrackInputMenus (juce::PopupMenu& menu, Session& s, const Track& t)
{
    const auto id = t.id;
    if (t.isInstrument())
    {
        menu.addSectionHeader ("Entrada de grabación"_u);
        menu.addItem ("MIDI: todos los controladores", false, true, nullptr);
    }
    else
    {
        const int numIns = juce::jmax (2, s.engine.numInputChannels());
        menu.addSectionHeader ("Entrada de grabación"_u);
        menu.addItem ("Sin entrada", true, t.input.isOff(), [&s, id] { s.updateTrack (id, [] (Track& x) { x.input.kind = TrackInput::Kind::off; x.armed = false; }); });
        juce::PopupMenu mono, stereo;
        for (int c = 0; c < numIns; ++c)
        {
            TrackInput in { TrackInput::Kind::mono, c };
            mono.addItem (in.displayName(), true, t.input == in, [&s, id, in] { s.updateTrack (id, [in] (Track& x) { x.input = in; x.armed = true; }); });
        }
        for (int c = 0; c + 1 < numIns; c += 2)
        {
            TrackInput in { TrackInput::Kind::stereo, c };
            stereo.addItem (in.displayName(), true, t.input == in, [&s, id, in] { s.updateTrack (id, [in] (Track& x) { x.input = in; x.armed = true; }); });
        }
        menu.addSubMenu ("Mono", mono);
        menu.addSubMenu (juce::String::fromUTF8 ("Estéreo"), stereo);
        menu.addSectionHeader ("Monitoreo de entrada");
        for (auto m : { MonitorMode::off, MonitorMode::on, MonitorMode::autoTape })
            menu.addItem (displayName (m), true, t.monitor == m, [&s, id, m] { s.updateTrack (id, [m] (Track& x) { x.monitor = m; }); });
    }
    menu.addSectionHeader ("Salida");
    juce::PopupMenu outs;
    outs.addItem ("Master", true, t.directOutPair < 2, [&s, id] { s.updateTrack (id, [] (Track& x) { x.directOutPair = 0; }); });
    const int pairs = s.engine.numOutputChannels() / 2;
    for (int p = 2; p <= juce::jmax (2, pairs); ++p)
        outs.addItem ("Salidas " + juce::String (p * 2 - 1) + "/" + juce::String (p * 2) + " (directo, p. ej. in-ears)",
                      p <= pairs, t.directOutPair == p, [&s, id, p] { s.updateTrack (id, [p] (Track& x) { x.directOutPair = p; }); });
    menu.addSubMenu ("Enviar a", outs);
}

void addMidiLearnMenu (juce::PopupMenu& menu, Session& s, const Track& t)
{
    juce::PopupMenu learn;
    using T = MidiMapping::Target;
    for (auto target : { T::trackVolume, T::trackPan, T::trackMute, T::trackSolo, T::trackArm })
    {
        auto label = MidiMapping::targetLabel (target);
        if (auto* m = s.midiLearn.find (target, t.id)) label << "  —  "_u << m->sourceLabel();
        learn.addItem (label, [&s, target, id = t.id] { s.midiLearn.startLearning (target, id); s.sendChangeMessage(); });
    }
    learn.addSeparator();
    learn.addItem ("Quitar asignaciones de esta pista", [&s, id = t.id]
    {
        for (auto target : { T::trackVolume, T::trackPan, T::trackMute, T::trackSolo, T::trackArm })
            if (auto* m = s.midiLearn.find (target, id)) s.midiLearn.remove (*m);
        s.midiLearn.saveProject (s.folder);
    });
    menu.addSubMenu ("Aprender MIDI", learn);
}

} // namespace wdaw::ui
