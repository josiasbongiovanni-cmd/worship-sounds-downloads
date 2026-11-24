#include "Session.h"
#include "Model/Serialization.h"

namespace wdaw
{

//==============================================================================
juce::String MidiMapping::sourceLabel() const
{
    switch (kind)
    {
        case Kind::cc:    return "CC " + juce::String (number) + " · canal "_u + juce::String (channel);
        case Kind::note:  return "Nota " + juce::String (number) + " · canal "_u + juce::String (channel);
        case Kind::pitch: return "Pitch bend · canal "_u + juce::String (channel);
    }
    return {};
}

juce::String MidiMapping::targetLabel (Target t)
{
    switch (t)
    {
        case Target::trackVolume:    return "Volumen";
        case Target::trackPan:       return "Paneo";
        case Target::trackMute:      return "Mute";
        case Target::trackSolo:      return "Solo";
        case Target::trackArm:       return "Armar";
        case Target::masterVolume:   return "Master · Volumen"_u;
        case Target::playStop:       return "Reproducir / Detener";
        case Target::record:         return "Grabar";
        case Target::repeat:         return "Repetir";
        case Target::nextMarker:     return "Marcador siguiente";
        case Target::previousMarker: return "Marcador anterior";
        case Target::goToStart:      return "Ir al inicio";
        case Target::panic:          return juce::String::fromUTF8 ("Pánico (todas las notas apagadas)");
    }
    return {};
}

static juce::var mappingsToVar (const std::vector<MidiMapping>& list)
{
    juce::Array<juce::var> arr;
    for (auto& m : list)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("kind", (int) m.kind);
        o->setProperty ("channel", m.channel);
        o->setProperty ("number", m.number);
        o->setProperty ("target", (int) m.target);
        o->setProperty ("track", m.trackId.toDashedString());
        arr.add (juce::var (o));
    }
    return arr;
}

static std::vector<MidiMapping> mappingsFromFile (const juce::File& f)
{
    std::vector<MidiMapping> list;
    auto v = juce::JSON::parse (f.loadFileAsString());
    if (auto* arr = v.getArray())
        for (auto& o : *arr)
        {
            MidiMapping m;
            m.kind = (MidiMapping::Kind) (int) o.getProperty ("kind", 0);
            m.channel = o.getProperty ("channel", 1);
            m.number = o.getProperty ("number", 0);
            m.target = (MidiMapping::Target) (int) o.getProperty ("target", 0);
            const auto t = o.getProperty ("track", "").toString();
            if (t.isNotEmpty()) m.trackId = juce::Uuid (t);
            list.push_back (m);
        }
    return list;
}

MidiLearn::MidiLearn()
{
    global = mappingsFromFile (PluginManager::settingsFolder().getChildFile ("midi-learn.json"));
    rebuildKeys();
}

void MidiLearn::loadProject (const juce::File& folder)
{
    project = mappingsFromFile (folder.getChildFile ("midi-learn.json"));
    rebuildKeys();
}

void MidiLearn::saveProject (const juce::File& folder) const
{
    folder.getChildFile ("midi-learn.json").replaceWithText (juce::JSON::toString (mappingsToVar (project)));
}

void MidiLearn::saveGlobal() const
{
    PluginManager::settingsFolder().getChildFile ("midi-learn.json").replaceWithText (juce::JSON::toString (mappingsToVar (global)));
}

int MidiLearn::key (MidiMapping::Kind k, int ch, int n) { return (int) k << 16 | ch << 8 | n; }

void MidiLearn::rebuildKeys()
{
    std::set<int> k;
    for (auto& m : global) k.insert (key (m.kind, m.channel, m.number));
    for (auto& m : project) k.insert (key (m.kind, m.channel, m.number));
    const juce::SpinLock::ScopedLockType sl (lock);
    keys = std::move (k);
}

void MidiLearn::startLearning (MidiMapping::Target t, juce::Uuid trackId)
{
    pending = {};
    pending.target = t;
    pending.trackId = trackId;
    learningLabel = MidiMapping::targetLabel (t);
    learning = true;
}

void MidiLearn::cancelLearning() { learning = false; }

const MidiMapping* MidiLearn::find (MidiMapping::Target t, juce::Uuid trackId) const
{
    for (auto* list : { &project, &global })
        for (auto& m : *list)
            if (m.target == t && m.trackId == trackId) return &m;
    return nullptr;
}

