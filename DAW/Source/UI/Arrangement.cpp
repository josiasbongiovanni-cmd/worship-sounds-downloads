#include "Arrangement.h"
#include "Model/Serialization.h"

namespace wdaw::ui
{

//==============================================================================
/** Cabecera de una pista: color, nombre, M / S / R, monitoreo, entrada, volumen y medidor.
    Arrastrar la cabecera reordena las pistas. */
class TrackHeader : public juce::Component
{
public:
    TrackHeader (Context& c, const juce::Uuid& id) : ctx (c), trackId (id)
    {
        for (auto* b : { &muteButton, &soloButton, &armButton, &autoButton }) addAndMakeVisible (*b);
        autoButton.setTooltip (juce::String::fromUTF8 ("Automatización: modo y carriles"));
        autoButton.onClick = [this]
        {
            autoButton.setToggleState (isWriteMode(), juce::dontSendNotification);
            if (auto* t = track())
            {
                juce::PopupMenu m;
                addAutomationMenu (m, ctx.session, *t);
                m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&autoButton));
            }
        };
        muteButton.onClick = [this] { ctx.session.updateTrack (trackId, [] (Track& t) { t.muted = ! t.muted; }); };
        soloButton.onClick = [this] { ctx.session.updateTrack (trackId, [] (Track& t) { t.solo = ! t.solo; }); };
        armButton.onClick  = [this] { ctx.session.updateTrack (trackId, [] (Track& t) { t.armed = ! t.armed; }); };
        armButton.setTooltip ("Armar para grabar (clic derecho: entrada y monitoreo)");
        armButton.addMouseListener (this, false);
        volume.setSliderStyle (juce::Slider::LinearHorizontal);
        volume.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        volume.setRange (0.0, 1.0);
        volume.setDoubleClickReturnValue (true, faderPosition (0));
        volume.onDragStart = [this] { ctx.session.touchBegin (trackId, { AutomationTarget::Kind::volume }); };
        volume.onDragEnd = [this] { ctx.session.touchEnd (trackId, { AutomationTarget::Kind::volume }); };
        volume.onValueChange = [this]
        {
            const auto db = std::round (faderDb (volume.getValue()) * 10) / 10;
            ctx.session.setControl (trackId, { AutomationTarget::Kind::volume }, db);
        };
        addAndMakeVisible (volume);
        update();
    }

    const Track* track() const { return ctx.session.project().findTrack (trackId); }
    int indent() const { auto* t = track(); return t != nullptr ? juce::jmin (5, ctx.session.project().folderDepth (*t)) * 14 : 0; }
    juce::Rectangle<int> contentArea() const { return getLocalBounds().reduced (12, 8).withTrimmedLeft (4 + indent()); }
    juce::Rectangle<int> folderToggleArea() const { return contentArea().removeFromTop (22).removeFromLeft (16); }
    bool isWriteMode() const
    {
        auto* t = track();
        return t != nullptr && (t->automationMode == AutomationMode::touch || t->automationMode == AutomationMode::latch);
    }

    void update()
    {
        auto* t = track();
        if (t == nullptr) return;
        muteButton.setToggleState (t->muted, juce::dontSendNotification);
        soloButton.setToggleState (t->solo, juce::dontSendNotification);
        armButton.setToggleState (t->armed, juce::dontSendNotification);
        armButton.setVisible (! t->isBus());
        autoButton.setToggleState (isWriteMode(), juce::dontSendNotification);
        autoButton.onColour = t->automationMode == AutomationMode::latch ? colours::record : colours::accent2;
        autoButton.setTooltip (juce::String::fromUTF8 ("Automatización: ") + displayName (t->automationMode));
        volume.setColour (juce::Slider::trackColourId, colours::named (t->colour));
        refreshValues();
        resized();
        repaint();
    }

    /** Durante la reproducción el fader sigue a la automatización (como en REAPER). */
    void refreshValues()
    {
        auto* t = track();
        if (t == nullptr || volume.isMouseButtonDown()) return;
        shownDb = ctx.session.effectiveValue (*t, { AutomationTarget::Kind::volume });
        volume.setValue (faderPosition (shownDb), juce::dontSendNotification);
        volume.setTooltip ("Volumen: " + formatDb (shownDb) + " dB");
    }
    double shownDb = 0;

    void resized() override
    {
        auto r = contentArea();
        r.removeFromTop (24);
        auto row = r.removeFromTop (22);
        auto* t = track();
        muteButton.setBounds (row.removeFromLeft (22)); row.removeFromLeft (4);
        soloButton.setBounds (row.removeFromLeft (22)); row.removeFromLeft (4);
        if (t == nullptr || ! t->isBus()) { armButton.setBounds (row.removeFromLeft (22)); row.removeFromLeft (4); }
        autoButton.setBounds (row.removeFromLeft (22)); row.removeFromLeft (8);
        row.removeFromRight (40);
        volume.setBounds (row);
    }

    void paint (juce::Graphics& g) override
    {
        auto* t = track();
        if (t == nullptr) return;
        const bool selected = ctx.session.isTrackSelected (trackId);
        const auto colour = colours::named (t->colour);
        g.fillAll (selected ? colours::elevated : colours::panel);
        g.setColour (colours::stroke);
        g.fillRect (0, getHeight() - 1, getWidth(), 1);
        // Guías de carpeta (una línea por nivel)
        for (int level = 0; level < indent() / 14; ++level)
        {
            g.setColour (colours::strokeHi);
            g.fillRect (12.0f + (float) level * 14.0f, 0.0f, 1.0f, (float) getHeight());
        }
        auto strip = juce::Rectangle<float> (6.0f + (float) indent(), 8, 4, (float) getHeight() - 16);
        if (selected) { g.setColour (colour.withAlpha (0.5f)); g.fillRoundedRectangle (strip.expanded (2), 3); }
        g.setColour (colour);
        g.fillRoundedRectangle (strip, 2);

        auto top = contentArea().removeFromTop (22).toFloat();
        if (t->isFolder)
        {
            // Triángulo para abrir / cerrar la carpeta y un ícono de carpeta
            auto tri = top.removeFromLeft (16).withSizeKeepingCentre (10, 10);
            juce::Path arrow;
            if (t->folderCollapsed) arrow.addTriangle (tri.getX() + 2, tri.getY(), tri.getRight() - 1, tri.getCentreY(), tri.getX() + 2, tri.getBottom());
            else arrow.addTriangle (tri.getX(), tri.getY() + 2, tri.getRight(), tri.getY() + 2, tri.getCentreX(), tri.getBottom() - 1);
            g.setColour (colours::text2);
            g.fillPath (arrow);
            auto folderIcon = top.removeFromLeft (16).withSizeKeepingCentre (14, 11);
            juce::Path f;
            f.addRoundedRectangle (folderIcon.getX(), folderIcon.getY() + 2, folderIcon.getWidth(), folderIcon.getHeight() - 2, 2.0f);
            f.addRoundedRectangle (folderIcon.getX(), folderIcon.getY(), folderIcon.getWidth() * 0.45f, 4.0f, 1.5f);
            g.setColour (colour);
            g.fillPath (f);
            top.removeFromLeft (5);
        }
        else if (t->isInstrument() || t->role == TrackRole::guide || t->isBus())
        {
            auto icon = IconButton::path (t->isInstrument() ? IconButton::Icon::piano : t->isBus() ? IconButton::Icon::mixer : IconButton::Icon::guide,
                                          top.removeFromLeft (14).withSizeKeepingCentre (12, 10));
            g.setColour (colour);
            g.strokePath (icon, juce::PathStrokeType (1.2f));
            top.removeFromLeft (5);
        }
        if (t->armed)
        {
            // Armada: entrada (y monitoreo) arriba a la derecha, para que el volumen tenga lugar
            auto info = top.removeFromRight (52);
            top.removeFromRight (4);
            g.setColour (colours::record);
            g.setFont (font (10, true));
            g.drawText (t->isInstrument() ? juce::String ("MIDI") : t->input.shortName(), info, juce::Justification::centredRight);
            if (t->monitor != MonitorMode::off && ! t->isInstrument())
            {
                g.setColour (colours::solo);
                g.fillPath (IconButton::path (IconButton::Icon::speaker, info.removeFromLeft (11).withSizeKeepingCentre (10, 9)));
            }
        }
        else if (t->directOutPair >= 2 || ! t->outputBus.isNull() || ! t->sends.empty() || (t->isFolder && t->folderCollapsed))
        {
            juce::String label;
            if (t->isFolder && t->folderCollapsed)
            {
                const auto [first, last] = edit::trackBlock (ctx.session.project(), trackId);
                label = juce::String (last - first) + (last - first == 1 ? " pista" : " pistas");
            }
            else if (t->directOutPair >= 2 || ! t->outputBus.isNull()) label = outputLabel (ctx.session.project(), *t);
            if (! t->sends.empty())
                label << (label.isEmpty() ? "" : "  ") << juce::String ((int) t->sends.size()) << (t->sends.size() == 1 ? juce::String::fromUTF8 (" envío") : juce::String::fromUTF8 (" envíos"));
            g.setFont (font (10));
            const auto w = juce::jmin (90.0f, (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), label) + 4);
            g.setColour (colours::text3);
            g.drawText (label, top.removeFromRight (w), juce::Justification::centredRight, true);
            top.removeFromRight (4);
        }
        if (! t->inserts.empty())
        {
            auto badge = top.removeFromRight (34).withSizeKeepingCentre (32, 16);
            g.setColour (colours::accent.withAlpha (0.2f));
            g.fillRoundedRectangle (badge, 8);
            g.setColour (colours::accent);
            g.setFont (font (10, true));
            g.drawText ("FX " + juce::String ((int) t->inserts.size()), badge, juce::Justification::centred);
        }
        g.setColour (selected ? colours::text : colours::text.withAlpha (0.85f));
        g.setFont (font (14, true));
        g.drawFittedText (t->name, top.toNearestInt(), juce::Justification::centredLeft, 1);

        auto row = contentArea();
        row.removeFromTop (24);
        auto controls = row.removeFromTop (22);
        g.setColour (colours::text3);
        g.setFont (mono (10));
        g.drawText (formatDb (shownDb), controls.removeFromRight (38), juce::Justification::centredRight);

        auto meter = row.removeFromTop (8).withTrimmedTop (5).toFloat();
        drawMeter (g, meter.withHeight (3), ctx.meters.track (trackId), false);

        if (ctx.session.midiLearn.isLearning())
            if (ctx.session.midiLearn.find (MidiMapping::Target::trackVolume, trackId) != nullptr)
            {
                g.setColour (colours::accent2);
                g.drawRect (getLocalBounds(), 1);
            }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.eventComponent == &armButton)
        {
            if (e.mods.isPopupMenu()) showInputMenu();
            return;
        }
        if (auto* t = track(); t != nullptr && t->isFolder && folderToggleArea().expanded (4).contains (e.getPosition()))
        {
            ctx.session.toggleFolderCollapsed (trackId);
            return;
        }
        if (e.mods.isPopupMenu())
        {
            if (! ctx.session.isTrackSelected (trackId)) ctx.session.selectTrack (trackId);
            showMenu();
            return;
        }
        ctx.session.selectTrack (trackId, e.mods.isCommandDown() || e.mods.isCtrlDown(), e.mods.isShiftDown());
        dragStartIndex = ctx.session.project().trackIndex (trackId);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.eventComponent != this || dragStartIndex < 0 || e.getDistanceFromDragStartY() == 0 || getParentComponent() == nullptr) return;
        auto* arr = findParentComponentOfClass<Arrangement>();
        if (arr == nullptr) return;
        const auto y = (float) e.getEventRelativeTo (getParentComponent()).position.y;
        auto under = arr->trackIndexAt (y);
        if (under < 0) under = y < 0 ? 0 : (int) ctx.session.project().tracks.size() - 1;
        const auto before = ctx.session.project().trackIndex (trackId);
        const auto parentBefore = track() != nullptr ? track()->parent : juce::Uuid::null();
        ctx.session.dragTrackTo (trackId, juce::jlimit (0, (int) ctx.session.project().tracks.size() - 1, under), ! movedDuringDrag);
        if (before == ctx.session.project().trackIndex (trackId) && track() != nullptr && track()->parent == parentBefore) return;
        movedDuringDrag = true;
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        dragStartIndex = -1;
        movedDuringDrag = false;
        setMouseCursor (juce::MouseCursor::NormalCursor);
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (e.eventComponent != this) return;
        rename();
    }

    void rename()
    {
        auto* t = track();
        if (t == nullptr || ! ctx.askText) return;
        ctx.askText ("Nombre de la pista", t->name, [this] (juce::String name)
        {
            if (name.isNotEmpty()) ctx.session.updateTrack (trackId, [name] (Track& tr) { tr.name = name; });
        });
    }

    void showInputMenu()
    {
        auto* t = track();
        if (t == nullptr) return;
        juce::PopupMenu m;
        addTrackInputMenus (m, ctx.session, *t);
        m.addSeparator();
        m.addItem (t->armed ? "Desarmar" : "Armar para grabar", [this] { ctx.session.updateTrack (trackId, [] (Track& x) { x.armed = ! x.armed; }); });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&armButton));
    }

    void showMenu()
    {
        auto* t = track();
        if (t == nullptr) return;
        juce::PopupMenu m;
        m.addItem ("Renombrar…"_u, [this] { rename(); });
        juce::PopupMenu colourMenu;
        for (auto& name : colours::names())
            colourMenu.addItem (colours::displayName (name), true, t->colour == name,
                                [this, name] { ctx.session.updateTrack (trackId, [name] (Track& x) { x.colour = name; }); });
        m.addSubMenu ("Color", colourMenu);
        if (t->isInstrument())
        {
            m.addItem ("Cambiar instrumento…"_u, [this] { if (ctx.choosePlugin) ctx.choosePlugin (trackId, true); });
            m.addItem ("Abrir instrumento", [this]
            {
                if (auto* tr = track(); tr != nullptr && tr->instrument && ctx.openPluginWindow) ctx.openPluginWindow (trackId, tr->instrument->id);
            });
            m.addItem (juce::String::fromUTF8 ("Nuevo ítem MIDI en el cursor"), [this] { ctx.session.createMidiClip (trackId); });
        }
        m.addItem ("Agregar plugin de efecto…"_u, [this] { if (ctx.choosePlugin) ctx.choosePlugin (trackId, false); });
        m.addSeparator();
        {
            juce::PopupMenu folders;
            auto& s = ctx.session;
            const auto count = juce::jmax ((size_t) 1, s.selectedTracks.size());
            folders.addItem (count > 1 ? "Nueva carpeta con las " + juce::String ((int) count) + " pistas seleccionadas" : juce::String ("Nueva carpeta con esta pista"),
                             [&s] { s.groupInFolder(); });
            if (t->isFolder)
                folders.addItem (t->folderCollapsed ? "Abrir carpeta" : "Cerrar carpeta", [&s, id = trackId] { s.toggleFolderCollapsed (id); });
            if (t->isBus() && ! t->isFolder)
                folders.addItem ("Usar este bus como carpeta", [&s, id = trackId] { s.updateTrack (id, [] (Track& x) { x.isFolder = true; }); });
            juce::PopupMenu into;
            for (auto& f : s.project().tracks)
                if (f.isFolder && f.id != trackId && f.id != t->parent && ! s.project().isDescendantOf (f, trackId))
                    into.addItem (f.name, [&s, id = trackId, fid = f.id] { s.moveIntoFolder (id, fid); });
            folders.addSubMenu ("Mover a la carpeta", into, into.getNumItems() > 0);
            if (! t->parent.isNull())
                folders.addItem ("Sacar de la carpeta", [&s, id = trackId] { s.takeOutOfFolder (id); });
            m.addSubMenu ("Carpeta", folders);
        }
        if (t->isBus()) addRoutingMenu (m, ctx.session, *t);
        else addTrackInputMenus (m, ctx.session, *t);
        juce::PopupMenu automation;
        addAutomationMenu (automation, ctx.session, *t);
        m.addSubMenu (juce::String::fromUTF8 ("Automatización"), automation);
        m.addSeparator();
        addMidiLearnMenu (m, ctx.session, *t);
        m.addSeparator();
        if (t->isFolder) m.addItem ("Eliminar carpeta (las pistas quedan)", [this] { ctx.session.removeTrack (trackId); });
        else m.addItem ("Eliminar pista", [this] { ctx.session.removeTrack (trackId); });
        m.showMenuAsync (juce::PopupMenu::Options());
    }

    Context& ctx;
    juce::Uuid trackId;
    PillButton muteButton { "M", colours::mute }, soloButton { "S", colours::solo }, armButton { "R", colours::record },
               autoButton { "A", colours::accent2 };
    juce::Slider volume;
    int dragStartIndex = -1;
    bool movedDuringDrag = false;
};

