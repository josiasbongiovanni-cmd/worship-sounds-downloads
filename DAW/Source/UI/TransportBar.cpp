#include "TransportBar.h"

namespace wdaw::ui
{

TransportBar::TransportBar (Context& c) : ctx (c)
{
    for (auto* b : { &home, &toStart, &prev, &play, &record, &next, &loop, &click, &undo, &importBtn, &exportBtn, &prefs, &mixer, &inspector })
        addAndMakeVisible (*b);
    play.primary = true;
    loop.activeColour = colours::loop;
    click.activeColour = colours::solo;
    auto& s = ctx.session;
    home.onClick = [this] { if (onHome) onHome(); };
    toStart.onClick = [&s] { s.goToStart(); };
    prev.onClick = [&s] { s.goToPreviousStop(); };
    play.onClick = [&s] { s.togglePlay(); };
    record.onClick = [&s] { s.toggleRecord(); };
    next.onClick = [&s] { s.goToNextStop(); };
    loop.onClick = [&s] { s.toggleRepeat(); };
    click.onClick = [&s] { s.edit ([] (Project& p) { p.clickEnabled = ! p.clickEnabled; }); };
    undo.onClick = [&s] { s.undo(); };
    importBtn.onClick = [this] { if (onImport) onImport(); };
    exportBtn.onClick = [this] { if (onExport) onExport(); };
    prefs.onClick = [this] { if (onPreferences) onPreferences(); };
    mixer.onClick = [this] { if (onToggleMixer) onToggleMixer(); };
    inspector.onClick = [this] { if (onToggleInspector) onToggleInspector(); };
    ctx.session.addChangeListener (this);
    update();
    startTimerHz (20);
}

TransportBar::~TransportBar() { ctx.session.removeChangeListener (this); }

void TransportBar::update()
{
    auto& s = ctx.session;
    play.icon = s.isPlaying() ? IconButton::Icon::stop : IconButton::Icon::play;
    play.active = s.isPlaying();
    record.active = s.isRecording();
    loop.active = s.project().loopEnabled;
    click.active = s.project().clickEnabled;
    undo.setEnabled (s.canUndo());
    mixer.active = mixerVisible;
    inspector.active = inspectorVisible;
    repaint();
}

void TransportBar::resized()
{
    auto r = getLocalBounds().reduced (14, 0);
    home.setBounds (r.removeFromLeft (32).withSizeKeepingCentre (32, 32));
    r.removeFromLeft (12);
    titleArea = r.removeFromLeft (200);
    chipsArea = r.removeFromLeft (190);

    auto right = r.removeFromRight (6 * 36);
    for (auto* b : { &undo, &importBtn, &exportBtn, &prefs, &mixer, &inspector })
        b->setBounds (right.removeFromLeft (36).withSizeKeepingCentre (32, 32));

    // Centro: transporte + posición
    const int transportWidth = 6 * 42 + 54;
    const int posWidth = 230;
    auto centre = r.withSizeKeepingCentre (juce::jmin (r.getWidth(), transportWidth + posWidth + 16), r.getHeight());
    auto t = centre.removeFromLeft (transportWidth);
    for (auto* b : { &toStart, &prev })
        b->setBounds (t.removeFromLeft (42).withSizeKeepingCentre (36, 36));
    play.setBounds (t.removeFromLeft (54).withSizeKeepingCentre (48, 48));
    for (auto* b : { &record, &next, &loop, &click })
        b->setBounds (t.removeFromLeft (42).withSizeKeepingCentre (36, 36));
    centre.removeFromLeft (16);
    positionArea = centre.withSizeKeepingCentre (centre.getWidth(), 50);
}

void TransportBar::paint (juce::Graphics& g)
{
    auto& s = ctx.session;
    auto& p = s.project();
    g.fillAll (colours::panel);
    g.setColour (colours::stroke);
    g.fillRect (0, getHeight() - 1, getWidth(), 1);

    // Nombre del proyecto y estado
    auto title = titleArea.withSizeKeepingCentre (titleArea.getWidth(), 36);
    g.setColour (colours::text);
    g.setFont (font (16, true));
    g.drawText (p.name, title.removeFromTop (20), juce::Justification::centredLeft, true);
    g.setColour (s.midiLearn.isLearning() ? colours::accent2 : colours::text2);
    g.setFont (font (11.5f));
    juce::String status = s.statusText;
    if (s.midiLearn.isLearning()) status = "Mueve un control MIDI: " + s.midiLearn.learningLabel;
    if (status.isEmpty())
        status = juce::String ((int) p.tracks.size()) + " pistas · "_u + juce::String ((int) p.sections.size()) + juce::String::fromUTF8 (" secciones · CPU ")
               + juce::String ((int) std::round (s.engine.cpuLoad() * 100)) + "%";
    g.drawText (status, title, juce::Justification::centredLeft, true);

    // Chips: BPM / tono / compás (clic para editar)
    auto chips = chipsArea.withSizeKeepingCentre (chipsArea.getWidth(), 28);
    auto chip = [&] (const juce::String& value, const juce::String& unit)
    {
        const auto f = font (13.5f, true);
        const auto w = juce::GlyphArrangement::getStringWidthInt (f, value) + (unit.isNotEmpty() ? juce::GlyphArrangement::getStringWidthInt (font (10), unit) + 4 : 0) + 20;
        auto r = chips.removeFromLeft (w).toFloat();
        chips.removeFromLeft (6);
        g.setColour (juce::Colour (0x0fffffff));
        g.fillRoundedRectangle (r, 14);
        g.setColour (colours::stroke);
        g.drawRoundedRectangle (r.reduced (0.5f), 14, 1);
        auto inner = r.reduced (10, 0);
        g.setColour (colours::text);
        g.setFont (f);
        g.drawText (value, inner.removeFromLeft ((float) juce::GlyphArrangement::getStringWidthInt (f, value)), juce::Justification::centredLeft);
        if (unit.isNotEmpty())
        {
            g.setColour (colours::text3);
            g.setFont (font (10));
            g.drawText (unit, inner.withTrimmedLeft (4), juce::Justification::centredLeft);
        }
    };
    chip (juce::String (p.bpm, p.bpm == std::floor (p.bpm) ? 0 : 1), "BPM");
    chip (p.key, "Tono");
    chip (juce::String (p.beatsPerBar) + "/4", {});

    // Posición grande: compás.tiempo, tiempo y sección actual
    auto pos = positionArea.toFloat();
    g.setColour (juce::Colour (0x59000000));
    g.fillRoundedRectangle (pos, 12);
    g.setColour (colours::stroke);
    g.drawRoundedRectangle (pos.reduced (0.5f), 12, 1);
    const auto tl = p.timeline();
    const auto b = s.playhead();
    auto inner = pos.reduced (14, 6);
    auto numbers = inner.removeFromLeft (100);
    const auto bb = tl.barBeatString (juce::jmax (0.0, b));
    g.setColour (colours::text);
    g.setFont (font (25, true));
    g.drawText (bb.upToLastOccurrenceOf (".", false, false), numbers.removeFromTop (26), juce::Justification::centredLeft);
    g.setColour (colours::text2);
    g.setFont (mono (11));
    g.drawText (Timeline::timecode (tl.toSeconds (juce::jmax (0.0, b))), numbers, juce::Justification::centredLeft);

    juce::String label;
    juce::Colour colour = colours::record;
    if (s.isRecording() && b < s.editCursor - 0.01) label = "Cuenta…"_u;
    else if (auto* sec = edit::sectionAt (p, b)) { label = sec->name; colour = colours::named (defaultColourId (sec->kind)); }
    if (label.isNotEmpty())
    {
        const auto f = font (13, true);
        auto r = inner.withSizeKeepingCentre (inner.getWidth(), 26).withWidth ((float) juce::GlyphArrangement::getStringWidthInt (f, label) + 20);
        g.setColour (colour.withAlpha (0.22f));
        g.fillRoundedRectangle (r, 13);
        g.setColour (colour.withAlpha (0.6f));
        g.drawRoundedRectangle (r.reduced (0.5f), 13, 1);
        g.setColour (colour);
        g.setFont (f);
        g.drawText (label, r, juce::Justification::centred);
    }
}

void TransportBar::mouseDown (const juce::MouseEvent& e)
{
    if (chipsArea.contains (e.getPosition())) showSongMenu();
}

void TransportBar::showSongMenu()
{
    auto& s = ctx.session;
    juce::PopupMenu m, keys, meters;
    m.addItem ("Cambiar tempo (BPM)…"_u, [this]
    {
        ctx.askText ("Tempo (BPM)", juce::String (ctx.session.project().bpm), [this] (juce::String v)
        {
            const auto bpm = v.getDoubleValue();
            if (bpm >= 20 && bpm <= 300) ctx.session.edit ([bpm] (Project& p) { edit::setTempo (p, bpm); });
        });
    });
    for (auto k : { "C", "C#", "Db", "D", "Eb", "E", "F", "F#", "Gb", "G", "Ab", "A", "Bb", "B" })
        for (auto suffix : { "", "m" })
        {
            juce::String key = juce::String (k) + suffix;
            keys.addItem (key, true, s.project().key == key, [&s, key] { s.edit ([key] (Project& p) { p.key = key; }); });
        }
    for (int beats : { 2, 3, 4, 6 })
        meters.addItem (juce::String (beats) + "/4", true, s.project().beatsPerBar == beats, [&s, beats] { s.edit ([beats] (Project& p) { p.beatsPerBar = beats; }); });
    m.addSubMenu ("Tonalidad", keys);
    m.addSubMenu (juce::String::fromUTF8 ("Compás"), meters);
    m.addItem ("Renombrar canción…"_u, [this]
    {
        ctx.askText (juce::String::fromUTF8 ("Nombre de la canción"), ctx.session.project().name, [this] (juce::String n)
        {
            if (n.isNotEmpty()) ctx.session.edit ([n] (Project& p) { p.name = n; });
        });
    });
    juce::PopupMenu clickMenu, countIn, clickOut;
    for (auto db : { 0.0, -6.0, -12.0, -18.0 })
        clickMenu.addItem ("Volumen " + juce::String (db, 0) + " dB", true, std::abs (s.project().clickVolumeDb - db) < 0.1,
                           [&s, db] { s.edit ([db] (Project& p) { p.clickVolumeDb = db; }); });
    for (int bars : { 0, 1, 2 })
        countIn.addItem (bars == 0 ? juce::String ("Sin cuenta") : juce::String (bars) + (bars == 1 ? juce::String::fromUTF8 (" compás") : " compases"),
                         true, s.countInBars == bars, [&s, bars] { s.countInBars = bars; });
    clickOut.addItem ("Master", true, s.project().clickOutPair < 2, [&s] { s.edit ([] (Project& p) { p.clickOutPair = 0; }); });
    for (int pr = 2; pr <= juce::jmax (2, s.engine.numOutputChannels() / 2); ++pr)
        clickOut.addItem ("Salidas " + juce::String (pr * 2 - 1) + "/" + juce::String (pr * 2) + " (in-ears)", pr <= s.engine.numOutputChannels() / 2,
                          s.project().clickOutPair == pr, [&s, pr] { s.edit ([pr] (Project& p) { p.clickOutPair = pr; }); });
    clickMenu.addSubMenu ("Enviar click a", clickOut);
    m.addSubMenu (juce::String::fromUTF8 ("Metrónomo"), clickMenu);
    m.addSubMenu ("Cuenta previa al grabar", countIn);
    m.showMenuAsync (juce::PopupMenu::Options());
}

} // namespace wdaw::ui
