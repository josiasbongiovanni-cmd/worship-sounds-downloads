#include "PianoRoll.h"

namespace wdaw::ui
{

static juce::String gridName (Beats g)
{
    if (std::abs (g - 4) < 1e-6) return "1";
    if (std::abs (g - 2) < 1e-6) return "1/2";
    if (std::abs (g - 1) < 1e-6) return "1/4";
    if (std::abs (g - 0.5) < 1e-6) return "1/8";
    if (std::abs (g - 0.25) < 1e-6) return "1/16";
    if (std::abs (g - 0.125) < 1e-6) return "1/32";
    if (std::abs (g - 1.0 / 3) < 1e-6) return "1/8 T";
    if (std::abs (g - 1.0 / 6) < 1e-6) return "1/16 T";
    return juce::String (g, 3);
}

//==============================================================================
/** Grilla de notas. Las coordenadas son relativas al inicio del ítem. */
class PianoRoll::Grid : public juce::Component
{
public:
    explicit Grid (PianoRoll& o) : roll (o) {}
    PianoRoll& roll;

    float xFor (Beats b) const { return (float) (b * roll.ppb); }
    Beats beatAt (float x) const { return juce::jmax (0.0, (double) x / roll.ppb); }
    int pitchAt (float y) const { return juce::jlimit (0, 127, 127 - (int) std::floor (y / (float) rowH)); }
    juce::Rectangle<float> noteRect (const MidiNote& n) const
    {
        return { xFor (n.start), (float) ((127 - n.pitch) * rowH + 1), juce::jmax (4.0f, xFor (n.length)), (float) rowH - 2 };
    }

    /** Ajuste a la grilla del proyecto (el ítem puede empezar fuera de la grilla). */
    Beats snapRel (Beats rel, bool down = false) const
    {
        auto* c = roll.clip();
        if (c == nullptr) return rel;
        const auto abs = c->start + rel;
        const auto g = roll.grid;
        const auto snapped = (down ? std::floor (abs / g + 1e-9) : std::round (abs / g)) * g;
        return juce::jmax (0.0, snapped - c->start);
    }

    void paint (juce::Graphics& g) override
    {
        auto* c = roll.clip();
        const auto area = g.getClipBounds();
        for (int row = juce::jmax (0, area.getY() / rowH); row <= juce::jmin (127, area.getBottom() / rowH); ++row)
        {
            const int pitch = 127 - row;
            g.setColour (edit::midi::isBlackKey (pitch) ? colours::lane : colours::laneAlt);
            g.fillRect (area.getX(), row * rowH, area.getWidth(), rowH);
            if (pitch % 12 == 0) { g.setColour (juce::Colour (0x1fffffff)); g.fillRect (area.getX(), row * rowH + rowH - 1, area.getWidth(), 1); }
        }
        if (c == nullptr) return;
        const auto tl = roll.ctx.session.timeline();
        // Fin del ítem
        g.setColour (juce::Colour (0x59000000));
        g.fillRect (juce::Rectangle<float> (xFor (c->length), (float) area.getY(), (float) getWidth(), (float) area.getHeight()));
        // Grilla alineada con el proyecto
        const auto gb = roll.grid;
        const auto firstAbs = std::floor ((c->start + beatAt ((float) area.getX())) / gb) * gb;
        for (auto abs = firstAbs; abs - c->start <= beatAt ((float) area.getRight()); abs += gb)
        {
            const auto x = xFor (abs - c->start);
            const bool bar = std::abs (std::fmod (abs, (double) tl.beatsPerBar)) < 1e-6;
            const bool beat = std::abs (abs - std::round (abs)) < 1e-6;
            g.setColour (juce::Colours::white.withAlpha (bar ? 0.16f : beat ? 0.08f : 0.035f));
            g.fillRect (x, (float) area.getY(), 1.0f, (float) area.getHeight());
        }
        // Notas
        const auto colour = roll.track() != nullptr ? colours::named (roll.track()->colour) : colours::accent;
        for (auto n : c->notes)
        {
            applyPreview (n);
            auto r = noteRect (n);
            if (! r.intersects (area.toFloat())) continue;
            g.setColour (colour.withAlpha (0.45f + 0.55f * (float) n.velocity / 127.0f));
            g.fillRoundedRectangle (r, 3);
            if (isSelected (n.id)) { g.setColour (juce::Colours::white); g.drawRoundedRectangle (r, 3, 1.5f); }
            if (r.getWidth() > 34)
            {
                g.setColour (juce::Colour (0xb3000000));
                g.setFont (font (9, true));
                g.drawText (edit::midi::noteName (n.pitch), r.withTrimmedLeft (3), juce::Justification::centredLeft);
            }
        }
        if (drawing)
        {
            g.setColour (colour.withAlpha (0.8f));
            g.fillRoundedRectangle (noteRect (*drawing), 3);
        }
        // Pedal de sustain (CC64): franja abajo de la nota más grave visible
        for (size_t i = 0; i < c->controls.size(); ++i)
        {
            auto& x = c->controls[i];
            if (x.controller != 64 || x.value < 64) continue;
            Beats end = c->length;
            for (size_t j = i + 1; j < c->controls.size(); ++j)
                if (c->controls[j].controller == 64 && c->controls[j].value < 64) { end = c->controls[j].time; break; }
            g.setColour (colours::solo.withAlpha (0.25f));
            g.fillRect (juce::Rectangle<float> (xFor (x.time), (float) area.getBottom() - 6, xFor (end - x.time), 4));
        }
        if (marquee)
        {
            g.setColour (colours::accent2.withAlpha (0.12f)); g.fillRect (*marquee);
            g.setColour (colours::accent2); g.drawRect (*marquee, 1);
        }
        // Cursor
        const auto ph = xFor (roll.ctx.session.playhead() - c->start);
        g.setColour (juce::Colours::white);
        g.fillRect (ph, (float) area.getY(), 1.5f, (float) area.getHeight());
    }