//==============================================================================
/** Cabecera de un carril de automatización: nombre, valor actual, activar y ocultar. */
class LaneHeader : public juce::Component
{
public:
    LaneHeader (Context& c, const juce::Uuid& track, const juce::Uuid& lane) : ctx (c), trackId (track), laneId (lane)
    {
        for (auto* b : { &activeButton, &hideButton }) addAndMakeVisible (*b);
        activeButton.setTooltip ("Activar / desactivar el carril (bypass)");
        hideButton.setTooltip ("Ocultar el carril (no borra los puntos)");
        activeButton.onClick = [this] { ctx.session.updateTrack (trackId, [this] (Track& t) { for (auto& l : t.automation) if (l.id == laneId) l.active = ! l.active; }); };
        hideButton.onClick = [this] { ctx.session.updateTrack (trackId, [this] (Track& t) { for (auto& l : t.automation) if (l.id == laneId) l.visible = false; }, false); };
    }

    const AutomationLane* lane (const Track** owner = nullptr) const
    {
        auto* t = ctx.session.project().findTrack (trackId);
        if (owner != nullptr) *owner = t;
        if (t == nullptr) return nullptr;
        for (auto& l : t->automation) if (l.id == laneId) return &l;
        return nullptr;
    }

    void update()
    {
        if (auto* l = lane()) activeButton.setToggleState (l->active, juce::dontSendNotification);
        repaint();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 8).withTrimmedLeft (14);
        auto top = r.removeFromTop (20);
        hideButton.setBounds (top.removeFromRight (20));
        top.removeFromRight (4);
        activeButton.setBounds (top.removeFromRight (20));
    }

    void paint (juce::Graphics& g) override
    {
        const Track* t = nullptr;
        auto* l = lane (&t);
        if (l == nullptr || t == nullptr) return;
        g.fillAll (colours::background.brighter (0.03f));
        g.setColour (colours::stroke);
        g.fillRect (0, getHeight() - 1, getWidth(), 1);
        const auto colour = colours::named (t->colour);
        g.setColour (colour.withAlpha (0.5f));
        g.fillRect (20, 10, 2, getHeight() - 20);
        auto r = getLocalBounds().reduced (10, 8).withTrimmedLeft (18);
        g.setColour (l->active && t->automationMode != AutomationMode::off ? colours::text : colours::text3);
        g.setFont (font (12, true));
        g.drawFittedText (edit::automation::laneName (ctx.session.project(), *t, *l), r.removeFromTop (20).withTrimmedRight (50),
                          juce::Justification::centredLeft, 1);
        g.setColour (l->writing ? colours::record : colours::text2);
        g.setFont (mono (10.5f));
        const auto value = ctx.session.effectiveValue (*t, l->target);
        auto text = l->target.formatValue (value);
        if (l->writing) text << "  " << juce::String::fromUTF8 ("● escribiendo");
        else if (t->automationMode == AutomationMode::off) text << "  (modo apagado)";
        g.drawText (text, r.removeFromTop (18), juce::Justification::centredLeft, true);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu()) return;
        juce::PopupMenu m;
        auto* l = lane();
        if (l == nullptr) return;
        m.addItem (l->active ? "Desactivar carril" : "Activar carril", [this] { activeButton.triggerClick(); });
        m.addItem ("Ocultar carril", [this] { hideButton.triggerClick(); });
        m.addItem ("Borrar todos los puntos", [this]
        {
            ctx.session.updateTrack (trackId, [this] (Track& t) { for (auto& x : t.automation) if (x.id == laneId) x.points.clear(); });
        });
        m.addItem ("Eliminar carril", [this]
        {
            ctx.session.updateTrack (trackId, [this] (Track& t)
            {
                t.automation.erase (std::remove_if (t.automation.begin(), t.automation.end(), [this] (auto& x) { return x.id == laneId; }), t.automation.end());
            });
        });
        m.showMenuAsync (juce::PopupMenu::Options());
    }

    Context& ctx;
    juce::Uuid trackId, laneId;
    PillButton activeButton { "on", colours::accent2 }, hideButton { juce::String::fromUTF8 ("×"), colours::text3 };
};