void MidiLearn::remove (const MidiMapping& r)
{
    auto same = [&] (const MidiMapping& m) { return m.target == r.target && m.trackId == r.trackId && m.kind == r.kind && m.number == r.number && m.channel == r.channel; };
    global.erase (std::remove_if (global.begin(), global.end(), same), global.end());
    project.erase (std::remove_if (project.begin(), project.end(), same), project.end());
    rebuildKeys();
    saveGlobal();
}

bool MidiLearn::filter (const juce::MidiMessage& m)
{
    MidiMapping::Kind kind;
    int number = 0;
    float value = 0;
    if (m.isController())        { kind = MidiMapping::Kind::cc; number = m.getControllerNumber(); value = (float) m.getControllerValue() / 127.0f; }
    else if (m.isNoteOn())       { kind = MidiMapping::Kind::note; number = m.getNoteNumber(); value = (float) m.getVelocity() / 127.0f; }
    else if (m.isNoteOff())      { kind = MidiMapping::Kind::note; number = m.getNoteNumber(); value = 0; }
    else if (m.isPitchWheel())   { kind = MidiMapping::Kind::pitch; value = (float) m.getPitchWheelValue() / 16383.0f; }
    else return false;
    const int ch = m.getChannel();

    if (learning.load())
    {
        if (value <= 0 && kind != MidiMapping::Kind::pitch) return true;   // ignorar el "soltar"
        auto learned = pending;
        learned.kind = kind; learned.channel = ch; learned.number = number;
        learning = false;
        juce::MessageManager::callAsync ([this, learned]
        {
            auto conflicts = [&] (const MidiMapping& x)
            {
                return (x.target == learned.target && x.trackId == learned.trackId)
                    || (x.kind == learned.kind && x.channel == learned.channel && x.number == learned.number);
            };
            global.erase (std::remove_if (global.begin(), global.end(), conflicts), global.end());
            project.erase (std::remove_if (project.begin(), project.end(), conflicts), project.end());
            (learned.isTrackTarget() ? project : global).push_back (learned);
            rebuildKeys();
            saveGlobal();
            if (onLearned) onLearned();
        });
        return true;
    }

    bool mapped;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        mapped = keys.count (key (kind, ch, number)) > 0;
    }
    if (! mapped) return false;
    juce::MessageManager::callAsync ([this, kind, ch, number, value]
    {
        for (auto* list : { &project, &global })
            for (auto& map : *list)
                if (map.kind == kind && map.channel == ch && map.number == number && onAction)
                {
                    if (map.isContinuous()) onAction (map, value);
                    else if (value > 0.5f) onAction (map, 1.0f);
                }
    });
    return true;
}

//==============================================================================
Session::Session (AudioEngine& e, PluginManager& p, MidiLearn& ml, juce::File f, Project pr)
    : engine (e), plugins (p), midiLearn (ml), folder (std::move (f)), proj (std::move (pr))
{
    if (! proj.tracks.empty()) selectedTrack = proj.tracks.front().id;
    engine.setProject (proj, folder);
    midiLearn.loadProject (folder);
    refreshLiveMidi();
    startTimer (200);
    writeTimer = std::make_unique<juce::TimedCallback> ([this] { writeTick(); });
    writeTimer->startTimerHz (30);
}

Session::~Session()
{
    *alive = false;
    stopTimer();
    writeTimer.reset();
    if (isRecording()) finishRecording();
    engine.stop();
    saveNow (true);
}

void Session::edit (const std::function<void (Project&)>& fn, bool undoable)
{
    auto copy = proj;
    fn (copy);
    if (undoable)
    {
        undoStack.push_back (proj);
        if (undoStack.size() > 300) undoStack.erase (undoStack.begin());
        redoStack.clear();
    }
    copy.modified = juce::Time::getCurrentTime();
    setProject (std::move (copy));
}

void Session::setProject (Project p)
{
    proj = std::move (p);
    engine.setProject (proj, folder);
    refreshLiveMidi();
    dirty = true;
    ticksSinceEdit = 0;
    ++revision;
    sendChangeMessage();
}

void Session::beginGesture()
{
    undoStack.push_back (proj);
    redoStack.clear();
}

void Session::undo()
{
    finishWriters();
    if (undoStack.empty()) return;
    redoStack.push_back (proj);
    auto p = undoStack.back();
    undoStack.pop_back();
    setProject (std::move (p));
}