    bool isSelected (const juce::Uuid& id) const
    {
        return std::find (roll.selection.begin(), roll.selection.end(), id) != roll.selection.end();
    }

    void applyPreview (MidiNote& n) const
    {
        if (! drag || ! isSelected (n.id)) return;
        if (drag->resize) n.length = juce::jmax (minLength(), n.length + drag->dt);
        else { n.start = juce::jmax (0.0, n.start + drag->dt); n.pitch = juce::jlimit (0, 127, n.pitch + drag->semis); }
    }

    Beats minLength() const { return juce::jmax (0.01, roll.grid / 4); }

    std::optional<std::pair<MidiNote, bool>> hit (juce::Point<float> p) const
    {
        auto* c = roll.clip();
        if (c == nullptr) return {};
        for (auto it = c->notes.rbegin(); it != c->notes.rend(); ++it)
        {
            auto r = noteRect (*it);
            if (r.expanded (0, 1).contains (p)) return std::make_pair (*it, p.x > r.getRight() - juce::jmin (8.0f, r.getWidth() / 3));
        }
        return {};
    }

    struct Drag { bool resize = false; Beats dt = 0; int semis = 0; MidiNote anchor; bool started = false; };
    std::optional<Drag> drag;
    std::optional<MidiNote> drawing;
    std::optional<juce::Rectangle<float>> marquee;