//==============================================================================
class HeadersContent : public juce::Component
{
public:
    explicit HeadersContent (Arrangement& a) : arr (a) {}

    void rebuild()
    {
        auto& proj = arr.ctx.session.project();
        // Una cabecera por fila (pista o carril), en el mismo orden que la línea de tiempo.
        std::vector<std::pair<juce::Uuid, juce::Uuid>> keys;
        for (auto& r : arr.rows)
        {
            auto& t = proj.tracks[(size_t) r.track];
            keys.push_back ({ t.id, r.lane >= 0 ? t.automation[(size_t) r.lane].id : juce::Uuid::null() });
        }
        if (keys != currentKeys)
        {
            headers.clear();
            lanes.clear();
            for (auto& [track, lane] : keys)
            {
                if (lane.isNull()) addAndMakeVisible (headers.add (new TrackHeader (arr.ctx, track)));
                else addAndMakeVisible (lanes.add (new LaneHeader (arr.ctx, track, lane)));
            }
            currentKeys = keys;
        }
        for (auto* h : headers) h->update();
        for (auto* l : lanes) l->update();
        setSize (Arrangement::headerWidth, juce::jmax (1, arr.contentHeight() + 160));
        resized();
    }

    void resized() override
    {
        int h = 0, l = 0;
        for (auto& r : arr.rows)
        {
            if (r.lane < 0 && h < headers.size()) headers[h++]->setBounds (0, r.y, getWidth(), r.h);
            else if (r.lane >= 0 && l < lanes.size()) lanes[l++]->setBounds (0, r.y, getWidth(), r.h);
        }
    }

    void paint (juce::Graphics& g) override { g.fillAll (colours::panel); }
    void repaintMeters() { for (auto* h : headers) h->repaint (h->getLocalBounds().removeFromBottom (14)); }
    void refreshValues()
    {
        for (auto* h : headers) h->refreshValues();
        for (auto* l : lanes) l->repaint();
    }

    Arrangement& arr;
    juce::OwnedArray<TrackHeader> headers;
    juce::OwnedArray<LaneHeader> lanes;
    std::vector<std::pair<juce::Uuid, juce::Uuid>> currentKeys;
};

