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
    menu.addSeparator();
    addRoutingMenu (menu, s, t);
}

juce::String outputLabel (const Project& p, const Track& t)
{
    if (t.directOutPair >= 2) return "Salidas " + juce::String (t.directOutPair * 2 - 1) + "/" + juce::String (t.directOutPair * 2);
    if (! t.outputBus.isNull())
        if (auto* b = p.findTrack (t.outputBus)) return juce::String::fromUTF8 ("→ ") + b->name;
    return "Master";
}

void addRoutingMenu (juce::PopupMenu& menu, Session& s, const Track& t)
{
    const auto id = t.id;
    const auto& p = s.project();
    juce::PopupMenu outs;
    const bool toMaster = t.directOutPair < 2 && t.outputBus.isNull();
    outs.addItem ("Master", true, toMaster, [&s, id]
    {
        s.edit ([id] (Project& x) { if (auto* tr = x.findTrack (id)) { tr->directOutPair = 0; tr->outputBus = juce::Uuid::null(); } });
    });
    if (! p.buses().empty()) outs.addSectionHeader ("Buses");
    for (auto* b : p.buses())
    {
        if (b->id == id) continue;
        const auto bus = b->id;
        outs.addItem (b->name, p.canRouteTo (id, bus), t.outputBus == bus && t.directOutPair < 2, [&s, id, bus] { s.setOutputBus (id, bus); });
    }
    outs.addItem ("Nuevo bus con esta pista…"_u, [&s, id] { s.createBusFor ({ id }, false); });
    outs.addSectionHeader ("Salidas de la interfaz");
    const int pairs = s.engine.numOutputChannels() / 2;
    for (int pr = 2; pr <= juce::jmax (2, pairs); ++pr)
        outs.addItem ("Salidas " + juce::String (pr * 2 - 1) + "/" + juce::String (pr * 2) + " (directo, p. ej. in-ears)",
                      pr <= pairs, t.directOutPair == pr, [&s, id, pr] { s.updateTrack (id, [pr] (Track& x) { x.directOutPair = pr; x.outputBus = juce::Uuid::null(); }); });
    menu.addSubMenu ("Salida: " + outputLabel (p, t), outs);

    juce::PopupMenu sends;
    for (auto* b : p.buses())
    {
        if (b->id == id) continue;
        const auto bus = b->id;
        const bool has = std::any_of (t.sends.begin(), t.sends.end(), [&] (auto& snd) { return snd.target == bus; });
        if (has) continue;
        juce::PopupMenu kind;
        kind.addItem ("Post-fader (efectos: reverb, delay)", p.canRouteTo (id, bus), false, [&s, id, bus] { s.addSend (id, bus, false); });
        kind.addItem ("Pre-fader (mezcla de monitoreo)", p.canRouteTo (id, bus), false, [&s, id, bus] { s.addSend (id, bus, true); });
        sends.addSubMenu (b->name, kind, p.canRouteTo (id, bus));
    }
    sends.addItem ("Nuevo bus de efectos con un envío…"_u, [&s, id] { s.createBusFor ({ id }, true, "Reverb"); });
    if (! t.sends.empty())
    {
        sends.addSectionHeader (juce::String::fromUTF8 ("Envíos de esta pista"));
        for (auto& snd : t.sends)
        {
            auto* b = p.findTrack (snd.target);
            if (b == nullptr) continue;
            const auto sendId = snd.id;
            juce::PopupMenu one;
            one.addItem ("Pre-fader", true, snd.preFader, [&s, id, sendId]
            {
                s.updateTrack (id, [sendId] (Track& x) { for (auto& e : x.sends) if (e.id == sendId) e.preFader = ! e.preFader; });
            });
            one.addItem ("Silenciar", true, snd.muted, [&s, id, sendId]
            {
                s.updateTrack (id, [sendId] (Track& x) { for (auto& e : x.sends) if (e.id == sendId) e.muted = ! e.muted; });
            });
            one.addItem (juce::String::fromUTF8 ("Mostrar automatización"), [&s, id, sendId] { s.showLane (id, { AutomationTarget::Kind::sendLevel, sendId }); });
            one.addItem (juce::String::fromUTF8 ("Quitar envío"), [&s, id, sendId] { s.removeSend (id, sendId); });
            sends.addSubMenu (b->name + "  " + formatDb (snd.levelDb) + " dB" + (snd.preFader ? "  (pre)" : ""), one);
        }
    }
    menu.addSubMenu (juce::String::fromUTF8 ("Envíos"), sends);
}