void Session::redo()
{
    if (redoStack.empty()) return;
    undoStack.push_back (proj);
    auto p = redoStack.back();
    redoStack.pop_back();
    setProject (std::move (p));
}

void Session::applyPluginStates (Project& p)
{
    auto apply = [&] (PluginSlot& s) { if (auto it = lastPluginStates.find (s.id); it != lastPluginStates.end()) s.state = it->second; };
    for (auto& t : p.tracks)
    {
        for (auto& s : t.inserts) apply (s);
        if (t.instrument) apply (*t.instrument);   // el sonido elegido en el instrumento también se guarda
    }
    for (auto& s : p.masterInserts) apply (s);
}

void Session::saveNow (bool capturePlugins)
{
    if (capturePlugins)
    {
        lastPluginStates = engine.capturePluginStates();
        ticksSincePluginCapture = 0;
    }
    auto copy = proj;
    applyPluginStates (copy);
    if (auto r = io::save (copy, folder); r.failed())
    {
        if (showError) showError (r.getErrorMessage());
        return;
    }
    midiLearn.saveProject (folder);
    dirty = false;
}

void Session::timerCallback()
{
    // Autoguardado 0,8 s después del último cambio (sin pedir el estado de los plugins, que puede ser lento).
    if (dirty && ticksSinceEdit >= 0 && ++ticksSinceEdit >= 4)
    {
        saveNow (++ticksSincePluginCapture > 600);
        ticksSinceEdit = -1;
    }
    else if (++ticksSincePluginCapture > 600 && ! isPlaying())
        saveNow (true);
}

//==============================================================================
Beats Session::playhead() const
{
    return isPlaying() ? engine.playheadBeats() : editCursor;
}

void Session::togglePlay() { isPlaying() ? stop() : play(); }

void Session::play()
{
    engine.play (editCursor);
    sendChangeMessage();
}

void Session::stop()
{
    finishWriters();
    if (isRecording()) finishRecording();
    engine.stop();
    engine.seek (editCursor);    // como REAPER: al detener vuelve al cursor de edición
    sendChangeMessage();
}

void Session::seek (Beats b)
{
    editCursor = juce::jmax (0.0, b);
    if (! isRecording()) engine.seek (editCursor);
    sendChangeMessage();
}

void Session::goToStart() { seek (0); }
void Session::goToEnd()   { seek (proj.contentEnd()); }

void Session::toggleRecord()
{
    if (isRecording()) { stop(); return; }

    AudioEngine::RecordRequest req;
    for (auto& t : proj.tracks)
    {
        if (! t.armed) continue;
        if (t.isInstrument()) req.midiTracks.push_back (t.id);
        else if (! t.input.isOff()) req.audioTracks.push_back (t.id);
    }
    if (req.audioTracks.empty() && req.midiTracks.empty())
    {
        // Nada armado: armar la pista seleccionada (como hace REAPER con "record: arm selected").
        auto* t = proj.findTrack (selectedTrack);
        if (t == nullptr) { if (showError) showError ("Arma una pista (botón R) para grabar."_u); return; }
        const auto id = t->id;
        const bool inst = t->isInstrument();
        updateTrack (id, [] (Track& tr) { tr.armed = true; if (tr.input.isOff() && ! tr.isInstrument()) tr.input = { TrackInput::Kind::mono, 0 }; });
        (inst ? req.midiTracks : req.audioTracks).push_back (id);
    }

    if (isPlaying())
    {
        // Punch-in: empieza a grabar donde está sonando, sin reiniciar.
        req.keepTransport = true;
        req.recordStart = engine.playheadBeats();
    }
    else
    {
        req.recordStart = editCursor;
        req.playFrom = editCursor - countInBars * proj.beatsPerBar;
    }
    if (auto r = engine.startRecording (req, proj, folder); r.failed())
    {
        if (showError) showError (r.getErrorMessage());
        return;
    }
    sendChangeMessage();
}

