#include "Inspector.h"

namespace wdaw::ui
{

static const juce::StringArray keyNames { "C", "Cm", "C#", "C#m", "Db", "Dbm", "D", "Dm", "Eb", "Ebm", "E", "Em", "F", "Fm", "F#", "F#m",
                                          "Gb", "Gbm", "G", "Gm", "Ab", "Abm", "A", "Am", "Bb", "Bbm", "B", "Bm" };

Inspector::Inspector (Context& c) : ctx (c)
{
    chooseInstrument.setButtonText ("Cambiar instrumento…"_u);
    auto& s = ctx.session;
    auto setupKnob = [this] (juce::Slider& k, juce::Colour col)
    {
        k.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        k.setRange (-12, 12, 0.1);
        k.setDoubleClickReturnValue (true, 0);
        k.setColour (juce::Slider::rotarySliderFillColourId, col);
        k.onDragStart = [this] { ctx.session.beginGesture(); };
    };
    setupKnob (low, colours::named ("violet"));
    setupKnob (mid, colours::named ("violet"));
    setupKnob (high, colours::named ("violet"));
    low.onValueChange = [this] { if (! updating) { const auto v = (float) low.getValue(); ctx.session.updateTrack (ctx.session.selectedTrack, [v] (Track& t) { t.eq.lowGain = v; }, false); } };
    mid.onValueChange = [this] { if (! updating) { const auto v = (float) mid.getValue(); ctx.session.updateTrack (ctx.session.selectedTrack, [v] (Track& t) { t.eq.midGain = v; }, false); } };
    high.onValueChange = [this] { if (! updating) { const auto v = (float) high.getValue(); ctx.session.updateTrack (ctx.session.selectedTrack, [v] (Track& t) { t.eq.highGain = v; }, false); } };

    for (auto* sl : { &volume, &pan, &bpm, &clickVolume })
    {
        sl->setSliderStyle (juce::Slider::LinearHorizontal);
        sl->setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 20);
        sl->onDragStart = [this] { ctx.session.beginGesture(); };
    }
    volume.setRange (0, 1);
    volume.textFromValueFunction = [] (double v) { return formatDb (faderDb (v)) + " dB"; };
    volume.onValueChange = [this] { if (! updating) { const auto db = std::round (faderDb (volume.getValue()) * 10) / 10; ctx.session.updateTrack (ctx.session.selectedTrack, [db] (Track& t) { t.volumeDb = db; }, false); } };
    pan.setRange (-1, 1, 0.01);
    pan.setDoubleClickReturnValue (true, 0);
    pan.textFromValueFunction = [] (double v) { return formatPan (v); };
    pan.onValueChange = [this] { if (! updating) { const auto v = pan.getValue(); ctx.session.updateTrack (ctx.session.selectedTrack, [v] (Track& t) { t.pan = v; }, false); } };
    bpm.setRange (20, 300, 1);
    bpm.textFromValueFunction = [] (double v) { return juce::String ((int) v) + " BPM"; };
    bpm.onValueChange = [this] { if (! updating) { const auto v = bpm.getValue(); ctx.session.edit ([v] (Project& p) { edit::setTempo (p, v); }, false); } };
    clickVolume.setRange (-40, 6, 0.5);
    clickVolume.textFromValueFunction = [] (double v) { return juce::String (v, 1) + " dB"; };
    clickVolume.onValueChange = [this] { if (! updating) { const auto v = clickVolume.getValue(); ctx.session.edit ([v] (Project& p) { p.clickVolumeDb = v; }, false); } };

    for (auto* t : { &trackName, &songName })
    {
        t->setFont (font (14, true));
        t->setIndents (8, 6);
    }
    trackName.onReturnKey = trackName.onFocusLost = [this]
    {
        const auto n = trackName.getText();
        if (auto* t = ctx.session.project().findTrack (ctx.session.selectedTrack); t != nullptr && n.isNotEmpty() && n != t->name)
            ctx.session.updateTrack (t->id, [n] (Track& x) { x.name = n; });
    };
    songName.onReturnKey = songName.onFocusLost = [this]
    {
        const auto n = songName.getText();
        if (n.isNotEmpty() && n != ctx.session.project().name) ctx.session.edit ([n] (Project& p) { p.name = n; });
    };