void addAutomationMenu (juce::PopupMenu& menu, Session& s, const Track& t)
{
    const auto id = t.id;
    juce::PopupMenu mode;
    for (auto m : { AutomationMode::off, AutomationMode::read, AutomationMode::touch, AutomationMode::latch })
        mode.addItem (displayName (m), true, t.automationMode == m, [&s, id, m] { s.updateTrack (id, [m] (Track& x) { x.automationMode = m; }); });
    menu.addSubMenu ("Modo: " + displayName (t.automationMode).upToFirstOccurrenceOf (" (", false, false), mode);

    auto laneItem = [&] (juce::PopupMenu& m, const juce::String& label, AutomationTarget target)
    {
        auto* lane = t.findLane (target);
        const bool visible = lane != nullptr && lane->visible;
        m.addItem (label + (lane != nullptr && ! lane->points.empty() && ! visible ? "  ·"_u : ""), true, visible, [&s, id, target, visible]
        {
            if (visible) s.updateTrack (id, [target] (Track& x) { if (auto* l = x.findLane (target)) l->visible = false; });
            else s.showLane (id, target);
        });
    };
    menu.addSectionHeader ("Carriles");
    laneItem (menu, "Volumen", { AutomationTarget::Kind::volume });
    laneItem (menu, "Paneo", { AutomationTarget::Kind::pan });
    for (auto& snd : t.sends)
        if (auto* b = s.project().findTrack (snd.target))
            laneItem (menu, juce::String::fromUTF8 ("Envío › ") + b->name, { AutomationTarget::Kind::sendLevel, snd.id });

    // Parámetros de plugins (instrumento + inserts)
    std::vector<const PluginSlot*> slots;
    if (t.isInstrument() && t.instrument) slots.push_back (&*t.instrument);
    for (auto& ins : t.inserts) slots.push_back (&ins);
    for (auto* slot : slots)
    {
        juce::PopupMenu params;
        auto plugin = s.engine.findPlugin (slot->id);
        if (plugin == nullptr) { menu.addItem (slot->name + " (cargando…)"_u, false, false, nullptr); continue; }
        auto& list = plugin->processor->getParameters();
        const int count = list.size();
        constexpr int perPage = 40;
        for (int start = 0; start < juce::jmin (count, 400); start += perPage)
        {
            juce::PopupMenu page;
            auto& target = count > perPage ? page : params;
            for (int i = start; i < juce::jmin (count, start + perPage); ++i)
            {
                auto* prm = list[i];
                if (! prm->isAutomatable()) continue;
                const auto name = prm->getName (40);
                laneItem (target, name.isNotEmpty() ? name : juce::String (i + 1), { AutomationTarget::Kind::pluginParam, slot->id, i, name });
            }
            if (count > perPage) params.addSubMenu (juce::String (start + 1) + juce::String::fromUTF8 ("–") + juce::String (juce::jmin (count, start + perPage)), page);
        }
        if (count == 0) params.addItem (juce::String::fromUTF8 ("Sin parámetros automatizables"), false, false, nullptr);
        menu.addSubMenu (slot->name, params);
    }
    if (s.lastTouchedParam && s.lastTouchedParam->track == id)
    {
        auto target = s.lastTouchedParam->target;
        menu.addItem (juce::String::fromUTF8 ("Último parámetro tocado: ") + target.paramName, [&s, id, target] { s.showLane (id, target); });
    }
    const bool anyVisible = std::any_of (t.automation.begin(), t.automation.end(), [] (auto& l) { return l.visible; });
    const bool anyLanes = ! t.automation.empty();
    menu.addSeparator();
    menu.addItem ("Mostrar todos los carriles con puntos", anyLanes, false, [&s, id]
    {
        s.updateTrack (id, [] (Track& x) { for (auto& l : x.automation) if (! l.points.empty()) l.visible = true; }, false);
    });
    menu.addItem ("Ocultar todos los carriles", anyVisible, false, [&s, id]
    {
        s.updateTrack (id, [] (Track& x) { for (auto& l : x.automation) l.visible = false; }, false);
    });
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