void Session::finishRecording()
{
    auto res = engine.stopRecording();
    if (res.audio.empty() && res.midi.empty()) { sendChangeMessage(); return; }
    int take = 1;
    for (auto& t : proj.tracks) take += (int) t.clips.size() + (int) t.midiClips.size();
    const auto tl = proj.timeline();
    edit ([&] (Project& p)
    {
        for (auto& a : res.audio)
        {
            auto* t = p.findTrack (a.trackId);
            if (t == nullptr) continue;
            AudioClip c;
            c.name = t->name + " · Toma "_u + juce::String (take);
            c.file = a.fileName;
            c.start = res.start;
            c.offset = a.offsetSeconds;
            c.length = a.lengthSeconds;
            c.sourceLength = a.sourceSeconds;
            // La toma nueva reemplaza lo que había debajo (no quedan ítems superpuestos sin querer).
            edit::clearRange (p, a.trackId, c.start, c.start + tl.toBeats (c.length));
            edit::addClip (p, a.trackId, c);
        }
        for (auto& [trackId, clip] : res.midi)
        {
            edit::clearRange (p, trackId, clip.start, clip.end());
            edit::addMidiClip (p, trackId, clip);
        }
    });
    if (res.overflow && showError)
        showError ("El disco no alcanzó a escribir toda la grabación. Prueba con un buffer más grande o un disco más rápido."_u);
}

//==============================================================================
bool Session::isSelected (const juce::Uuid& id) const
{
    return std::find (selectedClips.begin(), selectedClips.end(), id) != selectedClips.end();
}

void Session::selectClip (const juce::Uuid& id, bool add)
{
    if (! add) selectedClips.clear();
    if (auto it = std::find (selectedClips.begin(), selectedClips.end(), id); it != selectedClips.end())
    {
        if (add) selectedClips.erase (it);
    }
    else selectedClips.push_back (id);
    if (auto ref = edit::findClip (proj, id)) selectedTrack = proj.tracks[(size_t) ref.track].id;
    refreshLiveMidi();
    sendChangeMessage();
}

void Session::selectClips (std::vector<juce::Uuid> ids)
{
    selectedClips = std::move (ids);
    sendChangeMessage();
}

void Session::clearClipSelection()
{
    selectedClips.clear();
    sendChangeMessage();
}

void Session::selectTrack (const juce::Uuid& id)
{
    selectedTrack = id;
    refreshLiveMidi();
    sendChangeMessage();
}

void Session::selectAllClips()
{
    selectedClips.clear();
    for (auto& t : proj.tracks)
    {
        for (auto& c : t.clips) selectedClips.push_back (c.id);
        for (auto& c : t.midiClips) selectedClips.push_back (c.id);
    }
    sendChangeMessage();
}

void Session::splitAtCursor()
{
    const auto at = playhead();
    edit ([&] (Project& p)
    {
        bool any = false;
        for (auto& id : selectedClips) if (edit::splitClip (p, id, at)) any = true;
        if (! any) edit::splitAll (p, at);
    });
}

void Session::deleteSelection()
{
    if (selectedClips.empty()) return;
    auto ids = selectedClips;
    edit ([&] (Project& p) { for (auto& id : ids) edit::removeClip (p, id); });
    selectedClips.clear();
    sendChangeMessage();
}

void Session::duplicateSelection()
{
    if (selectedClips.empty()) return;
    auto ids = selectedClips;
    std::vector<juce::Uuid> created;
    edit ([&] (Project& p) { for (auto& id : ids) if (auto n = edit::duplicateClip (p, id)) created.push_back (*n); });
    selectClips (created);
}

void Session::copySelection()
{
    if (! selectedClips.empty()) clipboard = edit::copyClips (proj, selectedClips);
}

void Session::cutSelection()
{
    copySelection();
    deleteSelection();
}

void Session::paste()
{
    if (clipboard.empty()) return;
    auto track = juce::jmax (0, proj.trackIndex (selectedTrack));
    std::vector<juce::Uuid> created;
    const auto at = editCursor;
    edit ([&] (Project& p) { created = edit::paste (p, clipboard, at, track); });
    selectClips (created);
}

//==============================================================================
juce::Uuid Session::addTrack (TrackRole role)
{
    juce::Uuid id;
    edit ([&] (Project& p)
    {
        juce::String name;
        int count = 0;
        for (auto& t : p.tracks) if (t.role == role) ++count;
        switch (role)
        {
            case TrackRole::guide:      name = "Guía"_u; break;
            case TrackRole::instrument: name = "Instrumento " + juce::String (count + 1); break;
            case TrackRole::bus:        name = "Bus " + juce::String (count + 1); break;
            default:                    name = "Audio " + juce::String ((int) p.tracks.size() + 1); break;
        }
        auto& t = edit::addTrack (p, name, role);
        if (role == TrackRole::instrument) t.instrument = PluginManager::defaultInstrumentSlot();
        id = t.id;
    });
    selectTrack (id);
    return id;
}