    void mouseMove (const juce::MouseEvent& e) override
    {
        auto h = hit (e.position);
        setMouseCursor (h && h->second ? juce::MouseCursor::LeftRightResizeCursor
                        : roll.tool == Tool::draw ? juce::MouseCursor::CrosshairCursor : juce::MouseCursor::NormalCursor);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        roll.grabKeyboardFocus();
        auto h = hit (e.position);
        if (h)
        {
            auto [note, edge] = *h;
            if (e.getNumberOfClicks() >= 2)   // doble clic en una nota la borra (REAPER)
            {
                roll.selection = { note.id };
                roll.removeSelected();
                return;
            }
            if (! isSelected (note.id))
            {
                if (! e.mods.isShiftDown()) roll.selection.clear();
                roll.selection.push_back (note.id);
            }
            else if (e.mods.isShiftDown())
            {
                roll.selection.erase (std::find (roll.selection.begin(), roll.selection.end(), note.id));
                repaint();
                return;
            }
            drag = Drag { edge, 0, 0, note };
            roll.ctx.session.beginGesture();
            if (! edge) roll.preview (note.pitch, note.velocity);
            repaint();
            return;
        }
        if (roll.tool == Tool::draw || e.mods.isCommandDown())
        {
            MidiNote n;
            n.pitch = pitchAt (e.position.y);
            n.start = snapRel (beatAt (e.position.x), true);
            n.length = roll.grid;
            drawing = n;
            roll.preview (n.pitch);
            repaint();
            return;
        }
        if (! e.mods.isShiftDown()) roll.selection.clear();
        marquee = juce::Rectangle<float> (e.position, e.position);
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (drag)
        {
            if (! drag->started && e.getDistanceFromDragStart() < 3) return;
            drag->started = true;
            const auto dx = (double) e.getDistanceFromDragStartX() / roll.ppb;
            if (drag->resize)
                drag->dt = juce::jmax (drag->anchor.start + minLength(), snapRel (drag->anchor.end() + dx)) - drag->anchor.end();
            else
            {
                drag->dt = snapRel (drag->anchor.start + dx) - drag->anchor.start;
                const auto semis = -(int) std::round ((float) e.getDistanceFromDragStartY() / (float) rowH);
                if (semis != drag->semis) roll.preview (drag->anchor.pitch + semis);
                drag->semis = semis;
            }
        }
        else if (drawing)
            drawing->length = juce::jmax (roll.grid, snapRel (beatAt (e.position.x)) - drawing->start);
        else if (marquee)
            marquee = juce::Rectangle<float> (e.mouseDownPosition, e.position);
        repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        auto& s = roll.ctx.session;
        const auto clipId = s.editingMidiClip;
        if (drag)
        {
            auto d = *drag;
            drag.reset();
            if (d.started && (d.dt != 0 || d.semis != 0))
            {
                auto ids = roll.selection;
                const auto minLen = minLength();
                s.edit ([&] (Project& p)
                {
                    if (auto* c = edit::findMidiClip (p, clipId))
                    {
                        if (d.resize) edit::midi::resize (*c, ids, d.dt, minLen);
                        else edit::midi::move (*c, ids, d.dt, d.semis);
                    }
                }, false);
            }
        }
        else if (drawing)
        {
            auto n = *drawing;
            drawing.reset();
            juce::Uuid created;
            s.edit ([&] (Project& p) { if (auto* c = edit::findMidiClip (p, clipId)) created = edit::midi::addNote (*c, n.pitch, n.start, n.length); });
            roll.selection = { created };
        }
        else if (marquee)
        {
            if (auto* c = roll.clip())
                for (auto& n : c->notes)
                    if (noteRect (n).intersects (*marquee) && ! isSelected (n.id)) roll.selection.push_back (n.id);
            marquee.reset();
        }
        juce::ignoreUnused (e);
        repaint();
    }
};

//==============================================================================
class PianoRoll::Keys : public juce::Component
{
public:
    explicit Keys (PianoRoll& o) : roll (o) {}
    PianoRoll& roll;
    int scrollY = 0;
    void paint (juce::Graphics& g) override
    {
        for (int row = 0; row < 128; ++row)
        {
            const int pitch = 127 - row;
            const auto y = row * rowH - scrollY;
            if (y + rowH < 0 || y > getHeight()) continue;
            const bool black = edit::midi::isBlackKey (pitch);
            g.setColour (black ? juce::Colour (0xff292929) : juce::Colour (0xffe0e0e0));
            g.fillRect (0, y, getWidth(), rowH);
            g.setColour (juce::Colour (0x40000000));
            g.fillRect (0, y + rowH - 1, getWidth(), 1);
            if (pitch % 12 == 0)
            {
                g.setColour (juce::Colour (0x99000000));
                g.setFont (font (9, true));
                g.drawText (edit::midi::noteName (pitch), 0, y, getWidth() - 4, rowH, juce::Justification::centredRight);
            }
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        roll.preview (juce::jlimit (0, 127, 127 - (e.y + scrollY) / rowH));
    }
};

//==============================================================================
class PianoRoll::Velocity : public juce::Component
{
public:
    explicit Velocity (PianoRoll& o) : roll (o) {}
    PianoRoll& roll;
    int scrollX = 0;
    void paint (juce::Graphics& g) override
    {
        g.fillAll (colours::panel);
        g.setColour (colours::stroke);
        g.fillRect (0, 0, getWidth(), 1);
        g.setColour (colours::text3);
        g.setFont (font (9, true));
        g.drawText ("VELOCIDAD", 0, 4, keyW, 14, juce::Justification::centred);
        auto* c = roll.clip();
        if (c == nullptr) return;
        const auto colour = roll.track() != nullptr ? colours::named (roll.track()->colour) : colours::accent;
        for (auto& n : c->notes)
        {
            const auto x = keyW + (float) (n.start * roll.ppb) - (float) scrollX;
            if (x < keyW - 4 || x > getWidth()) continue;
            const auto h = (float) (getHeight() - 10) * (float) n.velocity / 127.0f;
            const bool sel = std::find (roll.selection.begin(), roll.selection.end(), n.id) != roll.selection.end();
            g.setColour (sel ? juce::Colours::white : colour.withAlpha (0.8f));
            g.fillRect (x, (float) getHeight() - h, 3.0f, h);
            g.fillEllipse (x - 2, (float) getHeight() - h - 3, 7, 7);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override { roll.ctx.session.beginGesture(); setVelocity (e.position); }
    void mouseDrag (const juce::MouseEvent& e) override { setVelocity (e.position); }
    void setVelocity (juce::Point<float> p)
    {
        auto* c = roll.clip();
        if (c == nullptr) return;
        const auto beat = (p.x - keyW + (float) scrollX) / roll.ppb;
        const auto tol = 6.0 / roll.ppb;
        std::vector<juce::Uuid> ids;
        for (auto& n : c->notes) if (std::abs (n.start - beat) < tol) ids.push_back (n.id);
        bool anySelected = false;
        for (auto& id : ids) if (std::find (roll.selection.begin(), roll.selection.end(), id) != roll.selection.end()) anySelected = true;
        if (anySelected) ids = roll.selection;
        if (ids.empty()) return;
        const int v = juce::jlimit (1, 127, (int) std::round ((1.0f - (p.y - 4) / (float) (getHeight() - 8)) * 127));
        const auto clipId = roll.ctx.session.editingMidiClip;
        roll.ctx.session.edit ([&] (Project& pr) { if (auto* cl = edit::findMidiClip (pr, clipId)) edit::midi::setVelocity (*cl, ids, v); }, false);
    }
};

//==============================================================================
PianoRoll::PianoRoll (Context& c) : ctx (c)
{
    grid_ = std::make_unique<Grid> (*this);
    keys = std::make_unique<Keys> (*this);
    velocity = std::make_unique<Velocity> (*this);
    view.setViewedComponent (grid_.get(), false);
    view.setScrollBarThickness (10);
    addAndMakeVisible (view);
    addAndMakeVisible (*keys);
    addAndMakeVisible (*velocity);
    for (auto* b : { &selectTool, &drawTool, &gridButton, &quantizeButton, &editButton, &playButton })
        addAndMakeVisible (*b);
    selectTool.onClick = [this] { tool = Tool::select; updateButtons(); };
    drawTool.onClick = [this] { tool = Tool::draw; updateButtons(); };
    gridButton.onClick = [this]
    {
        juce::PopupMenu m;
        for (auto gr : { 4.0, 2.0, 1.0, 0.5, 0.25, 0.125, 1.0 / 3, 1.0 / 6 })
            m.addItem ("Grilla " + gridName (gr), true, std::abs (grid - gr) < 1e-6, [this, gr] { grid = gr; updateButtons(); grid_->repaint(); });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&gridButton));
    };
    quantizeButton.onClick = [this]
    {
        juce::PopupMenu m;
        m.addItem ("Cuantizar ahora (Q)", [this] { quantize(); });
        m.addSeparator();
        for (auto st : { 1.0, 0.75, 0.5, 0.25 })
            m.addItem ("Intensidad " + juce::String ((int) (st * 100)) + " %", true, std::abs (strength - st) < 1e-6, [this, st] { strength = st; });
        m.addItem (juce::String::fromUTF8 ("Cuantizar también la duración"), true, quantizeLengths, [this] { quantizeLengths = ! quantizeLengths; });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&quantizeButton));
    };
    editButton.onClick = [this]
    {
        juce::PopupMenu m;
        m.addItem ("Subir un semitono (Flecha arriba)", [this] { transpose (1); });
        m.addItem ("Bajar un semitono (Flecha abajo)", [this] { transpose (-1); });
        m.addItem ("Subir una octava (Shift+Flecha arriba)", [this] { transpose (12); });
        m.addItem ("Bajar una octava (Shift+Flecha abajo)", [this] { transpose (-12); });
        m.addSeparator();
        m.addItem ("Legato", [this]
        {
            auto ids = selection;
            const auto id = ctx.session.editingMidiClip;
            ctx.session.edit ([&] (Project& p) { if (auto* c = edit::findMidiClip (p, id)) edit::midi::legato (*c, ids); });
        });
        m.addItem ("Humanizar (±10 ms, ±8 de velocidad)"_u, [this]
        {
            auto ids = selection;
            const auto id = ctx.session.editingMidiClip;
            const auto jitter = ctx.session.timeline().toBeats (0.010);
            ctx.session.edit ([&] (Project& p)
            {
                auto* c = edit::findMidiClip (p, id);
                if (c == nullptr) return;
                juce::Random rng;
                for (auto& n : c->notes)
                    if (ids.empty() || std::find (ids.begin(), ids.end(), n.id) != ids.end())
                    {
                        n.start = juce::jmax (0.0, n.start + (rng.nextDouble() * 2 - 1) * jitter);
                        n.velocity = juce::jlimit (1, 127, n.velocity + rng.nextInt ({ -8, 9 }));
                    }
                c->sortNotes();
            });
        });
        m.addItem ("Duplicar notas (Ctrl/Cmd+D)", [this] { duplicate(); });
        m.addSeparator();
        m.addItem ("Seleccionar todas (Ctrl/Cmd+A)", [this] { selectAll(); });
        m.addItem ("Quitar pedal de sustain", [this]
        {
            const auto id = ctx.session.editingMidiClip;
            ctx.session.edit ([&] (Project& p) { if (auto* c = edit::findMidiClip (p, id))
                c->controls.erase (std::remove_if (c->controls.begin(), c->controls.end(), [] (auto& x) { return x.controller == 64; }), c->controls.end()); });
        });
        m.addItem ("Eliminar seleccionadas (Supr)", [this] { removeSelected(); });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&editButton));
    };
    playButton.onClick = [this]
    {
        auto& s = ctx.session;
        if (! s.isPlaying()) if (auto* c = clip()) s.seek (c->start);
        s.togglePlay();
    };
    ctx.session.addChangeListener (this);
    setWantsKeyboardFocus (true);
    updateButtons();
    // Centrar en las notas del ítem
    if (auto* c = clip())
    {
        const auto [lo, hi] = c->pitchRange();
        juce::Component::SafePointer<PianoRoll> safe (this);
        juce::MessageManager::callAsync ([safe, lo = lo, hi = hi]
        {
            if (safe == nullptr) return;
            safe->view.setViewPosition (0, juce::jmax (0, (127 - (lo + hi) / 2) * rowH - safe->view.getViewHeight() / 2));
        });
    }
    startTimerHz (30);
}