    role.addItem ("Audio", 1); role.addItem (juce::String::fromUTF8 ("Guía"), 2); role.addItem ("Instrumento (MIDI)", 3);
    role.onChange = [this]
    {
        if (updating) return;
        const auto r = role.getSelectedId() == 3 ? TrackRole::instrument : role.getSelectedId() == 2 ? TrackRole::guide : TrackRole::audio;
        ctx.session.updateTrack (ctx.session.selectedTrack, [r] (Track& t)
        {
            t.role = r;
            if (r == TrackRole::instrument && ! t.instrument) t.instrument = PluginManager::defaultInstrumentSlot();
        });
    };
    monitor.addItem ("Monitoreo: apagado", 1); monitor.addItem ("Monitoreo: encendido", 2); monitor.addItem (juce::String::fromUTF8 ("Monitoreo: automático"), 3);
    monitor.onChange = [this]
    {
        if (updating) return;
        const auto m = monitor.getSelectedId() == 2 ? MonitorMode::on : monitor.getSelectedId() == 3 ? MonitorMode::autoTape : MonitorMode::off;
        ctx.session.updateTrack (ctx.session.selectedTrack, [m] (Track& t) { t.monitor = m; });
    };
    input.onChange = [this]
    {
        if (updating) return;
        const auto id = input.getSelectedId();
        TrackInput in;
        if (id == 1) in.kind = TrackInput::Kind::off;
        else if (id < 1000) { in.kind = TrackInput::Kind::mono; in.channel = id - 100; }
        else { in.kind = TrackInput::Kind::stereo; in.channel = id - 1000; }
        ctx.session.updateTrack (ctx.session.selectedTrack, [in] (Track& t) { t.input = in; if (! in.isOff()) t.armed = true; });
    };
    output.onChange = [this]
    {
        if (updating) return;
        const auto pair = output.getSelectedId() <= 1 ? 0 : output.getSelectedId();
        ctx.session.updateTrack (ctx.session.selectedTrack, [pair] (Track& t) { t.directOutPair = pair; });
    };
    armed.onClick = [this] { ctx.session.updateTrack (ctx.session.selectedTrack, [this] (Track& t) { t.armed = armed.getToggleState(); }); };
    eqOn.onClick = [this] { ctx.session.updateTrack (ctx.session.selectedTrack, [this] (Track& t) { t.eq.enabled = eqOn.getToggleState(); }); };
    chooseInstrument.onClick = [this] { if (ctx.choosePlugin) ctx.choosePlugin (ctx.session.selectedTrack, true); };

    for (int i = 0; i < keyNames.size(); ++i) keyBox.addItem (keyNames[i], i + 1);
    keyBox.onChange = [this] { if (! updating) { const auto k = keyBox.getText(); ctx.session.edit ([k] (Project& p) { p.key = k; }); } };
    for (int b : { 2, 3, 4, 6 }) meterBox.addItem (juce::String (b) + "/4", b);
    meterBox.onChange = [this] { if (! updating) { const auto b = meterBox.getSelectedId(); ctx.session.edit ([b] (Project& p) { p.beatsPerBar = b; }); } };
    countIn.addItem ("Sin cuenta previa", 1); countIn.addItem (juce::String::fromUTF8 ("Cuenta: 1 compás"), 2); countIn.addItem ("Cuenta: 2 compases", 3);
    countIn.onChange = [this] { ctx.session.countInBars = countIn.getSelectedId() - 1; };
    clickOn.onClick = [this] { ctx.session.edit ([this] (Project& p) { p.clickEnabled = clickOn.getToggleState(); }); };
    clickOut.onChange = [this]
    {
        if (updating) return;
        const auto pair = clickOut.getSelectedId() <= 1 ? 0 : clickOut.getSelectedId();
        ctx.session.edit ([pair] (Project& p) { p.clickOutPair = pair; });
    };