void Session::removeTrack (const juce::Uuid& id)
{
    edit ([&] (Project& p) { edit::removeTrack (p, id); });
    if (selectedTrack == id) selectedTrack = proj.tracks.empty() ? juce::Uuid::null() : proj.tracks.front().id;
    sendChangeMessage();
}

void Session::updateTrack (const juce::Uuid& id, const std::function<void (Track&)>& fn, bool undoable)
{
    edit ([&] (Project& p) { if (auto* t = p.findTrack (id)) fn (*t); }, undoable);
}

//==============================================================================
std::optional<juce::Uuid> Session::addSend (const juce::Uuid& from, const juce::Uuid& bus, bool preFader)
{
    if (! proj.canRouteTo (from, bus)) { if (showError) showError (juce::String::fromUTF8 ("Ese envío crearía un bucle de señal.")); return {}; }
    std::optional<juce::Uuid> id;
    edit ([&] (Project& p) { id = edit::addSend (p, from, bus, preFader ? 0.0 : -12.0, preFader); });
    return id;
}

void Session::removeSend (const juce::Uuid& track, const juce::Uuid& send)
{
    edit ([&] (Project& p) { edit::removeSend (p, track, send); });
}

void Session::setOutputBus (const juce::Uuid& track, const juce::Uuid& bus)
{
    if (! bus.isNull() && ! proj.canRouteTo (track, bus)) { if (showError) showError (juce::String::fromUTF8 ("Esa salida crearía un bucle de señal.")); return; }
    edit ([&] (Project& p) { edit::setOutputBus (p, track, bus); });
}

juce::Uuid Session::createBusFor (const std::vector<juce::Uuid>& tracks, bool asSend, juce::String name)
{
    juce::Uuid busId;
    edit ([&] (Project& p)
    {
        int count = 0;
        for (auto& t : p.tracks) if (t.isBus()) ++count;
        auto& bus = edit::addTrack (p, name.isNotEmpty() ? name : "Bus " + juce::String (count + 1), TrackRole::bus, "slate");
        busId = bus.id;
        // El bus queda justo después de la última pista que lo alimenta.
        int last = -1;
        for (auto& id : tracks) last = juce::jmax (last, p.trackIndex (id));
        if (last >= 0) edit::moveTrack (p, busId, last + 1);
        for (auto& id : tracks)
            if (asSend) edit::addSend (p, id, busId);
            else edit::setOutputBus (p, id, busId);
    });
    selectTrack (busId);
    return busId;
}

//==============================================================================
double Session::staticValue (const Track& t, const AutomationTarget& target) const
{
    switch (target.kind)
    {
        case AutomationTarget::Kind::volume: return t.volumeDb;
        case AutomationTarget::Kind::pan:    return t.pan;
        case AutomationTarget::Kind::sendLevel:
            for (auto& s : t.sends) if (s.id == target.ref) return s.levelDb;
            return target.defaultValue();
        case AutomationTarget::Kind::pluginParam:
            if (auto p = engine.findPlugin (target.ref))
                if (target.param >= 0 && target.param < p->processor->getParameters().size())
                    return p->processor->getParameters()[target.param]->getValue();
            return target.defaultValue();
    }
    return 0;
}

double Session::effectiveValue (const Track& t, const AutomationTarget& target) const
{
    if (auto* lane = t.playingLane (target)) return lane->valueAt (playhead());
    return staticValue (t, target);
}

void Session::applyStatic (Track& t, const AutomationTarget& target, double v) const
{
    switch (target.kind)
    {
        case AutomationTarget::Kind::volume: t.volumeDb = v; break;
        case AutomationTarget::Kind::pan:    t.pan = v; break;
        case AutomationTarget::Kind::sendLevel:
            for (auto& s : t.sends) if (s.id == target.ref) s.levelDb = v;
            break;
        case AutomationTarget::Kind::pluginParam: break;   // el estado vive en el plugin
    }
}

Session::Writer* Session::findWriter (const juce::Uuid& track, const AutomationTarget& target)
{
    for (auto& w : writers) if (w.track == track && w.target == target) return &w;
    return nullptr;
}

bool Session::isWriting (const juce::Uuid& track) const
{
    return std::any_of (writers.begin(), writers.end(), [&] (auto& w) { return w.track == track; });
}