//==============================================================================
/** Línea de tiempo con ítems. Un solo componente que pinta solo lo visible (rápido con muchas pistas). */
class TimelineContent : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    explicit TimelineContent (Arrangement& a) : arr (a) { setWantsKeyboardFocus (false); }

    Session& session() const { return arr.ctx.session; }
    double ppb() const { return session().pixelsPerBeat; }
    float xFor (Beats b) const { return (float) (b * ppb()); }
    Beats beatAt (float x) const { return juce::jmax (0.0, (double) x / ppb()); }
    /** Pista cuya fila principal está en `y` (-1 en un carril de automatización o fuera). */
    int rowAt (float y) const { auto* r = arr.rowAt (y); return r != nullptr && r->lane < 0 ? r->track : -1; }

    struct Hit { juce::Uuid clip; int track = -1; bool isMidi = false; enum Zone { body, left, right, fadeIn, fadeOut } zone = body; };

    juce::Rectangle<float> clipRect (int trackIndex, Beats start, Beats end) const
    {
        return { xFor (start), (float) (arr.trackY (trackIndex) + 4), juce::jmax (3.0f, xFor (end) - xFor (start)), (float) arr.trackHeight - 8 };
    }

    std::optional<Hit> hitTest (juce::Point<float> p) const
    {
        auto& proj = session().project();
        const auto row = rowAt (p.y);
        if (row < 0 || row >= (int) proj.tracks.size()) return {};
        auto& t = proj.tracks[(size_t) row];
        auto zone = [&] (juce::Rectangle<float> r, bool audio)
        {
            const auto edge = juce::jmin (7.0f, r.getWidth() / 4);
            if (audio && p.y < r.getY() + 12)
            {
                if (p.x < r.getX() + 14) return Hit::fadeIn;
                if (p.x > r.getRight() - 14) return Hit::fadeOut;
            }
            if (p.x < r.getX() + edge) return Hit::left;
            if (p.x > r.getRight() - edge) return Hit::right;
            return Hit::body;
        };
        for (auto it = t.clips.rbegin(); it != t.clips.rend(); ++it)
        {
            auto r = clipRect (row, it->start, proj.audioClipEnd (*it));
            if (r.contains (p)) return Hit { it->id, row, false, zone (r, true) };
        }
        for (auto it = t.midiClips.rbegin(); it != t.midiClips.rend(); ++it)
        {
            auto r = clipRect (row, it->start, it->end());
            if (r.contains (p)) return Hit { it->id, row, true, zone (r, false) };
        }
        return {};
    }

    //--------------------------------------------------------------------------
    void paint (juce::Graphics& g) override
    {
        auto& s = session();
        auto& proj = s.project();
        const auto tl = proj.timeline();
        const auto clip = g.getClipBounds();
        // Filas: pistas y carriles de automatización
        for (auto& r : arr.rows)
        {
            if (r.y > clip.getBottom() || r.y + r.h < clip.getY()) continue;
            if (r.lane >= 0) g.setColour (colours::background.brighter (0.02f));
            else g.setColour (r.track % 2 == 0 ? colours::lane : colours::laneAlt);
            g.fillRect (clip.getX(), r.y, clip.getWidth(), r.h);
            if (r.lane < 0 && s.isTrackSelected (proj.tracks[(size_t) r.track].id))
            {
                g.setColour (juce::Colour (0x06ffffff));
                g.fillRect (clip.getX(), r.y, clip.getWidth(), r.h);
            }
            g.setColour (colours::stroke);
            g.fillRect (clip.getX(), r.y + r.h - 1, clip.getWidth(), 1);
        }
        g.setColour (colours::background);
        g.fillRect (clip.getX(), arr.contentHeight(), clip.getWidth(), getHeight());

        // Secciones (tinte suave)
        for (auto& sec : proj.sections)
        {
            const auto x0 = xFor (sec.start), x1 = xFor (sec.end());
            if (x1 < (float) clip.getX() || x0 > (float) clip.getRight()) continue;
            const auto c = colours::named (defaultColourId (sec.kind));
            g.setColour (c.withAlpha (0.025f));
            g.fillRect (x0, 0.0f, x1 - x0, (float) getHeight());
            g.setColour (c.withAlpha (0.35f));
            g.fillRect (x0, 0.0f, 1.0f, (float) getHeight());
        }

        // Selección de tiempo / loop
        if (proj.loopEnd > proj.loopStart + 0.001)
        {
            g.setColour (colours::loop.withAlpha (proj.loopEnabled ? 0.08f : 0.05f));
            g.fillRect (xFor (proj.loopStart), 0.0f, xFor (proj.loopEnd) - xFor (proj.loopStart), (float) getHeight());
        }

        // Grilla: compases y tiempos (y subdivisiones si hay zoom)
        const auto first = juce::jmax (0.0, std::floor (beatAt ((float) clip.getX())));
        const auto last = beatAt ((float) clip.getRight()) + 1;
        const bool showBeats = ppb() >= 14;
        const bool showSub = ppb() >= 80 && s.gridDivision < 1;
        const auto step = showSub ? s.gridDivision : 1.0;
        for (auto b = first; b <= last; b += step)
        {
            const bool bar = std::fmod (b, (double) tl.beatsPerBar) < 1.0e-6;
            const bool beat = std::fmod (b, 1.0) < 1.0e-6;
            if (! bar && ! showBeats) continue;
            g.setColour (juce::Colours::white.withAlpha (bar ? 0.075f : beat ? 0.035f : 0.018f));
            g.fillRect (xFor (b), (float) clip.getY(), 1.0f, (float) clip.getHeight());
        }

        // Marcadores
        for (auto& m : proj.markers)
        {
            g.setColour (colours::marker.withAlpha (0.45f));
            g.fillRect (xFor (m.position), (float) clip.getY(), 1.0f, (float) clip.getHeight());
        }

        // Carpeta cerrada: resumen de los ítems de adentro
        for (auto& r : arr.rows)
        {
            auto& folder = proj.tracks[(size_t) r.track];
            if (r.lane >= 0 || ! folder.isFolder || ! folder.folderCollapsed || r.y > clip.getBottom() || r.y + r.h < clip.getY()) continue;
            const auto [first, last] = edit::trackBlock (proj, folder.id);
            const int inner = juce::jmax (1, last - first);
            const auto lineH = juce::jlimit (2.0f, 8.0f, (float) (r.h - 16) / (float) inner);
            for (int k = first + 1; k <= last; ++k)
            {
                auto& child = proj.tracks[(size_t) k];
                const auto y = (float) r.y + 8 + (float) (k - first - 1) * lineH;
                g.setColour (colours::named (child.colour).withAlpha (0.55f));
                for (auto& c : child.clips)
                    g.fillRoundedRectangle (juce::Rectangle<float> (xFor (c.start), y, juce::jmax (2.0f, xFor (proj.audioClipEnd (c)) - xFor (c.start)), lineH - 1), 1.5f);
                for (auto& c : child.midiClips)
                    g.fillRoundedRectangle (juce::Rectangle<float> (xFor (c.start), y, juce::jmax (2.0f, xFor (c.end()) - xFor (c.start)), lineH - 1), 1.5f);
            }
        }

        // Ítems
        for (int i = 0; i < (int) proj.tracks.size(); ++i)
        {
            auto& t = proj.tracks[(size_t) i];
            if (proj.isInsideCollapsedFolder (t)) continue;
            const auto colour = colours::named (t.colour);
            for (auto& c : t.clips)
            {
                auto [start, end, row] = previewPlacement (c.id, c.start, proj.audioClipEnd (c), i);
                auto r = clipRect (row, start, end);
                if (! r.intersects (clip.toFloat())) continue;
                paintAudioClip (g, c, r, colour, s.isSelected (c.id));
            }
            for (auto& c : t.midiClips)
            {
                auto [start, end, row] = previewPlacement (c.id, c.start, c.end(), i);
                auto r = clipRect (row, start, end);
                if (! r.intersects (clip.toFloat())) continue;
                paintMidiClip (g, c, r, colour, s.isSelected (c.id));
            }
        }

        // Envolventes de automatización
        for (auto& r : arr.rows)
            if (r.lane >= 0 && r.y <= clip.getBottom() && r.y + r.h >= clip.getY())
                paintLane (g, r);

        // Selección por rectángulo
        if (marquee)
        {
            g.setColour (colours::accent2.withAlpha (0.12f));
            g.fillRect (*marquee);
            g.setColour (colours::accent2);
            g.drawRect (*marquee, 1.0f);
        }
        if (dropHighlight)
        {
            g.setColour (colours::accent.withAlpha (0.15f));
            g.fillRect (getLocalBounds());
        }
    }

    std::tuple<Beats, Beats, int> previewPlacement (const juce::Uuid& id, Beats start, Beats end, int row) const
    {
        if (drag && std::find (drag->ids.begin(), drag->ids.end(), id) != drag->ids.end())
        {
            switch (drag->mode)
            {
                case Drag::move:  return { start + drag->deltaBeats, end + drag->deltaBeats, movedRow (row, drag->deltaRows) };
                case Drag::left:  return { juce::jmin (end - 0.05, start + drag->deltaBeats), end, row };
                case Drag::right: return { start, juce::jmax (start + 0.05, end + drag->deltaBeats), row };
                default: break;
            }
        }
        return { start, end, row };
    }

    void paintAudioClip (juce::Graphics& g, const AudioClip& c, juce::Rectangle<float> r, juce::Colour colour, bool selected)
    {
        g.setGradientFill (juce::ColourGradient (colour.withAlpha (selected ? 0.42f : 0.26f), r.getTopLeft(),
                                                 colour.withAlpha (selected ? 0.28f : 0.14f), r.getBottomLeft(), false));
        g.fillRoundedRectangle (r, 7);
        // Forma de onda (solo la parte visible)
        const auto file = io::audioFolder (session().folder).getChildFile (c.file);
        if (auto* thumb = arr.ctx.thumbnails.get (file); thumb != nullptr && thumb->getTotalLength() > 0)
        {
            const auto visible = r.getIntersection (g.getClipBounds().toFloat()).withTrimmedTop (0);
            if (! visible.isEmpty())
            {
                const auto secPerPx = session().timeline().secondsPerBeat() / ppb();
                const auto t0 = c.offset + (visible.getX() - r.getX()) * secPerPx;
                const auto t1 = t0 + visible.getWidth() * secPerPx;
                g.setColour (colour.withAlpha (0.95f));
                auto wave = juce::Rectangle<float> (visible.getX(), r.getY() + 16, visible.getWidth(), r.getHeight() - 20);
                thumb->drawChannels (g, wave.toNearestInt(), t0, t1, juce::Decibels::decibelsToGain (c.gainDb));
            }
        }
        // Fundidos
        const auto secToPx = (float) (ppb() / session().timeline().secondsPerBeat());
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        if (c.fadeIn > 0.006) g.drawLine (r.getX(), r.getBottom(), r.getX() + (float) c.fadeIn * secToPx, r.getY() + 2, 1.2f);
        if (c.fadeOut > 0.006) g.drawLine (r.getRight() - (float) c.fadeOut * secToPx, r.getY() + 2, r.getRight(), r.getBottom(), 1.2f);
        g.setColour (juce::Colours::white.withAlpha (0.92f));
        g.setFont (font (11, true));
        auto label = r.reduced (7, 3).removeFromTop (12);
        label.setX (juce::jmax (label.getX(), (float) g.getClipBounds().getX() + 4));
        auto name = c.name;
        if (c.gainDb != 0) name << "  " << formatDb (c.gainDb) << " dB";
        g.drawText (name, label, juce::Justification::centredLeft, true);
        g.setColour (selected ? juce::Colours::white.withAlpha (0.9f) : colour.withAlpha (0.55f));
        g.drawRoundedRectangle (r.reduced (0.5f), 7, selected ? 1.5f : 1.0f);
    }

    int movedRow (int row, int delta) const { return juce::jlimit (0, (int) session().project().tracks.size() - 1, row + delta); }

    //--------------------------------------------------------------------------
    // Automatización
    juce::Rectangle<float> laneArea (const Arrangement::Row& r) const
    {
        return { 0.0f, (float) r.y + 6, (float) getWidth(), (float) r.h - 12 };
    }
    float yForValue (const AutomationTarget& t, double v, juce::Rectangle<float> area) const
    {
        return area.getBottom() - (float) t.toNormalised (v) * area.getHeight();
    }
    double valueForY (const AutomationTarget& t, float y, juce::Rectangle<float> area) const
    {
        return t.fromNormalised ((area.getBottom() - y) / area.getHeight());
    }
    const AutomationLane* laneOf (const Arrangement::Row& r, const Track** owner = nullptr) const
    {
        auto& proj = session().project();
        if (r.track < 0 || r.track >= (int) proj.tracks.size()) return nullptr;
        auto& t = proj.tracks[(size_t) r.track];
        if (owner != nullptr) *owner = &t;
        return r.lane >= 0 && r.lane < (int) t.automation.size() ? &t.automation[(size_t) r.lane] : nullptr;
    }

    void paintLane (juce::Graphics& g, const Arrangement::Row& r)
    {
        const Track* t = nullptr;
        auto* lane = laneOf (r, &t);
        if (lane == nullptr) return;
        const auto area = laneArea (r);
        const auto clip = g.getClipBounds().toFloat();
        const bool live = lane->active && t->automationMode != AutomationMode::off;
        const auto colour = lane->writing ? colours::record : colours::named (t->colour);

        // Línea de referencia (valor por defecto: 0 dB / centro)
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.fillRect (clip.getX(), yForValue (lane->target, lane->target.defaultValue(), area), clip.getWidth(), 1.0f);

        // Curva: se muestrea por píxeles (el volumen se interpola en dB y se dibuja en escala de fader)
        juce::Path path, fill;
        const float x0 = juce::jmax (0.0f, clip.getX() - 2), x1 = clip.getRight() + 2;
        bool firstPoint = true;
        for (float x = x0; x <= x1; x += 3.0f)
        {
            const auto y = yForValue (lane->target, lane->valueAt (beatAt (x)), area);
            if (firstPoint) { path.startNewSubPath (x, y); fill.startNewSubPath (x, area.getBottom()); fill.lineTo (x, y); firstPoint = false; }
            else { path.lineTo (x, y); fill.lineTo (x, y); }
        }
        fill.lineTo (x1, area.getBottom());
        fill.closeSubPath();
        g.setColour (colour.withAlpha (live ? 0.10f : 0.04f));
        g.fillPath (fill);
        g.setColour (colour.withAlpha (live ? 0.95f : 0.35f));
        g.strokePath (path, juce::PathStrokeType (live ? 1.6f : 1.0f));

        for (size_t i = 0; i < lane->points.size(); ++i)
        {
            const auto& p = lane->points[i];
            const auto x = xFor (p.time);
            if (x < clip.getX() - 6 || x > clip.getRight() + 6) continue;
            const auto y = yForValue (lane->target, p.value, area);
            const bool hot = laneDrag && laneDrag->laneId == lane->id && laneDrag->point == (int) i;
            g.setColour (colours::background);
            g.fillEllipse (x - 4, y - 4, 8, 8);
            g.setColour (hot ? juce::Colours::white : colour.withAlpha (live ? 1.0f : 0.5f));
            g.drawEllipse (x - 3.5f, y - 3.5f, 7, 7, 1.6f);
            if (hot)
            {
                g.setFont (mono (10));
                g.drawText (lane->target.formatValue (p.value), juce::Rectangle<float> (x + 7, y - 16, 90, 14), juce::Justification::centredLeft);
            }
        }
    }

    struct LaneDrag { juce::Uuid trackId, laneId; int point = -1; bool moved = false; bool tension = false; double startTension = 0; };
    std::optional<LaneDrag> laneDrag;

    int pointNear (const AutomationLane& lane, const juce::Rectangle<float>& area, juce::Point<float> p) const
    {
        for (int i = (int) lane.points.size() - 1; i >= 0; --i)
        {
            const auto& pt = lane.points[(size_t) i];
            if (juce::Point<float> (xFor (pt.time), yForValue (lane.target, pt.value, area)).getDistanceFrom (p) <= 7.0f) return i;
        }
        return -1;
    }

    bool laneMouseDown (const juce::MouseEvent& e)
    {
        auto* row = arr.rowAt (e.position.y);
        if (row == nullptr || row->lane < 0) return false;
        const Track* t = nullptr;
        auto* lane = laneOf (*row, &t);
        if (lane == nullptr) return true;
        auto& s = session();
        s.selectTrack (t->id);
        const auto area = laneArea (*row);
        const auto near = pointNear (*lane, area, e.position);
        const auto trackId = t->id;
        const auto laneId = lane->id;
        auto withLane = [&s, trackId, laneId] (const std::function<void (AutomationLane&)>& fn, bool undoable)
        {
            s.updateTrack (trackId, [&] (Track& x) { for (auto& l : x.automation) if (l.id == laneId) fn (l); }, undoable);
        };
        // Tramo bajo el mouse (para la forma de la curva)
        int segment = -1;
        {
            const auto beat = beatAt (e.position.x);
            for (int i = 0; i + 1 < (int) lane->points.size(); ++i)
                if (lane->points[(size_t) i].time <= beat && beat < lane->points[(size_t) i + 1].time) segment = i;
        }
        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu m;
            const int shapePoint = near >= 0 ? near : segment;
            if (shapePoint >= 0 && shapePoint + 1 < (int) lane->points.size())
            {
                juce::PopupMenu shapes;
                const auto current = lane->points[(size_t) shapePoint].shape;
                for (auto sh : { CurveShape::linear, CurveShape::slowStartEnd, CurveShape::fastStart, CurveShape::fastEnd, CurveShape::bezier, CurveShape::square })
                    shapes.addItem (displayName (sh), true, current == sh, [withLane, shapePoint, sh]
                    {
                        withLane ([shapePoint, sh] (AutomationLane& l)
                        {
                            if (shapePoint < (int) l.points.size())
                            {
                                auto& pt = l.points[(size_t) shapePoint];
                                pt.shape = sh;
                                if (sh == CurveShape::bezier && std::abs (pt.tension) < 0.05) pt.tension = 0.5;
                            }
                        }, true);
                    });
                shapes.addSeparator();
                shapes.addItem ("Aplicar a todo el carril", [withLane, current]
                {
                    withLane ([current] (AutomationLane& l) { for (auto& pt : l.points) pt.shape = current; }, true);
                });
                m.addSubMenu ("Forma del tramo", shapes);
                m.addSeparator();
            }
            if (near >= 0)
            {
                m.addItem ("Eliminar punto", [withLane, near] { withLane ([near] (AutomationLane& l) { if (near < (int) l.points.size()) l.points.erase (l.points.begin() + near); }, true); });
                auto value = lane->points[(size_t) near].value;
                auto target = lane->target;
                if (target.kind == AutomationTarget::Kind::volume || target.kind == AutomationTarget::Kind::sendLevel)
                    m.addItem ("Escribir valor (dB)…"_u, [this, withLane, near, value]
                    {
                        if (! arr.ctx.askText) return;
                        arr.ctx.askText ("Valor del punto (dB)", juce::String (value, 1), [withLane, near] (juce::String text)
                        {
                            const auto v = text.retainCharacters ("-+0123456789.,").replaceCharacter (',', '.').getDoubleValue();
                            withLane ([near, v] (AutomationLane& l) { if (near < (int) l.points.size()) l.points[(size_t) near].value = juce::jlimit (l.target.minValue(), l.target.maxValue(), v); }, true);
                        });
                    });
            }
            const auto at = s.snap (beatAt (e.position.x));
            const auto v = valueForY (lane->target, e.position.y, area);
            m.addItem (juce::String::fromUTF8 ("Agregar punto aquí"), [withLane, at, v] { withLane ([at, v] (AutomationLane& l) { edit::automation::addPoint (l, at, v); }, true); });
            if (s.project().loopEnd > s.project().loopStart + 0.001)
            {
                const auto a = s.project().loopStart, b = s.project().loopEnd;
                m.addItem (juce::String::fromUTF8 ("Borrar puntos en la selección de tiempo"), [withLane, a, b] { withLane ([a, b] (AutomationLane& l) { edit::automation::removePoints (l, a, b); }, true); });
            }
            m.addItem ("Borrar todos los puntos", [withLane] { withLane ([] (AutomationLane& l) { l.points.clear(); }, true); });
            m.showMenuAsync (juce::PopupMenu::Options());
            return true;
        }
        if (e.mods.isAltDown() && near >= 0)   // Alt+clic borra el punto (como REAPER)
        {
            withLane ([near] (AutomationLane& l) { if (near < (int) l.points.size()) l.points.erase (l.points.begin() + near); }, true);
            return true;
        }
        // Ctrl/Cmd + arrastrar un tramo: curvarlo (tensión), como Alt+arrastrar en REAPER
        if ((e.mods.isCommandDown() || e.mods.isCtrlDown()) && near < 0 && segment >= 0)
        {
            s.beginGesture();
            laneDrag = LaneDrag { trackId, laneId, segment, false, true,
                                  lane->points[(size_t) segment].shape == CurveShape::bezier ? lane->points[(size_t) segment].tension : 0.0 };
            return true;
        }
        s.beginGesture();
        int point = near;
        if (point < 0)
        {
            // Clic en el carril: punto nuevo sobre la curva (o donde se hizo clic si está lejos)
            const auto at = s.snap (beatAt (e.position.x));
            const auto onCurve = std::abs (yForValue (lane->target, lane->valueAt (at), area) - e.position.y) < 10.0f;
            const auto v = onCurve ? lane->valueAt (at) : valueForY (lane->target, e.position.y, area);
            withLane ([&point, at, v] (AutomationLane& l) { point = edit::automation::addPoint (l, at, v); }, false);
        }
        laneDrag = LaneDrag { trackId, laneId, point, false };
        repaint();
        return true;
    }

    void laneMouseDrag (const juce::MouseEvent& e)
    {
        auto& s = session();
        auto* t = s.project().findTrack (laneDrag->trackId);
        if (t == nullptr) return;
        int rowIndex = -1;
        for (int i = 0; i < (int) arr.rows.size(); ++i)
        {
            auto& r = arr.rows[(size_t) i];
            if (r.lane >= 0 && r.track == s.project().trackIndex (t->id) && t->automation[(size_t) r.lane].id == laneDrag->laneId) rowIndex = i;
        }
        if (rowIndex < 0) return;
        const auto area = laneArea (arr.rows[(size_t) rowIndex]);
        const bool fine = e.mods.isShiftDown();   // Shift: solo vertical
        auto& d = *laneDrag;
        if (d.tension)
        {
            // Hacia arriba: el tramo sube rápido al principio; hacia abajo: lento al principio
            const auto tension = juce::jlimit (-1.0, 1.0, d.startTension - e.getDistanceFromDragStartY() / 80.0);
            s.updateTrack (d.trackId, [&] (Track& x)
            {
                for (auto& l : x.automation)
                    if (l.id == d.laneId && d.point >= 0 && d.point < (int) l.points.size())
                    {
                        auto& p = l.points[(size_t) d.point];
                        // Si el tramo baja, se invierte para que "arriba" siempre curve hacia arriba
                        const bool falling = d.point + 1 < (int) l.points.size() && l.points[(size_t) d.point + 1].value < p.value;
                        p.shape = CurveShape::bezier;
                        p.tension = falling ? -tension : tension;
                    }
            }, false);
            d.moved = true;
            return;
        }
        const auto beat = s.snap (beatAt (e.position.x));
        const auto y = juce::jlimit (area.getY(), area.getBottom(), e.position.y);
        s.updateTrack (d.trackId, [&] (Track& x)
        {
            for (auto& l : x.automation)
                if (l.id == d.laneId && d.point >= 0 && d.point < (int) l.points.size())
                {
                    auto& p = l.points[(size_t) d.point];
                    p.value = valueForY (l.target, y, area);
                    if (! fine)
                    {
                        // No puede pasar a los vecinos (el orden de los puntos se mantiene)
                        const auto lo = d.point > 0 ? l.points[(size_t) d.point - 1].time : 0.0;
                        const auto hi = d.point + 1 < (int) l.points.size() ? l.points[(size_t) d.point + 1].time : 1.0e9;
                        p.time = juce::jlimit (lo, hi, beat);
                    }
                }
        }, false);
        d.moved = true;
    }

    void paintMidiClip (juce::Graphics& g, const MidiClip& c, juce::Rectangle<float> r, juce::Colour colour, bool selected)
    {
        g.setColour (colour.withAlpha (selected ? 0.34f : 0.18f));
        g.fillRoundedRectangle (r, 7);
        const auto [lo, hi] = c.pitchRange();
        const auto rows = (float) (hi - lo + 1);
        const auto rowH = juce::jmax (1.5f, (r.getHeight() - 20) / rows);
        for (auto& n : c.notes)
        {
            auto nr = juce::Rectangle<float> (r.getX() + (float) (n.start * ppb()), r.getY() + 16 + (float) (hi - n.pitch) * rowH,
                                              juce::jmax (2.0f, (float) (n.length * ppb())), juce::jmax (1.5f, rowH - 0.5f));
            nr = nr.getIntersection (r);
            if (nr.isEmpty()) continue;
            g.setColour (colour.withAlpha (0.4f + 0.6f * (float) n.velocity / 127.0f));
            g.fillRoundedRectangle (nr, 1);
        }
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.setFont (font (11, true));
        g.drawText (c.name, r.reduced (7, 3).removeFromTop (12), juce::Justification::centredLeft, true);
        g.setColour (selected ? juce::Colours::white.withAlpha (0.9f) : colour.withAlpha (0.55f));
        g.drawRoundedRectangle (r.reduced (0.5f), 7, selected ? 1.5f : 1.0f);
    }

    //--------------------------------------------------------------------------
    struct Drag
    {
        enum Mode { none, move, left, right, fadeIn, fadeOut, marquee } mode = none;
        std::vector<juce::Uuid> ids;
        Beats deltaBeats = 0;
        int deltaRows = 0;
        Beats anchorStart = 0, anchorEnd = 0;
        bool started = false;
    };
    std::optional<Drag> drag;
    std::optional<juce::Rectangle<float>> marquee;
    bool dropHighlight = false;

    void mouseMove (const juce::MouseEvent& e) override
    {
        if (auto* row = arr.rowAt (e.position.y); row != nullptr && row->lane >= 0)
        {
            const auto* lane = laneOf (*row);
            const bool onPoint = lane != nullptr && pointNear (*lane, laneArea (*row), e.position) >= 0;
            setMouseCursor (onPoint ? juce::MouseCursor::DraggingHandCursor
                                    : e.mods.isCommandDown() || e.mods.isCtrlDown() ? juce::MouseCursor::UpDownResizeCursor
                                                                                    : juce::MouseCursor::CrosshairCursor);
            return;
        }
        auto hit = hitTest (e.position);
        if (! hit) { setMouseCursor (juce::MouseCursor::NormalCursor); return; }
        switch (hit->zone)
        {
            case Hit::left: case Hit::right: setMouseCursor (juce::MouseCursor::LeftRightResizeCursor); break;
            case Hit::fadeIn: case Hit::fadeOut: setMouseCursor (juce::MouseCursor::UpDownLeftRightResizeCursor); break;
            default: setMouseCursor (juce::MouseCursor::NormalCursor);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (laneMouseDown (e)) return;
        auto& s = session();
        auto hit = hitTest (e.position);
        const bool add = e.mods.isShiftDown() || e.mods.isCommandDown();
        if (hit)
        {
            if (! s.isSelected (hit->clip) || add) s.selectClip (hit->clip, add);
            if (e.mods.isPopupMenu()) { showClipMenu (*hit); return; }
            Drag d;
            d.ids = hit->zone == Hit::body ? s.selectedClips : std::vector<juce::Uuid> { hit->clip };
            d.mode = hit->zone == Hit::left ? Drag::left : hit->zone == Hit::right ? Drag::right
                   : hit->zone == Hit::fadeIn ? Drag::fadeIn : hit->zone == Hit::fadeOut ? Drag::fadeOut : Drag::move;
            auto& proj = s.project();
            if (auto ref = edit::findClip (proj, hit->clip))
            {
                auto& t = proj.tracks[(size_t) ref.track];
                d.anchorStart = ref.isMidi ? t.midiClips[(size_t) ref.clip].start : t.clips[(size_t) ref.clip].start;
                d.anchorEnd = ref.isMidi ? t.midiClips[(size_t) ref.clip].end() : proj.audioClipEnd (t.clips[(size_t) ref.clip]);
            }
            drag = d;
            return;
        }
        // Zona vacía
        const auto row = rowAt (e.position.y);
        if (row >= 0 && row < (int) s.project().tracks.size()) s.selectTrack (s.project().tracks[(size_t) row].id);
        if (e.mods.isPopupMenu()) { showLaneMenu (e.position); return; }
        if (! add) s.clearClipSelection();
        s.seek (s.snap (beatAt (e.position.x)));
        drag = Drag { Drag::marquee };
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (laneDrag) { laneMouseDrag (e); return; }
        if (! drag) return;
        auto& s = session();
        if (! drag->started && e.getDistanceFromDragStart() < 4) return;
        drag->started = true;
        const auto dx = (double) e.getDistanceFromDragStartX() / ppb();
        switch (drag->mode)
        {
            case Drag::move:
                drag->deltaBeats = s.snap (drag->anchorStart + dx) - drag->anchorStart;
            {
                const auto from = arr.trackIndexAt (e.mouseDownPosition.y), to = arr.trackIndexAt (e.position.y);
                if (from >= 0 && to >= 0) drag->deltaRows = to - from;
                else if (e.position.y > (float) arr.contentHeight()) drag->deltaRows = (int) session().project().tracks.size();
            }
                break;
            case Drag::left:  drag->deltaBeats = s.snap (drag->anchorStart + dx) - drag->anchorStart; break;
            case Drag::right: drag->deltaBeats = s.snap (drag->anchorEnd + dx) - drag->anchorEnd; break;
            case Drag::fadeIn: case Drag::fadeOut: drag->deltaBeats = dx; break;
            case Drag::marquee:
                marquee = juce::Rectangle<float> (e.mouseDownPosition, e.position);
                break;
            default: break;
        }
        repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (laneDrag) { laneDrag.reset(); repaint(); return; }
        if (! drag) return;
        auto d = *drag;
        drag.reset();
        auto& s = session();
        if (d.mode == Drag::marquee)
        {
            if (marquee)
            {
                std::vector<juce::Uuid> ids;
                auto& proj = s.project();
                for (int i = 0; i < (int) proj.tracks.size(); ++i)
                {
                    for (auto& c : proj.tracks[(size_t) i].clips)
                        if (clipRect (i, c.start, proj.audioClipEnd (c)).intersects (*marquee)) ids.push_back (c.id);
                    for (auto& c : proj.tracks[(size_t) i].midiClips)
                        if (clipRect (i, c.start, c.end()).intersects (*marquee)) ids.push_back (c.id);
                }
                if (e.mods.isShiftDown()) for (auto& id : s.selectedClips) ids.push_back (id);
                s.selectClips (ids);
            }
            marquee.reset();
            repaint();
            return;
        }
        if (! d.started) { repaint(); return; }
        const auto secPerBeat = s.timeline().secondsPerBeat();
        s.edit ([&] (Project& p)
        {
            for (auto& id : d.ids)
            {
                auto ref = edit::findClip (p, id);
                if (! ref) continue;
                auto& t = p.tracks[(size_t) ref.track];
                const auto start = ref.isMidi ? t.midiClips[(size_t) ref.clip].start : t.clips[(size_t) ref.clip].start;
                const auto end = ref.isMidi ? t.midiClips[(size_t) ref.clip].end() : p.audioClipEnd (t.clips[(size_t) ref.clip]);
                switch (d.mode)
                {
                    case Drag::move:
                    {
                        const auto row = juce::jlimit (0, (int) p.tracks.size() - 1, ref.track + d.deltaRows);
                        // Los ítems no van a un bus: se quedan en su pista.
                        const auto& dest = p.tracks[(size_t) row];
                        edit::moveClip (p, id, start + d.deltaBeats, dest.isBus() ? t.id : dest.id);
                        break;
                    }
                    case Drag::left:  edit::trimClipStart (p, id, start + d.deltaBeats); break;
                    case Drag::right: edit::trimClipEnd (p, id, end + d.deltaBeats); break;
                    case Drag::fadeIn:
                        if (! ref.isMidi) { auto& c = t.clips[(size_t) ref.clip]; c.fadeIn = juce::jlimit (0.0, c.length / 2, c.fadeIn + d.deltaBeats * secPerBeat); }
                        break;
                    case Drag::fadeOut:
                        if (! ref.isMidi) { auto& c = t.clips[(size_t) ref.clip]; c.fadeOut = juce::jlimit (0.0, c.length / 2, c.fadeOut - d.deltaBeats * secPerBeat); }
                        break;
                    default: break;
                }
            }
        });
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        auto& s = session();
        if (auto* row = arr.rowAt (e.position.y); row != nullptr && row->lane >= 0)
        {
            // Doble clic sobre un punto: borrarlo
            const Track* t = nullptr;
            if (auto* lane = laneOf (*row, &t))
                if (const auto near = pointNear (*lane, laneArea (*row), e.position); near >= 0)
                {
                    const auto laneId = lane->id;
                    s.updateTrack (t->id, [laneId, near] (Track& x)
                    {
                        for (auto& l : x.automation) if (l.id == laneId && near < (int) l.points.size()) l.points.erase (l.points.begin() + near);
                    });
                }
            return;
        }
        if (auto hit = hitTest (e.position))
        {
            if (hit->isMidi) { s.editingMidiClip = hit->clip; if (arr.ctx.openPianoRoll) arr.ctx.openPianoRoll(); }
            return;
        }
        const auto row = rowAt (e.position.y);
        if (row >= 0 && row < (int) s.project().tracks.size() && s.project().tracks[(size_t) row].isInstrument())
            s.createMidiClip (s.project().tracks[(size_t) row].id, beatAt (e.position.x));
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (e.mods.isCommandDown() || e.mods.isCtrlDown())
            arr.zoom (w.deltaY > 0 ? 1.15 : 1.0 / 1.15, e.getEventRelativeTo (&arr.timelineView).getPosition().x);
        else
            Component::mouseWheelMove (e, w);
    }

    void showClipMenu (const Hit& hit)
    {
        auto& s = session();
        juce::PopupMenu m;
        if (hit.isMidi)
            m.addItem ("Abrir en el editor MIDI", [this, id = hit.clip] { session().editingMidiClip = id; if (arr.ctx.openPianoRoll) arr.ctx.openPianoRoll(); });
        m.addItem ("Dividir en el cursor\tS", [&s] { s.splitAtCursor(); });
        m.addItem ("Copiar", [&s] { s.copySelection(); });
        m.addItem ("Cortar", [&s] { s.cutSelection(); });
        m.addItem ("Duplicar", [&s] { s.duplicateSelection(); });
        if (! hit.isMidi)
        {
            juce::PopupMenu gain, fades;
            for (auto db : { 6.0, 3.0, 1.0, -1.0, -3.0, -6.0 })
                gain.addItem ((db > 0 ? "+" : "") + juce::String (db, 0) + " dB", [&s, db, ids = s.selectedClips]
                {
                    s.edit ([&] (Project& p) { for (auto& id : ids) if (auto r = edit::findClip (p, id); r && ! r.isMidi)
                        { auto& c = p.tracks[(size_t) r.track].clips[(size_t) r.clip]; c.gainDb = (float) juce::jlimit (-60.0, 24.0, c.gainDb + db); } });
                });
            gain.addItem ("Restablecer (0 dB)", [&s, ids = s.selectedClips]
            {
                s.edit ([&] (Project& p) { for (auto& id : ids) if (auto r = edit::findClip (p, id); r && ! r.isMidi) p.tracks[(size_t) r.track].clips[(size_t) r.clip].gainDb = 0; });
            });
            for (auto ms : { 5, 50, 250, 1000 })
                fades.addItem ("Entrada y salida: " + juce::String (ms) + " ms", [&s, ms, ids = s.selectedClips]
                {
                    s.edit ([&] (Project& p) { for (auto& id : ids) if (auto r = edit::findClip (p, id); r && ! r.isMidi)
                        { auto& c = p.tracks[(size_t) r.track].clips[(size_t) r.clip]; c.fadeIn = c.fadeOut = juce::jmin (ms / 1000.0, c.length / 2); } });
                });
            m.addSubMenu ("Ganancia del ítem"_u, gain);
            m.addSubMenu ("Fundidos", fades);
        }
        m.addSeparator();
        m.addItem ("Eliminar", [&s] { s.deleteSelection(); });
        m.showMenuAsync (juce::PopupMenu::Options());
    }

    void showLaneMenu (juce::Point<float> p)
    {
        auto& s = session();
        const auto row = rowAt (p.y);
        juce::PopupMenu m;
        const auto at = s.snap (beatAt (p.x));
        if (row >= 0 && row < (int) s.project().tracks.size() && s.project().tracks[(size_t) row].isInstrument())
            m.addItem (juce::String::fromUTF8 ("Nuevo ítem MIDI aquí"), [&s, id = s.project().tracks[(size_t) row].id, at] { s.createMidiClip (id, at); });
        m.addItem ("Pegar", [&s, at] { s.editCursor = at; s.paste(); });
        m.addItem ("Insertar marcador", [&s, at] { s.seek (at); s.insertMarker(); });
        m.showMenuAsync (juce::PopupMenu::Options());
    }

    // Soltar archivos de audio desde el Finder / Explorador
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void fileDragEnter (const juce::StringArray&, int, int) override { dropHighlight = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dropHighlight = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int x, int y) override
    {
        dropHighlight = false;
        auto& s = session();
        juce::Array<juce::File> list;
        for (auto& f : files) list.add (juce::File (f));
        const auto row = arr.trackIndexAt ((float) y);
        std::optional<juce::Uuid> track;
        if (row >= 0 && row < (int) s.project().tracks.size() && ! s.project().tracks[(size_t) row].isBus()) track = s.project().tracks[(size_t) row].id;
        s.importFiles (list, track, s.snap (beatAt ((float) x)));
        repaint();
    }

    Arrangement& arr;
};