PianoRoll::~PianoRoll() { ctx.session.removeChangeListener (this); }

MidiClip* PianoRoll::clip()
{
    // La edición se hace siempre a través de Session::edit; este puntero es solo para leer y dibujar.
    return const_cast<MidiClip*> ([this]() -> const MidiClip*
    {
        auto& p = ctx.session.project();
        for (auto& t : p.tracks) for (auto& c : t.midiClips) if (c.id == ctx.session.editingMidiClip) return &c;
        return nullptr;
    }());
}

const Track* PianoRoll::track() const
{
    for (auto& t : ctx.session.project().tracks) for (auto& c : t.midiClips) if (c.id == ctx.session.editingMidiClip) return &t;
    return nullptr;
}

void PianoRoll::preview (int pitch, int vel)
{
    if (auto* t = track()) ctx.session.engine.previewNote (t->id, pitch, vel);
}

void PianoRoll::updateButtons()
{
    selectTool.setToggleState (tool == Tool::select, juce::dontSendNotification);
    drawTool.setToggleState (tool == Tool::draw, juce::dontSendNotification);
    gridButton.setButtonText ("Grilla " + gridName (grid));
    playButton.setButtonText (ctx.session.isPlaying() ? "Detener" : "Reproducir");
}

void PianoRoll::resized()
{
    auto r = getLocalBounds();
    auto bar = r.removeFromTop (toolbarH).reduced (10, 8);
    selectTool.setBounds (bar.removeFromLeft (120)); bar.removeFromLeft (4);
    drawTool.setBounds (bar.removeFromLeft (100)); bar.removeFromLeft (12);
    gridButton.setBounds (bar.removeFromLeft (110)); bar.removeFromLeft (6);
    quantizeButton.setBounds (bar.removeFromLeft (120)); bar.removeFromLeft (6);
    editButton.setBounds (bar.removeFromLeft (80));
    playButton.setBounds (bar.removeFromRight (100));
    r.removeFromTop (rulerH);
    velocity->setBounds (r.removeFromBottom (velocityH));
    keys->setBounds (r.removeFromLeft (keyW));
    view.setBounds (r);
    auto* c = clip();
    const auto lengthBeats = c != nullptr ? c->length + ctx.session.project().beatsPerBar * 2 : 16.0;
    grid_->setSize (juce::jmax (view.getWidth(), (int) (lengthBeats * ppb)), 128 * rowH);
}