    for (int k = 0; k <= (int) SectionKind::outro; ++k)
    {
        auto* b = addSectionButtons.add (new juce::TextButton (displayName ((SectionKind) k)));
        b->setColour (juce::TextButton::buttonColourId, colours::named (defaultColourId ((SectionKind) k)).withAlpha (0.18f));
        b->setColour (juce::TextButton::textColourOffId, colours::named (defaultColourId ((SectionKind) k)));
        b->onClick = [&s, k]
        {
            s.edit ([k] (Project& p)
            {
                int count = 0;
                for (auto& x : p.sections) if (x.kind == (SectionKind) k) ++count;
                auto& sec = edit::addSection (p, (SectionKind) k, (SectionKind) k == SectionKind::intro || (SectionKind) k == SectionKind::outro || (SectionKind) k == SectionKind::preChorus ? 4 : 8);
                if (count > 0) sec.name = displayName ((SectionKind) k) + " " + juce::String (count + 1);
            });
        };
    }

    ctx.session.addChangeListener (this);
    update();
}

Inspector::~Inspector() { ctx.session.removeChangeListener (this); }

void Inspector::update()
{
    const juce::ScopedValueSetter<bool> guard (updating, true);
    auto& s = ctx.session;
    auto& p = s.project();
    for (auto* comp : std::initializer_list<juce::Component*> { &trackName, &role, &armed, &input, &monitor, &output, &low, &mid, &high, &eqOn, &volume, &pan, &chooseInstrument,
                                                                &songName, &bpm, &keyBox, &meterBox, &clickOn, &clickVolume, &countIn, &clickOut })
        removeChildComponent (comp);
    for (auto* b : addSectionButtons) removeChildComponent (b);

    if (tab == 0)
    {
        if (auto* t = p.findTrack (s.selectedTrack))
        {
            for (auto* comp : std::initializer_list<juce::Component*> { &trackName, &role, &output, &volume, &pan })
                addAndMakeVisible (comp);
            if (! trackName.hasKeyboardFocus (true)) trackName.setText (t->name, false);
            role.setSelectedId (t->role == TrackRole::instrument ? 3 : t->role == TrackRole::guide ? 2 : 1, juce::dontSendNotification);
            if (t->isInstrument()) addAndMakeVisible (chooseInstrument);
            else
            {
                for (auto* comp : std::initializer_list<juce::Component*> { &armed, &input, &monitor, &low, &mid, &high, &eqOn })
                    addAndMakeVisible (comp);
                input.clear (juce::dontSendNotification);
                input.addItem ("Sin entrada", 1);
                const int numIns = juce::jmax (2, s.engine.numInputChannels());
                for (int c = 0; c < numIns; ++c) input.addItem ("Entrada " + juce::String (c + 1), 100 + c);
                for (int c = 0; c + 1 < numIns; c += 2) input.addItem ("Entradas " + juce::String (c + 1) + "/" + juce::String (c + 2), 1000 + c);
                input.setSelectedId (t->input.isOff() ? 1 : t->input.kind == TrackInput::Kind::mono ? 100 + t->input.channel : 1000 + t->input.channel, juce::dontSendNotification);
                monitor.setSelectedId (t->monitor == MonitorMode::on ? 2 : t->monitor == MonitorMode::autoTape ? 3 : 1, juce::dontSendNotification);
                low.setValue (t->eq.lowGain, juce::dontSendNotification);
                mid.setValue (t->eq.midGain, juce::dontSendNotification);
                high.setValue (t->eq.highGain, juce::dontSendNotification);
                eqOn.setToggleState (t->eq.enabled, juce::dontSendNotification);
            }
            armed.setToggleState (t->armed, juce::dontSendNotification);
            output.clear (juce::dontSendNotification);
            output.addItem ("Salida: Master", 1);
            for (int pr = 2; pr <= s.engine.numOutputChannels() / 2; ++pr)
                output.addItem ("Salidas " + juce::String (pr * 2 - 1) + "/" + juce::String (pr * 2) + " (directo)", pr);
            output.setSelectedId (t->directOutPair >= 2 ? t->directOutPair : 1, juce::dontSendNotification);
            volume.setValue (faderPosition (t->volumeDb), juce::dontSendNotification);
            pan.setValue (t->pan, juce::dontSendNotification);
            volume.setColour (juce::Slider::trackColourId, colours::named (t->colour));
            for (auto* k : { &low, &mid, &high }) k->setColour (juce::Slider::rotarySliderFillColourId, colours::named (t->colour));
        }
    }
    else if (tab == 1)
    {
        for (auto* comp : std::initializer_list<juce::Component*> { &songName, &bpm, &keyBox, &meterBox, &clickOn, &clickVolume, &countIn, &clickOut })
            addAndMakeVisible (comp);
        if (! songName.hasKeyboardFocus (true)) songName.setText (p.name, false);
        bpm.setValue (p.bpm, juce::dontSendNotification);
        keyBox.setSelectedId (keyNames.indexOf (p.key) + 1, juce::dontSendNotification);
        meterBox.setSelectedId (p.beatsPerBar, juce::dontSendNotification);
        clickOn.setToggleState (p.clickEnabled, juce::dontSendNotification);
        clickVolume.setValue (p.clickVolumeDb, juce::dontSendNotification);
        countIn.setSelectedId (s.countInBars + 1, juce::dontSendNotification);
        clickOut.clear (juce::dontSendNotification);
        clickOut.addItem ("Click al Master", 1);
        for (int pr = 2; pr <= s.engine.numOutputChannels() / 2; ++pr)
            clickOut.addItem ("Click a salidas " + juce::String (pr * 2 - 1) + "/" + juce::String (pr * 2) + " (in-ears)", pr);
        clickOut.setSelectedId (p.clickOutPair >= 2 ? p.clickOutPair : 1, juce::dontSendNotification);
    }
    else
        for (auto* b : addSectionButtons) addAndMakeVisible (b);
    resized();
    repaint();
}

