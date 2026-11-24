#include "Mixer.h"

namespace wdaw::ui
{

//==============================================================================
void Fader::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0, 8);
    auto rail = r.withSizeKeepingCentre (6, r.getHeight());
    drawMeter (g, rail, level, true);
    const auto zeroY = r.getBottom() - r.getHeight() * (float) faderPosition (0);
    g.setColour (colours::text3);
    g.fillRect (r.getCentreX() - 10, zeroY, 20.0f, 1.0f);
    const auto y = r.getBottom() - r.getHeight() * (float) faderPosition (db);
    auto cap = juce::Rectangle<float> (30, 16).withCentre ({ r.getCentreX(), y });
    g.setColour (juce::Colour (0x80000000));
    g.fillRoundedRectangle (cap.translated (0, 2), 5);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white, cap.getTopLeft(), juce::Colour (0xffc8c8d0), cap.getBottomLeft(), false));
    g.fillRoundedRectangle (cap, 5);
    g.setColour (tint);
    g.fillRect (cap.withSizeKeepingCentre (cap.getWidth(), 2));
}

void Fader::mouseDown (const juce::MouseEvent&)
{
    startPos = faderPosition (db);
    dragging = true;
    if (onBegin) onBegin();
}

void Fader::mouseUp (const juce::MouseEvent&)
{
    dragging = false;
    if (onEnd) onEnd();
}

void Fader::mouseDrag (const juce::MouseEvent& e)
{
    const auto h = (double) getHeight() - 16;
    const auto fine = e.mods.isShiftDown() ? 0.2 : 1.0;
    const auto pos = startPos - e.getDistanceFromDragStartY() / h * fine;
    db = std::round (faderDb (pos) * 10) / 10;
    if (onChange) onChange (db);
    repaint();
}

void Fader::mouseDoubleClick (const juce::MouseEvent&)
{
    if (onBegin) onBegin();
    db = 0;
    if (onChange) onChange (db);
    if (onEnd) onEnd();
    repaint();
}

void Fader::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    db = juce::jlimit (Decibels::silence, 6.0, db + (w.deltaY > 0 ? 0.5 : -0.5));
    if (onChange) onChange (db);
    repaint();
}

//==============================================================================
/** Canal del mezclador. Si `isMaster`, controla el Master (volumen y plugins de mastering). */
class ChannelStrip : public juce::Component
{
public:
    ChannelStrip (Context& c, juce::Uuid id, bool master) : ctx (c), trackId (id), isMaster (master)
    {
        auto knob = [this] (juce::Slider& k, double min, double max, juce::Colour col, const juce::String& tip)
        {
            k.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            k.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            k.setRange (min, max, 0.1);
            k.setColour (juce::Slider::rotarySliderFillColourId, col);
            k.setDoubleClickReturnValue (true, 0);
            k.setTooltip (tip);
            k.onDragStart = [this] { ctx.session.beginGesture(); };
            addAndMakeVisible (k);
        };
        if (! isMaster)
        {
            knob (high, -12, 12, colours::accent2, "Agudos (8 kHz)");
            knob (mid, -12, 12, colours::accent, "Medios (1 kHz)");
            knob (low, -12, 12, juce::Colour (0xffff9966), "Graves (100 Hz)");
            knob (pan, -1, 1, colours::text2, "Paneo");
            pan.setRange (-1, 1, 0.01);
            high.onValueChange = [this] { setEq (2, (float) high.getValue()); };
            mid.onValueChange  = [this] { setEq (1, (float) mid.getValue()); };
            low.onValueChange  = [this] { setEq (0, (float) low.getValue()); };
            pan.onDragStart = [this] { ctx.session.touchBegin (trackId, { AutomationTarget::Kind::pan }); };
            pan.onDragEnd = [this] { ctx.session.touchEnd (trackId, { AutomationTarget::Kind::pan }); };
            pan.onValueChange  = [this] { ctx.session.setControl (trackId, { AutomationTarget::Kind::pan }, pan.getValue()); };
            for (auto* b : { &muteButton, &soloButton, &armButton }) addAndMakeVisible (*b);
            muteButton.onClick = [this] { ctx.session.updateTrack (trackId, [] (Track& t) { t.muted = ! t.muted; }); };
            soloButton.onClick = [this] { ctx.session.updateTrack (trackId, [] (Track& t) { t.solo = ! t.solo; }); };
            armButton.onClick  = [this] { ctx.session.updateTrack (trackId, [] (Track& t) { t.armed = ! t.armed; }); };
        }
        fader.onBegin = [this] { if (isMaster) ctx.session.beginGesture(); else ctx.session.touchBegin (trackId, { AutomationTarget::Kind::volume }); };
        fader.onEnd = [this] { if (! isMaster) ctx.session.touchEnd (trackId, { AutomationTarget::Kind::volume }); };
        fader.onChange = [this] (double db)
        {
            if (isMaster) ctx.session.edit ([db] (Project& p) { p.masterVolumeDb = db; }, false);
            else ctx.session.setControl (trackId, { AutomationTarget::Kind::volume }, db);
        };
        addAndMakeVisible (fader);
        update();
    }