//==============================================================================
/** Regla: secciones de la canción (arriba) y compases con marcadores (abajo).
    Clic = mover el cursor. Arrastrar = selección de tiempo (el loop la sigue), como en REAPER. */
class RulerBar : public juce::Component
{
public:
    explicit RulerBar (Arrangement& a) : arr (a) {}
    int scrollX = 0;

    double ppb() const { return arr.ctx.session.pixelsPerBeat; }
    float xFor (Beats b) const { return (float) (b * ppb()) - (float) scrollX; }
    Beats beatAt (float x) const { return juce::jmax (0.0, (double) (x + (float) scrollX) / ppb()); }

    void paint (juce::Graphics& g) override
    {
        auto& s = arr.ctx.session;
        auto& proj = s.project();
        const auto tl = proj.timeline();
        g.fillAll (colours::panel);
        const auto secBar = getLocalBounds().removeFromTop (24).toFloat();
        const auto ruler = getLocalBounds().withTrimmedTop (24).toFloat();
        const auto current = edit::sectionAt (proj, s.playhead());

        for (auto& sec : proj.sections)
        {
            auto r = juce::Rectangle<float> (xFor (sec.start), secBar.getY() + 3, (float) (sec.length * ppb()) - 2, secBar.getHeight() - 6);
            if (r.getRight() < 0 || r.getX() > (float) getWidth()) continue;
            const auto c = colours::named (defaultColourId (sec.kind));
            const bool on = current != nullptr && current->id == sec.id;
            g.setColour (c.withAlpha (on ? 0.42f : 0.2f));
            g.fillRoundedRectangle (r, 5);
            g.setColour (c.withAlpha (on ? 0.95f : 0.45f));
            g.drawRoundedRectangle (r.reduced (0.5f), 5, 1);
            g.setColour (on ? juce::Colours::white : c);
            g.setFont (font (11.5f, true));
            g.drawText (r.getWidth() > 60 ? sec.name : shortName (sec.kind), r.reduced (7, 0), juce::Justification::centredLeft, true);
        }

        if (proj.loopEnd > proj.loopStart + 0.001)
        {
            g.setColour (colours::loop.withAlpha (proj.loopEnabled ? 0.45f : 0.18f));
            g.fillRoundedRectangle (juce::Rectangle<float> (xFor (proj.loopStart), ruler.getY() + 2, xFor (proj.loopEnd) - xFor (proj.loopStart), 5), 2.5f);
        }

        const auto barPx = tl.beatsPerBar * ppb();
        int every = 1;
        while (every * barPx < 46) every *= 2;
        const auto firstBar = juce::jmax (0, (int) std::floor (beatAt (0) / tl.beatsPerBar));
        const auto lastBar = (int) std::ceil (beatAt ((float) getWidth()) / tl.beatsPerBar);
        for (int bar = firstBar; bar <= lastBar; ++bar)
        {
            const auto x = xFor (bar * (double) tl.beatsPerBar);
            const bool labeled = bar % every == 0;
            g.setColour (juce::Colours::white.withAlpha (labeled ? 0.35f : 0.15f));
            g.fillRect (x, labeled ? ruler.getY() + 9 : ruler.getBottom() - 8, 1.0f, labeled ? ruler.getHeight() - 9 : 8.0f);
            if (labeled)
            {
                g.setColour (colours::text2);
                g.setFont (font (11, true));
                g.drawText (juce::String (bar + 1), juce::Rectangle<float> (x + 4, ruler.getY() + 8, 40, 14), juce::Justification::centredLeft);
            }
        }

        for (auto& m : proj.markers)
        {
            auto label = m.name.isEmpty() ? juce::String (m.number) : juce::String (m.number) + " " + m.name;
            const auto w = (float) juce::GlyphArrangement::getStringWidthInt (font (10, true), label) + 8;
            auto r = juce::Rectangle<float> (xFor (m.position), ruler.getY() + 1, w, 13);
            g.setColour (colours::marker);
            g.fillRoundedRectangle (r, 3);
            g.setColour (juce::Colours::white);
            g.setFont (font (10, true));
            g.drawText (label, r, juce::Justification::centred);
        }

        // Cursor de reproducción (triángulo)
        const auto px = xFor (s.playhead());
        juce::Path tri;
        tri.addTriangle (px - 6, ruler.getBottom() - 10, px + 6, ruler.getBottom() - 10, px, ruler.getBottom());
        g.setColour (juce::Colours::white);
        g.fillPath (tri);
        g.setColour (colours::stroke);
        g.fillRect (0, getHeight() - 1, getWidth(), 1);
    }