void Inspector::resized()
{
    auto r = getLocalBounds().reduced (14, 12);
    tabsArea = r.removeFromTop (30);
    r.removeFromTop (14);
    if (tab == 0) layoutTrack (r);
    else if (tab == 1) layoutSong (r);
    else layoutStructure (r);
}

void Inspector::layoutTrack (juce::Rectangle<int> r)
{
    trackName.setBounds (r.removeFromTop (32));
    r.removeFromTop (22);
    swatchesArea = r.removeFromTop (60);
    r.removeFromTop (22);
    role.setBounds (r.removeFromTop (28));
    r.removeFromTop (22);
    if (chooseInstrument.isShowing() || chooseInstrument.getParentComponent() == this)
    {
        chooseInstrument.setBounds (r.removeFromTop (30));
        r.removeFromTop (16);
    }
    if (armed.getParentComponent() == this)
    {
        armed.setBounds (r.removeFromTop (24));
        r.removeFromTop (6);
        input.setBounds (r.removeFromTop (28));
        r.removeFromTop (6);
        monitor.setBounds (r.removeFromTop (28));
        r.removeFromTop (22);
        auto eqRow = r.removeFromTop (24);
        eqOn.setBounds (eqRow.removeFromRight (100));
        auto knobs = r.removeFromTop (56);
        const int w = knobs.getWidth() / 3;
        for (auto* k : { &low, &mid, &high }) k->setBounds (knobs.removeFromLeft (w).withSizeKeepingCentre (46, 46));
        r.removeFromTop (22);
    }
    output.setBounds (r.removeFromTop (28));
    r.removeFromTop (12);
    volume.setBounds (r.removeFromTop (26));
    r.removeFromTop (6);
    pan.setBounds (r.removeFromTop (26));
}

void Inspector::layoutSong (juce::Rectangle<int> r)
{
    songName.setBounds (r.removeFromTop (32));
    r.removeFromTop (22);
    bpm.setBounds (r.removeFromTop (26));
    r.removeFromTop (10);
    auto row = r.removeFromTop (28);
    keyBox.setBounds (row.removeFromLeft (row.getWidth() / 2 - 4));
    row.removeFromLeft (8);
    meterBox.setBounds (row);
    r.removeFromTop (28);
    clickOn.setBounds (r.removeFromTop (24));
    r.removeFromTop (6);
    clickVolume.setBounds (r.removeFromTop (26));
    r.removeFromTop (8);
    clickOut.setBounds (r.removeFromTop (28));
    r.removeFromTop (8);
    countIn.setBounds (r.removeFromTop (28));
}