    const Track* track() const { return ctx.session.project().findTrack (trackId); }

    void setEq (int band, float v)
    {
        ctx.session.updateTrack (trackId, [band, v] (Track& t)
        {
            (band == 0 ? t.eq.lowGain : band == 1 ? t.eq.midGain : t.eq.highGain) = v;
        }, false);
    }

    void update()
    {
        if (isMaster)
        {
            fader.db = ctx.session.project().masterVolumeDb;
            fader.tint = colours::accent;
        }
        else if (auto* t = track())
        {
            high.setValue (t->eq.highGain, juce::dontSendNotification);
            mid.setValue (t->eq.midGain, juce::dontSendNotification);
            low.setValue (t->eq.lowGain, juce::dontSendNotification);
            muteButton.setToggleState (t->muted, juce::dontSendNotification);
            soloButton.setToggleState (t->solo, juce::dontSendNotification);
            armButton.setToggleState (t->armed, juce::dontSendNotification);
            armButton.setVisible (! t->isBus());
            fader.tint = colours::named (t->colour);
            refreshValues();
        }
        resized();
        repaint();
    }

    /** Valores que suenan ahora (siguen a la automatización durante la reproducción). */
    void refreshValues()
    {
        auto* t = track();
        if (isMaster || t == nullptr) return;
        if (! fader.dragging) fader.db = ctx.session.effectiveValue (*t, { AutomationTarget::Kind::volume });
        if (! pan.isMouseButtonDown())
        {
            const auto p = ctx.session.effectiveValue (*t, { AutomationTarget::Kind::pan });
            pan.setValue (p, juce::dontSendNotification);
            pan.setTooltip ("Paneo: " + formatPan (p));
        }
    }

    std::vector<PluginSlot> slots() const
    {
        if (isMaster) return ctx.session.project().masterInserts;
        if (auto* t = track()) return t->inserts;
        return {};
    }