void Session::showLane (const juce::Uuid& track, const AutomationTarget& target)
{
    auto* t = proj.findTrack (track);
    if (t == nullptr) return;
    const auto initial = staticValue (*t, target);
    updateTrack (track, [&] (Track& x)
    {
        auto& lane = edit::automation::ensureLane (x, target, initial);
        if (lane.target.paramName.isEmpty()) lane.target.paramName = target.paramName;
    });
}

void Session::touchBegin (const juce::Uuid& track, const AutomationTarget& target)
{
    beginGesture();
    auto* t = proj.findTrack (track);
    if (t == nullptr) return;
    const bool writeMode = t->automationMode == AutomationMode::touch || t->automationMode == AutomationMode::latch;
    if (! writeMode || ! isPlaying()) return;
    if (auto* w = findWriter (track, target)) { w->held = true; return; }
    const auto now = playhead();
    const auto current = effectiveValue (*t, target);
    edit ([&] (Project& p)
    {
        auto& tr = *p.findTrack (track);
        auto& lane = edit::automation::ensureLane (tr, target, current);
        // Se conserva la curva anterior hasta acá; desde ahora manda lo que se toca.
        edit::automation::addPoint (lane, now, lane.valueAt (now));
        lane.writing = true;
        applyStatic (tr, target, current);
    }, false);
    writers.push_back ({ track, target, now, current, true, juce::Time::getMillisecondCounter() });
}

void Session::touchEnd (const juce::Uuid& track, const AutomationTarget& target)
{
    auto* w = findWriter (track, target);
    if (w == nullptr) return;
    w->held = false;
    auto* t = proj.findTrack (track);
    if (t != nullptr && t->automationMode == AutomationMode::touch)
    {
        // Touch: al soltar, el carril vuelve a mandar desde aquí.
        const auto writer = *w;
        writers.erase (writers.begin() + (w - writers.data()));
        const auto now = playhead();
        edit ([&] (Project& p)
        {
            if (auto* tr = p.findTrack (track))
                if (auto* lane = tr->findLane (target))
                {
                    if (now > writer.lastBeat) edit::automation::writeValue (*lane, writer.lastBeat, now, writer.value);
                    lane->writing = false;
                    edit::automation::simplify (*lane, target.kind == AutomationTarget::Kind::volume || target.kind == AutomationTarget::Kind::sendLevel ? 0.05 : 0.002);
                }
        }, false);
    }
}

void Session::setControl (const juce::Uuid& track, const AutomationTarget& target, double value)
{
    auto* t = proj.findTrack (track);
    if (t == nullptr) return;
    value = juce::jlimit (target.minValue(), target.maxValue(), value);

    if (auto* w = findWriter (track, target))
    {
        const auto now = playhead();
        const auto from = w->lastBeat;
        w->value = value;
        w->lastBeat = now;
        w->lastChange = juce::Time::getMillisecondCounter();
        edit ([&] (Project& p)
        {
            auto& tr = *p.findTrack (track);
            applyStatic (tr, target, value);
            if (auto* lane = tr.findLane (target))
            {
                if (now >= from) edit::automation::writeValue (*lane, from, now, value);
                else edit::automation::addPoint (*lane, now, value);   // el loop volvió al inicio
            }
        }, false);
        return;
    }

    // Sin escritura en curso. Detenido y con carril: se edita el punto en el cursor (no hay que buscarlo en el carril).
    if (! isPlaying() && t->playingLane (target) != nullptr)
    {
        const auto at = editCursor;
        edit ([&] (Project& p)
        {
            auto& tr = *p.findTrack (track);
            applyStatic (tr, target, value);
            if (auto* lane = tr.findLane (target)) edit::automation::addPoint (*lane, at, value);
        }, false);
        return;
    }

    // Touch/Latch sin gesto explícito (p. ej. un controlador MIDI): empezar a escribir.
    const bool writeMode = t->automationMode == AutomationMode::touch || t->automationMode == AutomationMode::latch;
    if (writeMode && isPlaying() && target.kind != AutomationTarget::Kind::pluginParam)
    {
        touchBegin (track, target);
        if (auto* w = findWriter (track, target)) w->held = false;
        setControl (track, target, value);
        return;
    }

    if (target.kind != AutomationTarget::Kind::pluginParam)
        edit ([&] (Project& p) { if (auto* tr = p.findTrack (track)) applyStatic (*tr, target, value); }, false);
}