void Inspector::layoutStructure (juce::Rectangle<int> r)
{
    r.removeFromTop (18);
    const int cols = 3, h = 28, gap = 6;
    const int w = (r.getWidth() - gap * (cols - 1)) / cols;
    for (int i = 0; i < addSectionButtons.size(); ++i)
        addSectionButtons[i]->setBounds (r.getX() + (i % cols) * (w + gap), r.getY() + (i / cols) * (h + gap), w, h);
    r.removeFromTop (((addSectionButtons.size() + cols - 1) / cols) * (h + gap) + 16);
    sectionsArea = r;
}

void Inspector::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);
    g.setColour (colours::stroke);
    g.fillRect (0, 0, 1, getHeight());

    // Pestañas
    const juce::String tabs[] = { "Pista", juce::String::fromUTF8 ("Canción"), "Estructura" };
    auto t = tabsArea.toFloat();
    g.setColour (juce::Colour (0x0fffffff));
    g.fillRoundedRectangle (t, 8);
    const auto w = t.getWidth() / 3;
    for (int i = 0; i < 3; ++i)
    {
        auto r = juce::Rectangle<float> (t.getX() + w * i, t.getY(), w, t.getHeight()).reduced (2);
        if (i == tab) { g.setColour (juce::Colour (0x24ffffff)); g.fillRoundedRectangle (r, 6); }
        g.setColour (i == tab ? colours::text : colours::text2);
        g.setFont (font (12.5f, i == tab));
        g.drawText (tabs[i], r, juce::Justification::centred);
    }

    auto label = [&] (juce::Component& c, const juce::String& text)
    {
        if (c.getParentComponent() != this) return;
        g.setColour (colours::text3);
        g.setFont (font (10, true));
        g.drawText (text, c.getX(), c.getY() - 17, juce::jmax (c.getWidth(), 200), 14, juce::Justification::centredLeft);
    };
    auto& s = ctx.session;
    if (tab == 0)
    {
        auto* tr = s.project().findTrack (s.selectedTrack);
        if (tr == nullptr)
        {
            g.setColour (colours::text2);
            g.setFont (font (13));
            g.drawText ("Selecciona una pista.", getLocalBounds().withTrimmedTop (60), juce::Justification::centredTop);
            return;
        }
        g.setColour (colours::text3);
        g.setFont (font (10, true));
        g.drawText ("COLOR", swatchesArea.getX(), swatchesArea.getY() - 17, 100, 14, juce::Justification::centredLeft);
        const auto names = colours::names();
        const int per = 5;
        const auto size = 26, gap = (swatchesArea.getWidth() - per * size) / (per - 1);
        for (int i = 0; i < names.size(); ++i)
        {
            auto r = juce::Rectangle<float> ((float) (swatchesArea.getX() + (i % per) * (size + gap)), (float) (swatchesArea.getY() + (i / per) * (size + 8)), (float) size, (float) size);
            if (tr->colour == names[i]) { g.setColour (colours::named (names[i]).withAlpha (0.6f)); g.fillEllipse (r.expanded (4)); g.setColour (juce::Colours::white); g.fillEllipse (r.expanded (2)); }
            g.setColour (colours::named (names[i]));
            g.fillEllipse (r);
        }
        label (role, "TIPO");
        label (armed, juce::String::fromUTF8 ("GRABACIÓN"));
        if (eqOn.getParentComponent() == this) { g.setColour (colours::text3); g.setFont (font (10, true)); g.drawText ("EQ DE CANAL", 14, eqOn.getY() + 4, 150, 14, juce::Justification::centredLeft); }
        label (output, "SALIDA");
        if (tr->isInstrument()) label (chooseInstrument, "INSTRUMENTO: " + (tr->instrument ? tr->instrument->name : juce::String()));
        if (low.getParentComponent() == this)
        {
            g.setColour (colours::text2);
            g.setFont (font (10, true));
            for (auto* k : { &low, &mid, &high })
                g.drawText (k == &low ? "Graves" : k == &mid ? "Medios" : "Agudos", k->getBounds().translated (0, 46).withHeight (12).expanded (20, 0), juce::Justification::centred);
        }
        label (volume, "VOLUMEN Y PANEO");
    }
    else if (tab == 1)
    {
        label (songName, juce::String::fromUTF8 ("CANCIÓN"));
        label (bpm, "TEMPO");
        label (keyBox, juce::String::fromUTF8 ("TONALIDAD Y COMPÁS"));
        label (clickOn, juce::String::fromUTF8 ("METRÓNOMO"));
        label (countIn, juce::String::fromUTF8 ("GRABACIÓN"));
    }
    else
    {
        g.setColour (colours::text3);
        g.setFont (font (10, true));
        if (! addSectionButtons.isEmpty())
            g.drawText (juce::String::fromUTF8 ("AGREGAR SECCIÓN"), addSectionButtons[0]->getX(), addSectionButtons[0]->getY() - 18, 200, 14, juce::Justification::centredLeft);
        sectionRows.clear(); sectionLoopButtons.clear(); sectionDeleteButtons.clear();
        auto r = sectionsArea;
        const auto tl = s.project().timeline();
        for (auto& sec : s.project().sections)
        {
            if (r.getHeight() < 36) break;
            auto row = r.removeFromTop (36);
            r.removeFromTop (4);
            sectionRows.push_back ({ row, sec.id });
            g.setColour (juce::Colour (0x0affffff));
            g.fillRoundedRectangle (row.toFloat(), 8);
            const auto c = colours::named (defaultColourId (sec.kind));
            g.setColour (c);
            g.fillRoundedRectangle (row.reduced (8, 6).removeFromLeft (4).toFloat(), 2);
            auto del = row.removeFromRight (28), lp = row.removeFromRight (28);
            sectionDeleteButtons.push_back ({ del, sec.id });
            sectionLoopButtons.push_back ({ lp, sec.id });
            g.setColour (colours::loop);
            g.strokePath (IconButton::path (IconButton::Icon::loop, lp.toFloat().withSizeKeepingCentre (14, 12)), juce::PathStrokeType (1.4f));
            g.setColour (colours::text3);
            g.strokePath (IconButton::path (IconButton::Icon::plus, del.toFloat().withSizeKeepingCentre (10, 10)), juce::PathStrokeType (1.4f), juce::AffineTransform::rotation (0.785f, (float) del.getCentreX(), (float) del.getCentreY()));
            auto text = row.withTrimmedLeft (20);
            g.setColour (colours::text);
            g.setFont (font (12.5f, true));
            g.drawText (sec.name, text.removeFromTop (20).withTrimmedTop (4), juce::Justification::centredLeft);
            g.setColour (colours::text3);
            g.setFont (mono (9.5f));
            g.drawText (tl.barBeatString (sec.start) + " · "_u + juce::String ((int) std::round (sec.length / tl.beatsPerBar)) + " compases"_u, text, juce::Justification::centredLeft);
        }
    }
}