    int slotsTop() const { return 40 + (! isMaster && track() != nullptr && track()->isInstrument() ? 26 : 0); }
    static constexpr int sendRowH = 20, sendGap = 3;
    int sendsTop() const { return slotsTop() + (int) slots().size() * 23 + 20 + 10; }
    int numSends() const { auto* t = track(); return isMaster || t == nullptr ? 0 : (int) t->sends.size(); }
    /** Envíos: título + una fila por envío + "+ envío". */
    int sendsHeight() const { return isMaster ? 0 : 14 + numSends() * (sendRowH + sendGap) + 18 + 8; }
    juce::Rectangle<int> sendRow (int i) const { return { 8, sendsTop() + 14 + i * (sendRowH + sendGap), getWidth() - 16, sendRowH }; }
    juce::Rectangle<int> addSendArea() const { return { 8, sendsTop() + 14 + numSends() * (sendRowH + sendGap), getWidth() - 16, 18 }; }
    juce::Rectangle<int> outputArea() const { return getLocalBounds().removeFromBottom (16).reduced (6, 0); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 10);
        r.removeFromTop (sendsTop() - 10 + sendsHeight());
        if (! isMaster)
        {
            // EQ en una fila (graves · medios · agudos) y el paneo debajo: el fader conserva su altura.
            auto eqRow = r.removeFromTop (26);
            const int col = eqRow.getWidth() / 3;
            for (auto* k : { &low, &mid, &high }) k->setBounds (eqRow.removeFromLeft (col).withSizeKeepingCentre (26, 26));
            r.removeFromTop (14);
            pan.setBounds (r.removeFromTop (28).withSizeKeepingCentre (28, 28));
            r.removeFromTop (14);
            auto pills = r.removeFromBottom (24);
            const int w = 22, gap = 3;
            auto row = pills.withSizeKeepingCentre (w * 3 + gap * 2, 22);
            muteButton.setBounds (row.removeFromLeft (w)); row.removeFromLeft (gap);
            soloButton.setBounds (row.removeFromLeft (w)); row.removeFromLeft (gap);
            armButton.setBounds (row.removeFromLeft (w));
            r.removeFromBottom (20);
        }
        else r.removeFromBottom (20);
        fader.setBounds (r.withSizeKeepingCentre (40, r.getHeight()));
    }

    void paint (juce::Graphics& g) override
    {
        auto* t = track();
        const bool selected = ! isMaster && ctx.session.isTrackSelected (trackId);
        const auto colour = isMaster ? colours::accent : (t != nullptr ? colours::named (t->colour) : colours::accent);
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        if (isMaster) g.setGradientFill (juce::ColourGradient (colours::elevated, r.getTopLeft(), colours::panel, r.getBottomLeft(), false));
        else g.setColour (selected ? colours::elevated : colours::panel);
        g.fillRoundedRectangle (r, 12);
        g.setColour (isMaster ? colours::accent.withAlpha (0.35f) : selected ? colour.withAlpha (0.5f) : colours::stroke);
        g.drawRoundedRectangle (r, 12, 1);

        auto content = getLocalBounds().reduced (8, 10);
        auto cap = content.removeFromTop (3).toFloat();
        if (isMaster) g.setGradientFill (accentGradient (cap)); else g.setColour (colour);
        g.fillRoundedRectangle (cap, 1.5f);
        content.removeFromTop (5);
        g.setColour (colours::text);
        g.setFont (font (12, true));
        g.drawFittedText (isMaster ? juce::String ("Master") : (t != nullptr ? t->name : juce::String()), content.removeFromTop (16), juce::Justification::centred, 1);
        content.removeFromTop (6);

        // Instrumento (pistas MIDI): primer slot, como el VSTi en REAPER
        if (! isMaster && t != nullptr && t->isInstrument())
        {
            auto slot = content.removeFromTop (22).toFloat();
            content.removeFromTop (4);
            g.setColour (colour.withAlpha (0.3f));
            g.fillRoundedRectangle (slot, 5);
            g.setColour (colours::text);
            g.setFont (font (10, true));
            g.drawText (t->instrument ? t->instrument->name : juce::String ("Elegir instrumento"), slot.reduced (6, 0), juce::Justification::centredLeft, true);
        }
        // Plugins
        for (auto& s : slots())
        {
            auto slot = content.removeFromTop (20).toFloat();
            content.removeFromTop (3);
            g.setColour (juce::Colour (0x0fffffff));
            g.fillRoundedRectangle (slot, 5);
            g.setColour (s.bypassed ? colours::text3 : colours::accent2);
            g.fillEllipse (juce::Rectangle<float> (5, 5).withCentre ({ slot.getX() + 7, slot.getCentreY() }));
            g.setColour (s.bypassed ? colours::text3 : colours::text);
            g.setFont (font (10, true));
            g.drawText (s.name, slot.withTrimmedLeft (13).withTrimmedRight (3), juce::Justification::centredLeft, true);
        }
        auto add = content.removeFromTop (20).toFloat();
        g.setColour (colours::stroke);
        g.drawRoundedRectangle (add.reduced (0.5f), 5, 1);
        g.setColour (colours::text3);
        g.setFont (font (12, true));
        g.drawText ("+", add, juce::Justification::centred);
        addArea = add.toNearestInt();

        // Envíos (a buses de efectos o de monitoreo)
        if (! isMaster && t != nullptr)
        {
            g.setColour (colours::text3);
            g.setFont (font (9, true));
            g.drawText (juce::String::fromUTF8 ("ENVÍOS"), juce::Rectangle<int> (8, sendsTop(), getWidth() - 16, 12), juce::Justification::centredLeft);
            for (int i = 0; i < numSends(); ++i)
            {
                const auto& snd = t->sends[(size_t) i];
                auto row = sendRow (i).toFloat();
                g.setColour (juce::Colour (0x0fffffff));
                g.fillRoundedRectangle (row, 5);
                const auto level = ctx.session.effectiveValue (*t, { AutomationTarget::Kind::sendLevel, snd.id });
                auto bar = row.reduced (1);
                bar.setWidth (bar.getWidth() * (float) faderPosition (level));
                g.setColour ((snd.muted ? colours::text3 : snd.preFader ? colours::solo : colours::accent2).withAlpha (0.35f));
                g.fillRoundedRectangle (bar, 4);
                auto* bus = ctx.session.project().findTrack (snd.target);
                g.setColour (snd.muted ? colours::text3 : colours::text);
                g.setFont (font (10, true));
                g.drawText ((snd.preFader ? "pre " : "") + (bus != nullptr ? bus->name : juce::String ("?")), row.reduced (5, 0), juce::Justification::centredLeft, true);
            }
            auto addSend = addSendArea().toFloat();
            g.setColour (colours::stroke);
            g.drawRoundedRectangle (addSend.reduced (0.5f), 5, 1);
            g.setColour (colours::text3);
            g.setFont (font (10, true));
            g.drawText (juce::String::fromUTF8 ("+ envío"), addSend, juce::Justification::centred);
        }

        if (! isMaster)
        {
            g.setFont (font (8.5f, true));
            g.setColour (colours::text3);
            for (auto* k : { &high, &mid, &low })
            {
                const auto label = k == &high ? "Agu" : k == &mid ? "Med" : "Gra";
                g.drawText (label, k->getBounds().translated (0, 26).withHeight (11).expanded (4, 0), juce::Justification::centred);
            }
            g.setColour (colours::text2);
            g.setFont (font (9, true));
            if (t != nullptr)
                g.drawText (formatPan (pan.getValue()), pan.getBounds().translated (0, 27).withHeight (12).expanded (14, 0), juce::Justification::centred);
        }
        // Valor del fader y salida
        g.setColour (colours::text2);
        g.setFont (mono (10));
        auto dbArea = fader.getBounds().withY (fader.getBottom() + 2).withHeight (14).expanded (20, 0);
        g.drawText (formatDb (fader.db), dbArea, juce::Justification::centred);
        if (! isMaster && t != nullptr)
        {
            // Salida (clic para cambiarla). Armada: muestra la entrada.
            const bool routed = t->directOutPair >= 2 || ! t->outputBus.isNull();
            g.setColour (t->armed ? colours::record : routed ? colours::accent2 : colours::text3);
            g.setFont (font (9, true));
            auto info = t->armed ? (t->isInstrument() ? juce::String ("MIDI") : t->input.displayName()) : outputLabel (ctx.session.project(), *t);
            g.drawText (info, outputArea(), juce::Justification::centred, true);
        }
        if (t != nullptr && t->isBus())
        {
            const juce::String tagText = t->isFolder ? "CARPETA" : "BUS";
            auto tag = juce::Rectangle<float> (0, 0, t->isFolder ? 50.0f : 28.0f, 12).withCentre ({ getWidth() / 2.0f, 11.5f });
            g.setColour (colours::text3.withAlpha (0.25f));
            g.fillRoundedRectangle (tag, 6);
            g.setColour (colours::text2);
            g.setFont (font (8.5f, true));
            g.drawText (tagText, tag, juce::Justification::centred);
        }
    }

    int slotIndexAt (juce::Point<int> p) const
    {
        int y = slotsTop();
        const auto all = slots();
        for (int i = 0; i < (int) all.size(); ++i)
        {
            if (p.y >= y && p.y < y + 20 && p.x > 8 && p.x < getWidth() - 8) return i;
            y += 23;
        }
        return -1;
    }

    bool instrumentSlotHit (juce::Point<int> p) const
    {
        return ! isMaster && track() != nullptr && track()->isInstrument() && p.y >= 40 && p.y < 62;
    }

    int sendIndexAt (juce::Point<int> p) const
    {
        for (int i = 0; i < numSends(); ++i) if (sendRow (i).contains (p)) return i;
        return -1;
    }

    void showSendMenu (int index)
    {
        auto* t = track();
        if (t == nullptr || index >= (int) t->sends.size()) return;
        const auto snd = t->sends[(size_t) index];
        const auto id = trackId;
        auto& s = ctx.session;
        juce::PopupMenu m;
        m.addItem ("Pre-fader (no sigue al volumen de la pista)", true, snd.preFader, [&s, id, sid = snd.id]
        {
            s.updateTrack (id, [sid] (Track& x) { for (auto& e : x.sends) if (e.id == sid) e.preFader = ! e.preFader; });
        });
        m.addItem ("Silenciar", true, snd.muted, [&s, id, sid = snd.id]
        {
            s.updateTrack (id, [sid] (Track& x) { for (auto& e : x.sends) if (e.id == sid) e.muted = ! e.muted; });
        });
        m.addItem ("0 dB", [&s, id, sid = snd.id] { s.beginGesture(); s.setControl (id, { AutomationTarget::Kind::sendLevel, sid }, 0.0); });
        m.addItem (juce::String::fromUTF8 ("Mostrar automatización"), [&s, id, sid = snd.id] { s.showLane (id, { AutomationTarget::Kind::sendLevel, sid }); });
        m.addSeparator();
        m.addItem (juce::String::fromUTF8 ("Quitar envío"), [&s, id, sid = snd.id] { s.removeSend (id, sid); });
        m.showMenuAsync (juce::PopupMenu::Options());
    }

    int draggingSend = -1;
    double sendStartPos = 0;

    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto* t = track();
        if (draggingSend < 0 || t == nullptr || draggingSend >= (int) t->sends.size()) return;
        const auto fine = e.mods.isShiftDown() ? 0.2 : 1.0;
        const auto pos = sendStartPos + e.getDistanceFromDragStartX() / (double) sendRow (0).getWidth() * fine;
        const auto db = std::round (faderDb (juce::jlimit (0.0, 1.0, pos)) * 10) / 10;
        ctx.session.setControl (trackId, { AutomationTarget::Kind::sendLevel, t->sends[(size_t) draggingSend].id }, db);
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (draggingSend >= 0)
            if (auto* t = track(); t != nullptr && draggingSend < (int) t->sends.size())
                ctx.session.touchEnd (trackId, { AutomationTarget::Kind::sendLevel, t->sends[(size_t) draggingSend].id });
        draggingSend = -1;
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        auto* t = track();
        const auto i = sendIndexAt (e.getPosition());
        if (t == nullptr || i < 0) return;
        ctx.session.beginGesture();
        ctx.session.setControl (trackId, { AutomationTarget::Kind::sendLevel, t->sends[(size_t) i].id }, 0.0);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! isMaster) ctx.session.selectTrack (trackId);
        const auto p = e.getPosition();
        if (! isMaster)
        {
            auto* t = track();
            if (const auto i = sendIndexAt (p); i >= 0 && t != nullptr)
            {
                if (e.mods.isPopupMenu()) { showSendMenu (i); return; }
                const auto target = AutomationTarget { AutomationTarget::Kind::sendLevel, t->sends[(size_t) i].id };
                draggingSend = i;
                sendStartPos = faderPosition (ctx.session.effectiveValue (*t, target));
                ctx.session.touchBegin (trackId, target);
                return;
            }
            if (t != nullptr && (addSendArea().contains (p) || outputArea().contains (p)))
            {
                juce::PopupMenu m;
                addRoutingMenu (m, ctx.session, *t);
                // Abrir directamente el submenú que corresponde
                juce::PopupMenu::MenuItemIterator it (m);
                while (it.next())
                    if (auto* sub = it.getItem().subMenu.get())
                        if (it.getItem().text.startsWith (addSendArea().contains (p) ? juce::String::fromUTF8 ("Envíos") : juce::String ("Salida")))
                        {
                            sub->showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
                            return;
                        }
                m.showMenuAsync (juce::PopupMenu::Options());
                return;
            }
        }
        if (addArea.contains (p))
        {
            if (isMaster) { if (ctx.addMasterPlugin) ctx.addMasterPlugin(); }
            else if (ctx.choosePlugin) ctx.choosePlugin (trackId, false);
            return;
        }
        if (instrumentSlotHit (p))
        {
            auto* t = track();
            if (e.mods.isPopupMenu() || ! t->instrument) { if (ctx.choosePlugin) ctx.choosePlugin (trackId, true); }
            else if (ctx.openPluginWindow) ctx.openPluginWindow (trackId, t->instrument->id);
            return;
        }
        if (const auto i = slotIndexAt (p); i >= 0)
        {
            const auto all = slots();
            const auto slot = all[(size_t) i];
            if (e.mods.isPopupMenu()) { showSlotMenu (slot, i); return; }
            if (ctx.openPluginWindow) ctx.openPluginWindow (isMaster ? juce::Uuid::null() : trackId, slot.id);
            return;
        }
        if (e.mods.isPopupMenu() && ! isMaster)
            if (auto* t = track())
            {
                juce::PopupMenu m;
                if (t->isBus()) addRoutingMenu (m, ctx.session, *t);
                else addTrackInputMenus (m, ctx.session, *t);
                juce::PopupMenu automation;
                addAutomationMenu (automation, ctx.session, *t);
                m.addSubMenu (juce::String::fromUTF8 ("Automatización"), automation);
                m.addSeparator();
                addMidiLearnMenu (m, ctx.session, *t);
                m.showMenuAsync (juce::PopupMenu::Options());
            }
        if (e.mods.isPopupMenu() && isMaster)
        {
            juce::PopupMenu m;
            m.addItem ("Aprender MIDI: volumen del Master", [this] { ctx.session.midiLearn.startLearning (MidiMapping::Target::masterVolume); ctx.session.sendChangeMessage(); });
            m.showMenuAsync (juce::PopupMenu::Options());
        }
    }

    void showSlotMenu (const PluginSlot& slot, int index)
    {
        juce::PopupMenu m;
        const auto id = slot.id;
        auto change = [this, id] (std::function<void (std::vector<PluginSlot>&, size_t)> fn)
        {
            ctx.session.edit ([&] (Project& p)
            {
                auto& list = isMaster ? p.masterInserts : p.findTrack (trackId)->inserts;
                for (size_t i = 0; i < list.size(); ++i) if (list[i].id == id) { fn (list, i); return; }
            });
        };
        m.addItem ("Abrir", [this, id] { if (ctx.openPluginWindow) ctx.openPluginWindow (isMaster ? juce::Uuid::null() : trackId, id); });
        m.addItem (slot.bypassed ? "Activar" : "Bypass", [change] { change ([] (auto& l, size_t i) { l[i].bypassed = ! l[i].bypassed; }); });
        if (index > 0) m.addItem ("Mover arriba", [change] { change ([] (auto& l, size_t i) { std::swap (l[i], l[i - 1]); }); });
        if (index + 1 < (int) slots().size()) m.addItem ("Mover abajo", [change] { change ([] (auto& l, size_t i) { std::swap (l[i], l[i + 1]); }); });
        m.addSeparator();
        m.addItem ("Quitar", [change] { change ([] (auto& l, size_t i) { l.erase (l.begin() + (long) i); }); });
        m.showMenuAsync (juce::PopupMenu::Options());
    }

    Context& ctx;
    juce::Uuid trackId;
    bool isMaster;
    juce::Slider high, mid, low, pan;
    PillButton muteButton { "M", colours::mute }, soloButton { "S", colours::solo }, armButton { "R", colours::record };
    Fader fader;
    juce::Rectangle<int> addArea;
};