void PianoRoll::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
    g.setColour (colours::panel);
    g.fillRect (getLocalBounds().removeFromTop (toolbarH + rulerH));
    // Regla con compases
    auto* c = clip();
    if (c == nullptr) return;
    const auto tl = ctx.session.timeline();
    auto ruler = getLocalBounds().withTrimmedTop (toolbarH).removeFromTop (rulerH).withTrimmedLeft (keyW);
    const auto scroll = view.getViewPositionX();
    g.setFont (font (10, true));
    for (int bar = (int) std::floor (c->start / tl.beatsPerBar); bar * tl.beatsPerBar < c->end() + tl.beatsPerBar * 2; ++bar)
    {
        const auto x = ruler.getX() + (int) ((bar * tl.beatsPerBar - c->start) * ppb) - scroll;
        if (x < ruler.getX() - 30 || x > getWidth()) continue;
        g.setColour (juce::Colour (0x59ffffff));
        g.fillRect (x, ruler.getY() + 8, 1, ruler.getHeight() - 8);
        g.setColour (colours::text2);
        g.drawText (juce::String (bar + 1), x + 4, ruler.getY() + 4, 40, 14, juce::Justification::centredLeft);
    }
    g.setColour (colours::text);
    g.setFont (font (13, true));
    auto title = getLocalBounds().removeFromTop (toolbarH).reduced (10, 0);
    title.removeFromLeft (560);
    juce::String info = c->name + "  ·  "_u + juce::String ((int) c->notes.size()) + " notas";
    if (auto* t = track(); t != nullptr && t->instrument) info << "  ·  "_u << t->instrument->name;
    g.drawText (info, title.withTrimmedRight (110), juce::Justification::centredLeft, true);
}