    const Section* sectionAt (juce::Point<float> p) const
    {
        if (p.y > 24) return nullptr;
        for (auto& sec : arr.ctx.session.project().sections)
            if (p.x >= xFor (sec.start) && p.x < xFor (sec.end())) return &sec;
        return nullptr;
    }

    const Marker* markerAt (juce::Point<float> p) const
    {
        if (p.y < 24 || p.y > 40) return nullptr;
        for (auto& m : arr.ctx.session.project().markers)
            if (p.x >= xFor (m.position) && p.x < xFor (m.position) + 40) return &m;
        return nullptr;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        auto& s = arr.ctx.session;
        dragging = false;
        if (auto* sec = sectionAt (e.position))
        {
            if (e.mods.isPopupMenu()) { showSectionMenu (*sec); return; }
            s.seek (sec->start);
            return;
        }
        if (auto* m = markerAt (e.position))
        {
            if (e.mods.isPopupMenu()) { showMarkerMenu (*m); return; }
            s.seek (m->position);
            return;
        }
        if (e.mods.isPopupMenu()) { showRulerMenu(); return; }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.mouseDownPosition.y <= 24 || e.getDistanceFromDragStartX() == 0) return;
        if (std::abs (e.getDistanceFromDragStartX()) < 4 && ! dragging) return;
        dragging = true;
        auto& s = arr.ctx.session;
        s.setTimeSelection (s.snap (beatAt (e.mouseDownPosition.x)), s.snap (beatAt (e.position.x)));
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! dragging && e.mouseDownPosition.y > 24 && ! e.mods.isPopupMenu() && markerAt (e.position) == nullptr)
            arr.ctx.session.seek (arr.ctx.session.snap (beatAt (e.position.x)));
        dragging = false;
    }

    void showSectionMenu (const Section& sec)
    {
        auto& s = arr.ctx.session;
        const auto id = sec.id;
        juce::PopupMenu m, kinds, lengths;
        m.addItem (juce::String::fromUTF8 ("Repetir esta sección"), [&s, id] { s.loopSection (id); });
        m.addItem ("Renombrar…"_u, [this, id, name = sec.name]
        {
            arr.ctx.askText (juce::String::fromUTF8 ("Nombre de la sección"), name, [this, id] (juce::String n)
            {
                arr.ctx.session.edit ([&] (Project& p) { for (auto& x : p.sections) if (x.id == id) x.name = n; });
            });
        });
        for (int k = 0; k <= (int) SectionKind::outro; ++k)
            kinds.addItem (displayName ((SectionKind) k), [&s, id, k]
            {
                s.edit ([&] (Project& p) { for (auto& x : p.sections) if (x.id == id) { x.kind = (SectionKind) k; x.name = displayName ((SectionKind) k); } });
            });
        for (int bars : { 2, 4, 8, 12, 16 })
            lengths.addItem (juce::String (bars) + " compases", [&s, id, bars]
            {
                s.edit ([&] (Project& p) { for (auto& x : p.sections) if (x.id == id) x.length = bars * p.beatsPerBar; });
            });
        m.addSubMenu ("Tipo", kinds);
        m.addSubMenu ("Largo", lengths);
        m.addSeparator();
        m.addItem ("Eliminar", [&s, id] { s.edit ([&] (Project& p) { p.sections.erase (std::remove_if (p.sections.begin(), p.sections.end(), [&] (auto& x) { return x.id == id; }), p.sections.end()); }); });
        m.showMenuAsync (juce::PopupMenu::Options());
    }

    void showMarkerMenu (const Marker& mk)
    {
        auto& s = arr.ctx.session;
        const auto id = mk.id;
        juce::PopupMenu m;
        m.addItem ("Renombrar…"_u, [this, id, name = mk.name]
        {
            arr.ctx.askText ("Nombre del marcador", name, [this, id] (juce::String n)
            {
                arr.ctx.session.edit ([&] (Project& p) { for (auto& x : p.markers) if (x.id == id) x.name = n; });
            });
        });
        m.addItem ("Eliminar marcador", [&s, id] { s.edit ([&] (Project& p) { edit::removeMarker (p, id); }); });
        m.showMenuAsync (juce::PopupMenu::Options());
    }

    void showRulerMenu()
    {
        auto& s = arr.ctx.session;
        juce::PopupMenu m, add;
        for (int k = 0; k <= (int) SectionKind::outro; ++k)
            add.addItem (displayName ((SectionKind) k), [&s, k]
            {
                s.edit ([&] (Project& p)
                {
                    int count = 0;
                    for (auto& x : p.sections) if (x.kind == (SectionKind) k) ++count;
                    auto& sec = edit::addSection (p, (SectionKind) k, (SectionKind) k == SectionKind::intro || (SectionKind) k == SectionKind::outro ? 4 : 8);
                    if (count > 0) sec.name = displayName ((SectionKind) k) + " " + juce::String (count + 1);
                });
            });
        m.addSubMenu (juce::String::fromUTF8 ("Agregar sección al final"), add);
        m.addItem ("Insertar marcador en el cursor\tM", [&s] { s.insertMarker(); });
        m.addItem (juce::String::fromUTF8 ("Quitar selección de tiempo\tEsc"), [&s] { s.clearTimeSelection(); });
        m.showMenuAsync (juce::PopupMenu::Options());
    }

    Arrangement& arr;
    bool dragging = false;
};