//==============================================================================
Mixer::Mixer (Context& c) : ctx (c)
{
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (false, true);
    viewport.setScrollBarThickness (10);
    addAndMakeVisible (viewport);
    ctx.session.addChangeListener (this);
    rebuild();
}

Mixer::~Mixer() { ctx.session.removeChangeListener (this); }

void Mixer::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
    g.setColour (colours::strokeHi);
    g.fillRect (0, 0, getWidth(), 1);
}

void Mixer::rebuild()
{
    auto& proj = ctx.session.project();
    std::vector<juce::Uuid> visible;   // las pistas dentro de una carpeta cerrada no se muestran
    for (auto& t : proj.tracks) if (! proj.isInsideCollapsedFolder (t)) visible.push_back (t.id);
    bool same = strips.size() == (int) visible.size();
    for (int i = 0; same && i < strips.size(); ++i) same = strips[i]->trackId == visible[(size_t) i];
    if (! same)
    {
        strips.clear();
        for (auto& id : visible) content.addAndMakeVisible (strips.add (new ChannelStrip (ctx, id, false)));
        if (master == nullptr) { master = std::make_unique<ChannelStrip> (ctx, juce::Uuid::null(), true); content.addAndMakeVisible (*master); }
    }
    for (auto* s : strips) s->update();
    if (master != nullptr) master->update();
    resized();
}

void Mixer::resized()
{
    viewport.setBounds (getLocalBounds().reduced (0, 1));
    const int w = 96, gap = 8, h = juce::jmax (300, viewport.getHeight() - 14);
    int x = 10;
    for (auto* s : strips) { s->setBounds (x, 8, w, h - 8); x += w + gap; }
    if (master != nullptr) { master->setBounds (x + 8, 8, w, h - 8); x += w + gap + 8; }
    content.setSize (juce::jmax (x + 10, viewport.getWidth()), h);
}

void Mixer::repaintMeters()
{
    const bool playing = ctx.session.isPlaying();
    for (auto* s : strips)
    {
        s->fader.level = ctx.meters.track (s->trackId);
        if (playing) { s->refreshValues(); s->repaint(); }
        else s->fader.repaint();
    }
    if (master != nullptr) { master->fader.level = ctx.meters.master(); master->fader.repaint(); }
}

} // namespace wdaw::ui