void Session::writeTick()
{
    const bool playingNow = isPlaying();
    if (! playingNow)
    {
        if (wasPlayingForWriters) finishWriters();
        wasPlayingForWriters = false;
        return;
    }
    wasPlayingForWriters = true;
    if (writers.empty()) return;

    const auto now = playhead();
    const auto ms = juce::Time::getMillisecondCounter();
    std::vector<Writer> ended;
    edit ([&] (Project& p)
    {
        for (auto& w : writers)
        {
            auto* tr = p.findTrack (w.track);
            auto* lane = tr != nullptr ? tr->findLane (w.target) : nullptr;
            if (lane == nullptr) continue;
            // Mientras se sostiene (o en latch), se sigue escribiendo el último valor bajo el cursor.
            if (now >= w.lastBeat) edit::automation::writeValue (*lane, w.lastBeat, now, w.value);
            else edit::automation::addPoint (*lane, now, w.value);
            w.lastBeat = now;
            // Touch sin gesto (controlador MIDI): termina tras medio segundo sin cambios.
            if (tr->automationMode == AutomationMode::touch && ! w.held && ms - w.lastChange > 500) ended.push_back (w);
        }
    }, false);
    for (auto& w : ended)
    {
        if (auto* x = findWriter (w.track, w.target)) x->held = false;
        touchEnd (w.track, w.target);
    }
}

void Session::finishWriters()
{
    if (writers.empty()) return;
    auto done = writers;
    writers.clear();
    edit ([&] (Project& p)
    {
        for (auto& w : done)
            if (auto* tr = p.findTrack (w.track))
                if (auto* lane = tr->findLane (w.target))
                {
                    lane->writing = false;
                    edit::automation::simplify (*lane, w.target.kind == AutomationTarget::Kind::volume || w.target.kind == AutomationTarget::Kind::sendLevel ? 0.05 : 0.002);
                }
    }, false);
}

void Session::moveTrack (const juce::Uuid& id, int newIndex)
{
    edit ([&] (Project& p) { edit::moveTrack (p, id, newIndex); });
}

//==============================================================================
void Session::insertMarker()
{
    const auto at = playhead();
    edit ([&] (Project& p) { edit::addMarker (p, at); });
}

void Session::goToMarker (int number)
{
    if (auto* m = edit::markerNumber (proj, number)) seek (m->position);
}

void Session::goToNextStop()
{
    if (auto b = edit::nextStop (proj, playhead())) seek (*b);
}

void Session::goToPreviousStop()
{
    seek (edit::previousStop (proj, playhead()).value_or (0.0));
}

void Session::setTimeSelection (Beats a, Beats b)
{
    if (std::abs (a - b) < 0.01) return;
    edit ([&] (Project& p) { p.loopStart = juce::jmax (0.0, juce::jmin (a, b)); p.loopEnd = juce::jmax (a, b); }, false);
}

void Session::clearTimeSelection()
{
    edit ([] (Project& p) { p.loopStart = p.loopEnd = 0; p.loopEnabled = false; });
}

void Session::toggleRepeat()
{
    edit ([&] (Project& p)
    {
        if (! p.loopEnabled && p.loopEnd <= p.loopStart + 0.01)
            if (auto* s = edit::sectionAt (p, playhead())) { p.loopStart = s->start; p.loopEnd = s->end(); }
        p.loopEnabled = ! p.loopEnabled;
    });
}

void Session::loopSection (const juce::Uuid& id)
{
    for (auto& s : proj.sections)
        if (s.id == id)
        {
            const auto a = s.start, b = s.end();
            edit ([&] (Project& p) { p.loopStart = a; p.loopEnd = b; p.loopEnabled = true; });
            seek (a);
            return;
        }
}