//==============================================================================
class Arrangement::Corner : public juce::Component
{
public:
    explicit Corner (Arrangement& a) : arr (a)
    {
        addTrack.setButtonText ("+ Pista");
        addTrack.onClick = [this]
        {
            juce::PopupMenu m;
            auto& s = arr.ctx.session;
            m.addItem ("Pista de audio\tCtrl/Cmd+T", [&s] { s.addTrack (TrackRole::audio); });
            m.addItem ("Pista de instrumento (MIDI)\tShift+Ctrl/Cmd+T", [&s] { s.addTrack (TrackRole::instrument); });
            m.addItem (juce::String::fromUTF8 ("Pista guía"), [&s] { s.addTrack (TrackRole::guide); });
            m.addItem ("Bus (submezcla o efectos)", [&s] { s.addTrack (TrackRole::bus); });
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addTrack));
        };
        addAndMakeVisible (addTrack);
        for (auto* b : { &snapButton, &followButton, &zoomOut, &zoomIn }) addAndMakeVisible (*b);
        snapButton.onClick = [this]
        {
            juce::PopupMenu m;
            auto& s = arr.ctx.session;
            m.addItem ("Ajustar a la grilla", true, s.snapEnabled, [&s] { s.snapEnabled = ! s.snapEnabled; s.sendChangeMessage(); });
            m.addSeparator();
            for (auto [label, div] : std::initializer_list<std::pair<const char*, double>> { { "Compás", 0 }, { "1/4 (negra)", 1 }, { "1/8", 0.5 }, { "1/16", 0.25 }, { "1/8 tresillo", 1.0 / 3 } })   // utf8-ok (fromUTF8 abajo)
            {
                const auto d = div == 0 ? (double) s.project().beatsPerBar : div;
                m.addItem (juce::String::fromUTF8 (label), true, std::abs (s.gridDivision - d) < 1e-6, [&s, d] { s.gridDivision = d; s.snapEnabled = true; s.sendChangeMessage(); });
            }
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&snapButton));
        };
        followButton.onClick = [this] { auto& s = arr.ctx.session; s.followPlayhead = ! s.followPlayhead; s.sendChangeMessage(); };
        zoomIn.onClick = [this] { arr.zoom (1.4); };
        zoomOut.onClick = [this] { arr.zoom (1 / 1.4); };
    }

    void update()
    {
        auto& s = arr.ctx.session;
        snapButton.active = s.snapEnabled;
        followButton.active = s.followPlayhead;
        repaint();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 12);
        addTrack.setBounds (r.removeFromLeft (74));
        for (auto* b : { &zoomIn, &zoomOut, &followButton, &snapButton })
        {
            b->setBounds (r.removeFromRight (26).withSizeKeepingCentre (26, 26));
            r.removeFromRight (4);
        }
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (colours::panel);
        g.setColour (colours::stroke);
        g.fillRect (getWidth() - 1, 0, 1, getHeight());
        g.fillRect (0, getHeight() - 1, getWidth(), 1);
    }

    Arrangement& arr;
    juce::TextButton addTrack;
    IconButton snapButton { IconButton::Icon::snap, "Grilla y ajuste" }, followButton { IconButton::Icon::follow, "Seguir el cursor" },
               zoomOut { IconButton::Icon::zoomOut, "Alejar (-)" }, zoomIn { IconButton::Icon::zoomIn, "Acercar (+)" };
};