void PianoRoll::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // Si el ítem desaparece (deshacer, borrar), las notas seleccionadas ya no existen.
    if (auto* c = clip())
        selection.erase (std::remove_if (selection.begin(), selection.end(), [c] (auto& id)
                         { return std::none_of (c->notes.begin(), c->notes.end(), [&] (auto& n) { return n.id == id; }); }), selection.end());
    updateButtons();
    resized();
    repaint();
    grid_->repaint();
    velocity->repaint();
}

void PianoRoll::timerCallback()
{
    keys->scrollY = view.getViewPositionY();
    velocity->scrollX = view.getViewPositionX();
    keys->repaint();
    velocity->repaint();
    if (ctx.session.isPlaying()) { grid_->repaint(); repaint (getLocalBounds().withTrimmedTop (toolbarH).removeFromTop (rulerH)); }
}

void PianoRoll::quantize()
{
    auto ids = selection;
    const auto id = ctx.session.editingMidiClip;
    const auto g = grid, st = strength;
    const bool lengths = quantizeLengths;
    ctx.session.edit ([&] (Project& p) { if (auto* c = edit::findMidiClip (p, id)) edit::midi::quantize (*c, c->start, ids, g, st, lengths); });
}

void PianoRoll::transpose (int semis)
{
    if (selection.empty()) return;
    auto ids = selection;
    const auto id = ctx.session.editingMidiClip;
    ctx.session.edit ([&] (Project& p) { if (auto* c = edit::findMidiClip (p, id)) edit::midi::transpose (*c, ids, semis); });
    if (auto* c = clip())
        for (auto& n : c->notes) if (n.id == ids.front()) { preview (n.pitch, n.velocity); break; }
}