//==============================================================================
void Session::importFiles (const juce::Array<juce::File>& files, std::optional<juce::Uuid> track, std::optional<Beats> at)
{
    if (files.isEmpty()) return;
    const auto position = at.value_or (files.size() > 1 ? 0.0 : editCursor);
    const bool many = files.size() > 1;
    statusText = "Importando…"_u;
    sendChangeMessage();
    juce::Thread::launch ([this, files, track, position, many, f = folder, flag = alive]
    {
        for (auto& file : files)
        {
            if (! flag->load()) return;
            juce::MessageManager::callAsync ([this, flag, name = file.getFileName()] { if (! flag->load()) return; statusText = "Importando "_u + name + "…"_u; sendChangeMessage(); });
            auto res = importAudioFile (file, f);
            juce::MessageManager::callAsync ([this, flag, res, file, track, position, many]
            {
                if (! flag->load()) return;
                if (! res.ok) { if (showError) showError (res.error); return; }
                const auto name = file.getFileNameWithoutExtension();
                edit ([&] (Project& p)
                {
                    juce::Uuid target;
                    if (! many && track && p.findTrack (*track) != nullptr && ! p.findTrack (*track)->isInstrument()) target = *track;
                    else if (! many && p.findTrack (selectedTrack) != nullptr && ! p.findTrack (selectedTrack)->isInstrument()) target = selectedTrack;
                    else target = edit::addTrack (p, name).id;    // varios archivos (stems): una pista por archivo
                    AudioClip c;
                    c.name = name;
                    c.file = res.fileName;
                    c.start = position;
                    c.length = res.lengthSeconds;
                    c.sourceLength = res.lengthSeconds;
                    edit::addClip (p, target, c);
                });
            });
        }
        juce::MessageManager::callAsync ([this, flag] { if (! flag->load()) return; statusText.clear(); sendChangeMessage(); });
    });
}

//==============================================================================
void Session::refreshLiveMidi()
{
    std::vector<juce::Uuid> ids;
    for (auto& t : proj.tracks) if (t.isInstrument() && t.armed) ids.push_back (t.id);
    if (ids.empty())
        if (auto* t = proj.findTrack (selectedTrack); t != nullptr && t->isInstrument()) ids.push_back (t->id);
    engine.setLiveMidiTargets (ids);
}

void Session::handleMidiAction (const MidiMapping& m, float v)
{
    using T = MidiMapping::Target;
    auto track = [&] (const std::function<void (Track&)>& fn) { if (proj.findTrack (m.trackId) != nullptr) updateTrack (m.trackId, fn, false); };
    auto faderDb = [] (float pos) { return pos <= 0.0005f ? Decibels::silence : Decibels::silence + std::pow (pos, 1.0 / 2.2) * 66.0; };
    switch (m.target)
    {
        case T::trackVolume:    if (proj.findTrack (m.trackId)) setControl (m.trackId, { AutomationTarget::Kind::volume }, std::round (faderDb (v) * 10) / 10); break;
        case T::trackPan:       if (proj.findTrack (m.trackId)) setControl (m.trackId, { AutomationTarget::Kind::pan }, std::round ((v * 2 - 1) * 100) / 100); break;
        case T::trackMute:      track ([] (Track& t) { t.muted = ! t.muted; }); break;
        case T::trackSolo:      track ([] (Track& t) { t.solo = ! t.solo; }); break;
        case T::trackArm:       track ([] (Track& t) { t.armed = ! t.armed; }); break;
        case T::masterVolume:   edit ([&] (Project& p) { p.masterVolumeDb = std::round (faderDb (v) * 10) / 10; }, false); break;
        case T::playStop:       togglePlay(); break;
        case T::record:         toggleRecord(); break;
        case T::repeat:         toggleRepeat(); break;
        case T::nextMarker:     goToNextStop(); break;
        case T::previousMarker: goToPreviousStop(); break;
        case T::goToStart:      goToStart(); break;
        case T::panic:          engine.panic(); break;
    }
}

juce::Uuid Session::createMidiClip (const juce::Uuid& trackId, std::optional<Beats> at)
{
    auto* t = proj.findTrack (trackId);
    if (t == nullptr || ! t->isInstrument()) return {};
    MidiClip c;
    c.name = t->name + " · MIDI"_u;
    c.start = snapEnabled ? Timeline::snapDown (at.value_or (editCursor), proj.beatsPerBar) : at.value_or (editCursor);
    c.length = proj.beatsPerBar * 4;
    const auto id = c.id;
    edit ([&] (Project& p) { edit::addMidiClip (p, trackId, c); });
    selectClips ({ id });
    editingMidiClip = id;
    if (openMidiEditor) openMidiEditor();
    return id;
}

Beats Session::snap (Beats b) const
{
    return snapEnabled ? Timeline::snap (b, gridDivision) : juce::jmax (0.0, b);
}

Beats Session::timelineLength() const
{
    return juce::jmax (proj.contentEnd() + proj.beatsPerBar * 16, (double) proj.beatsPerBar * 64, playhead() + proj.beatsPerBar * 8);
}

} // namespace wdaw