//==============================================================================
void Arrangement::Playhead::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::transparentBlack);
    g.setColour (findColour (juce::Label::textColourId));
    g.fillRect (getLocalBounds().toFloat().withSizeKeepingCentre (1.5f, (float) getHeight()));
}

Arrangement::Arrangement (Context& c) : ctx (c)
{
    timeline = std::make_unique<TimelineContent> (*this);
    headers = std::make_unique<HeadersContent> (*this);
    ruler = std::make_unique<RulerBar> (*this);
    corner = std::make_unique<Corner> (*this);

    timelineView.setViewedComponent (timeline.get(), false);
    timelineView.setScrollBarsShown (true, true);
    timelineView.setScrollBarThickness (10);
    headersView.setViewedComponent (headers.get(), false);
    headersView.setScrollBarsShown (false, false);
    headersView.setInterceptsMouseClicks (true, true);
    addAndMakeVisible (timelineView);
    addAndMakeVisible (headersView);
    addAndMakeVisible (*ruler);
    addAndMakeVisible (*corner);

    for (auto* p : { &playheadLine, &editCursorLine })
    {
        p->setInterceptsMouseClicks (false, false);
        timeline->addAndMakeVisible (*p);
    }
    playheadLine.setColour (juce::Label::textColourId, juce::Colours::white);
    editCursorLine.setColour (juce::Label::textColourId, colours::text3);

    ctx.session.addChangeListener (this);
    ctx.thumbnails.addChangeListener (this);
    refresh();
    startTimerHz (30);
}

Arrangement::~Arrangement()
{
    ctx.session.removeChangeListener (this);
    ctx.thumbnails.removeChangeListener (this);
}

void Arrangement::paint (juce::Graphics& g) { g.fillAll (colours::background); }

void Arrangement::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (rulerHeight);
    corner->setBounds (top.removeFromLeft (headerWidth));
    ruler->setBounds (top);
    headersView.setBounds (r.removeFromLeft (headerWidth));
    timelineView.setBounds (r);
    refresh();
}

void Arrangement::computeLayout()
{
    rows.clear();
    int y = 0;
    auto& tracks = ctx.session.project().tracks;
    auto& proj = ctx.session.project();
    for (int i = 0; i < (int) tracks.size(); ++i)
    {
        if (proj.isInsideCollapsedFolder (tracks[(size_t) i])) continue;   // carpeta cerrada
        rows.push_back ({ i, -1, y, trackHeight });
        y += trackHeight;
        for (int l = 0; l < (int) tracks[(size_t) i].automation.size(); ++l)
            if (tracks[(size_t) i].automation[(size_t) l].visible)
            {
                rows.push_back ({ i, l, y, laneHeight });
                y += laneHeight;
            }
    }
}

int Arrangement::trackY (int track) const
{
    for (auto& r : rows) if (r.track == track && r.lane < 0) return r.y;
    return track * trackHeight;
}

const Arrangement::Row* Arrangement::rowAt (float y) const
{
    if (y < 0) return nullptr;
    auto it = std::upper_bound (rows.begin(), rows.end(), y, [] (float v, const Row& r) { return v < (float) r.y; });
    if (it == rows.begin()) return nullptr;
    --it;
    return y < (float) (it->y + it->h) ? &*it : nullptr;
}

void Arrangement::refresh()
{
    auto& s = ctx.session;
    computeLayout();
    const auto width = (int) (s.timelineLength() * s.pixelsPerBeat);
    const auto height = juce::jmax (timelineView.getHeight(), contentHeight() + 160);
    timeline->setSize (width, height);
    headers->rebuild();
    headers->setSize (headerWidth, height);
    corner->update();
    std::vector<juce::Uuid> ids;
    for (auto& t : s.project().tracks) ids.push_back (t.id);
    ctx.meters.setTracks (ids);
    timeline->repaint();
    ruler->repaint();
}

void Arrangement::zoom (double factor, std::optional<int> anchorX)
{
    auto& s = ctx.session;
    const auto anchor = anchorX.value_or (timelineView.getViewWidth() / 2);
    const auto beatAtAnchor = (timelineView.getViewPositionX() + anchor) / s.pixelsPerBeat;
    s.pixelsPerBeat = juce::jlimit (3.0, 800.0, s.pixelsPerBeat * factor);
    refresh();
    timelineView.setViewPosition ((int) (beatAtAnchor * s.pixelsPerBeat) - anchor, timelineView.getViewPositionY());
}

void Arrangement::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}

void Arrangement::syncScroll()
{
    const auto x = timelineView.getViewPositionX(), y = timelineView.getViewPositionY();
    if (y != lastScrollY) { headersView.setViewPosition (0, y); lastScrollY = y; }
    if (x != lastScrollX) { ruler->scrollX = x; ruler->repaint(); lastScrollX = x; }
}

void Arrangement::timerCallback()
{
    syncScroll();
    auto& s = ctx.session;
    // Solo se redibujan el cursor, la regla y los medidores (no toda la línea de tiempo).
    const auto x = (int) (s.playhead() * s.pixelsPerBeat);
    playheadLine.setBounds (x - 1, 0, 3, timeline->getHeight());
    const bool showEdit = s.isPlaying();
    editCursorLine.setVisible (showEdit);
    if (showEdit) editCursorLine.setBounds ((int) (s.editCursor * s.pixelsPerBeat) - 1, 0, 3, timeline->getHeight());
    if (s.isPlaying())
    {
        ruler->repaint();
        if (s.followPlayhead)
        {
            const auto vx = timelineView.getViewPositionX(), vw = timelineView.getViewWidth();
            if (x > vx + vw * 0.9 || x < vx) timelineView.setViewPosition (juce::jmax (0, x - vw / 7), timelineView.getViewPositionY());
        }
    }
    headers->repaintMeters();
    if (s.isPlaying() && ++valueTicks % 3 == 0) headers->refreshValues();
    // Mientras se escribe automatización, la curva crece bajo el cursor.
    if (s.isPlaying())
        for (auto& r : rows)
            if (r.lane >= 0)
            {
                auto& t = s.project().tracks[(size_t) r.track];
                if (t.automation[(size_t) r.lane].writing) timeline->repaint (juce::jmax (0, x - 200), r.y, 260, r.h);
            }
}

} // namespace wdaw::ui