void Inspector::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    if (tabsArea.contains (p))
    {
        tab = juce::jlimit (0, 2, (p.x - tabsArea.getX()) * 3 / juce::jmax (1, tabsArea.getWidth()));
        update();
        return;
    }
    auto& s = ctx.session;
    if (tab == 0 && swatchesArea.contains (p))
    {
        const auto names = colours::names();
        const int per = 5, size = 26, gap = (swatchesArea.getWidth() - per * size) / (per - 1);
        const int col = (p.x - swatchesArea.getX()) / (size + gap), row = (p.y - swatchesArea.getY()) / (size + 8);
        const int i = row * per + col;
        if (i >= 0 && i < names.size()) { const auto n = names[i]; s.updateTrack (s.selectedTrack, [n] (Track& t) { t.colour = n; }); }
        return;
    }
    if (tab == 2)
    {
        for (auto& [r, id] : sectionLoopButtons) if (r.contains (p)) { s.loopSection (id); return; }
        for (auto& [r, id] : sectionDeleteButtons)
            if (r.contains (p))
            {
                const auto sid = id;
                s.edit ([sid] (Project& pr) { pr.sections.erase (std::remove_if (pr.sections.begin(), pr.sections.end(), [&] (auto& x) { return x.id == sid; }), pr.sections.end()); });
                return;
            }
        for (auto& [r, id] : sectionRows)
            if (r.contains (p))
                for (auto& sec : s.project().sections) if (sec.id == id) { s.seek (sec.start); return; }
    }
}

} // namespace wdaw::ui