void PianoRoll::nudge (int dir)
{
    if (selection.empty()) return;
    auto ids = selection;
    const auto id = ctx.session.editingMidiClip;
    const auto dt = grid * dir;
    ctx.session.edit ([&] (Project& p) { if (auto* c = edit::findMidiClip (p, id)) edit::midi::move (*c, ids, dt, 0); });
}

void PianoRoll::duplicate()
{
    if (selection.empty()) return;
    auto ids = selection;
    const auto id = ctx.session.editingMidiClip;
    std::vector<juce::Uuid> created;
    ctx.session.edit ([&] (Project& p) { if (auto* c = edit::findMidiClip (p, id)) created = edit::midi::duplicate (*c, ids); });
    selection = created;
}

void PianoRoll::removeSelected()
{
    if (selection.empty()) return;
    auto ids = selection;
    const auto id = ctx.session.editingMidiClip;
    ctx.session.edit ([&] (Project& p) { if (auto* c = edit::findMidiClip (p, id)) edit::midi::remove (*c, ids); });
    selection.clear();
}

void PianoRoll::selectAll()
{
    selection.clear();
    if (auto* c = clip()) for (auto& n : c->notes) selection.push_back (n.id);
    grid_->repaint();
}

bool PianoRoll::keyPressed (const juce::KeyPress& k)
{
    const auto mods = k.getModifiers();
    const auto code = k.getKeyCode();
    auto& s = ctx.session;
    if (mods.isCommandDown())
    {
        if (code == 'A') { selectAll(); return true; }
        if (code == 'D') { duplicate(); return true; }
        if (code == 'Z') { mods.isShiftDown() ? s.redo() : s.undo(); return true; }
        return false;
    }
    if (code == juce::KeyPress::spaceKey) { if (! s.isPlaying()) if (auto* c = clip()) s.seek (c->start); s.togglePlay(); return true; }
    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey) { removeSelected(); return true; }
    if (code == juce::KeyPress::escapeKey) { selection.clear(); grid_->repaint(); return true; }
    if (code == juce::KeyPress::upKey) { transpose (mods.isShiftDown() ? 12 : 1); return true; }
    if (code == juce::KeyPress::downKey) { transpose (mods.isShiftDown() ? -12 : -1); return true; }
    if (code == juce::KeyPress::leftKey) { nudge (-1); return true; }
    if (code == juce::KeyPress::rightKey) { nudge (1); return true; }
    const auto ch = juce::CharacterFunctions::toLowerCase (k.getTextCharacter());
    if (ch == 'q') { quantize(); return true; }
    if (ch == 'd') { tool = Tool::draw; updateButtons(); return true; }
    if (ch == 's') { tool = Tool::select; updateButtons(); return true; }
    if (ch == '+' || ch == '=') { ppb = juce::jmin (800.0, ppb * 1.4); resized(); return true; }
    if (ch == '-') { ppb = juce::jmax (15.0, ppb / 1.4); resized(); return true; }
    return false;
}

} // namespace wdaw::ui
