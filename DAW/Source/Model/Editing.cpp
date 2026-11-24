#include "Editing.h"
#include <map>

namespace wdaw::edit
{

static const char* palette[] = { "violet", "indigo", "sky", "teal", "mint", "lime", "amber", "coral", "rose", "slate" };
static constexpr Beats epsilon = 1.0e-6;

static bool contains (const std::vector<juce::Uuid>& ids, const juce::Uuid& id)
{
    return ids.empty() || std::find (ids.begin(), ids.end(), id) != ids.end();
}

//==============================================================================
Track& addTrack (Project& p, const juce::String& name, TrackRole role, juce::String colour)
{
    Track t;
    t.name = name;
    t.role = role;
    t.colour = colour.isNotEmpty() ? colour : juce::String (palette[p.tracks.size() % std::size (palette)]);
    if (role == TrackRole::instrument) t.input.kind = TrackInput::Kind::off;
    p.tracks.push_back (std::move (t));
    return p.tracks.back();
}

void removeTrack (Project& p, const juce::Uuid& id)
{
    // Las hijas de una carpeta borrada pasan a la carpeta de arriba.
    juce::Uuid grandParent = juce::Uuid::null();
    if (auto* t = p.findTrack (id)) grandParent = t->parent;
    for (auto& t : p.tracks) if (t.parent == id) t.parent = grandParent;
    p.tracks.erase (std::remove_if (p.tracks.begin(), p.tracks.end(), [&] (auto& t) { return t.id == id; }), p.tracks.end());
    // Lo que mandaba a ese bus vuelve al Master; sus envíos (y sus carriles) se quitan.
    for (auto& t : p.tracks)
    {
        if (t.outputBus == id) t.outputBus = juce::Uuid::null();
        for (auto& s : std::vector<Send> (t.sends))
            if (s.target == id) removeSend (p, t.id, s.id);
    }
}

//==============================================================================
std::pair<int, int> trackBlock (const Project& p, const juce::Uuid& id)
{
    const auto first = p.trackIndex (id);
    if (first < 0) return { -1, -1 };
    int last = first;
    while (last + 1 < (int) p.tracks.size() && p.isDescendantOf (p.tracks[(size_t) last + 1], id)) ++last;
    return { first, last };
}

void moveTrackBlock (Project& p, const juce::Uuid& id, int newIndex)
{
    const auto [first, last] = trackBlock (p, id);
    if (first < 0) return;
    std::vector<Track> block (std::make_move_iterator (p.tracks.begin() + first), std::make_move_iterator (p.tracks.begin() + last + 1));
    p.tracks.erase (p.tracks.begin() + first, p.tracks.begin() + last + 1);
    // newIndex se refiere a la lista sin el bloque
    newIndex = juce::jlimit (0, (int) p.tracks.size(), newIndex);
    p.tracks.insert (p.tracks.begin() + newIndex, std::make_move_iterator (block.begin()), std::make_move_iterator (block.end()));
}

bool setParent (Project& p, const juce::Uuid& trackId, const juce::Uuid& folderId)
{
    auto* t = p.findTrack (trackId);
    auto* f = p.findTrack (folderId);
    if (t == nullptr || f == nullptr || trackId == folderId || ! f->isBus()) return false;
    if (p.isDescendantOf (*f, trackId)) return false;          // la carpeta está dentro de la pista
    // Sin bucles de señal: la carpeta no puede alimentar (por envíos) a la pista.
    if (p.feedsInto (folderId, trackId)) return false;
    f->isFolder = true;
    t->parent = folderId;
    if (t->outputBus == folderId) t->outputBus = juce::Uuid::null();
    t->directOutPair = t->directOutPair >= 2 ? t->directOutPair : 0;
    // Al final del contenido de la carpeta (sin contar la propia pista)
    const auto [tf, tl] = trackBlock (p, trackId);
    std::vector<Track> block (std::make_move_iterator (p.tracks.begin() + tf), std::make_move_iterator (p.tracks.begin() + tl + 1));
    p.tracks.erase (p.tracks.begin() + tf, p.tracks.begin() + tl + 1);
    const auto [ff, fl] = trackBlock (p, folderId);
    p.tracks.insert (p.tracks.begin() + fl + 1, std::make_move_iterator (block.begin()), std::make_move_iterator (block.end()));
    return true;
}

void unparent (Project& p, const juce::Uuid& trackId)
{
    auto* t = p.findTrack (trackId);
    if (t == nullptr || t->parent.isNull()) return;
    const auto folder = t->parent;
    auto* f = p.findTrack (folder);
    t->parent = f != nullptr ? f->parent : juce::Uuid::null();
    // Queda justo después del contenido de la carpeta
    const auto [tf, tl] = trackBlock (p, trackId);
    std::vector<Track> block (std::make_move_iterator (p.tracks.begin() + tf), std::make_move_iterator (p.tracks.begin() + tl + 1));
    p.tracks.erase (p.tracks.begin() + tf, p.tracks.begin() + tl + 1);
    const auto [ff, fl] = trackBlock (p, folder);
    p.tracks.insert (p.tracks.begin() + (fl >= 0 ? fl + 1 : (int) p.tracks.size()), std::make_move_iterator (block.begin()), std::make_move_iterator (block.end()));
}

juce::Uuid makeFolder (Project& p, const std::vector<juce::Uuid>& ids, const juce::String& name)
{
    int at = (int) p.tracks.size();
    juce::Uuid outer = juce::Uuid::null();
    for (auto& id : ids)
        if (const auto i = p.trackIndex (id); i >= 0 && i < at) { at = i; outer = p.tracks[(size_t) i].parent; }
    auto& f = addTrack (p, name, TrackRole::bus, "slate");
    f.isFolder = true;
    f.parent = outer;
    const auto fid = f.id;
    moveTrackBlock (p, fid, at);
    // En el orden en que estaban
    std::vector<juce::Uuid> ordered;
    for (auto& t : p.tracks)
        if (std::find (ids.begin(), ids.end(), t.id) != ids.end() && t.id != fid) ordered.push_back (t.id);
    for (auto& id : ordered)
    {
        auto* t = p.findTrack (id);
        // Si una carpeta elegida ya contiene a otra elegida, se mueve el bloque de afuera.
        bool parentChosen = false;
        if (t != nullptr) for (auto& o : ordered) if (o != id && p.isDescendantOf (*t, o)) parentChosen = true;
        if (! parentChosen) setParent (p, id, fid);
    }
    return fid;
}

void normalizeFolders (Project& p)
{
    std::vector<juce::Uuid> open;   // carpetas abiertas (de afuera hacia adentro)
    for (auto& t : p.tracks)
    {
        if (! t.parent.isNull())
        {
            auto it = std::find (open.begin(), open.end(), t.parent);
            auto* f = p.findTrack (t.parent);
            if (it == open.end() || f == nullptr || ! f->isBus()) { t.parent = juce::Uuid::null(); open.clear(); }
            else open.erase (it + 1, open.end());
        }
        else open.clear();
        if (t.isBus() && t.isFolder) open.push_back (t.id);
    }
    for (auto& t : p.tracks)
    {
        if (! t.isBus()) { t.isFolder = false; t.folderCollapsed = false; }
        if (! t.parent.isNull() && p.feedsInto (t.parent, t.id)) t.parent = juce::Uuid::null();   // bucle: se rompe
    }
}

void adoptFolderFromAbove (Project& p, const juce::Uuid& id)
{
    const auto i = p.trackIndex (id);
    if (i < 0) return;
    auto& t = p.tracks[(size_t) i];
    juce::Uuid parent = juce::Uuid::null();
    if (i > 0)
    {
        auto& above = p.tracks[(size_t) i - 1];
        if (above.isBus() && above.isFolder && ! above.folderCollapsed && above.id != id) parent = above.id;
        else parent = above.parent;
        // Encima hay una carpeta cerrada: se queda al mismo nivel que ella
        if (above.isBus() && above.isFolder && above.folderCollapsed) parent = above.parent;
        if (p.isInsideCollapsedFolder (above)) parent = juce::Uuid::null();
    }
    if (parent == id) parent = juce::Uuid::null();
    if (! parent.isNull())
        if (auto* f = p.findTrack (parent); f == nullptr || p.isDescendantOf (*f, id)) parent = juce::Uuid::null();
    t.parent = parent;
    normalizeFolders (p);
}

//==============================================================================
std::optional<juce::Uuid> addSend (Project& p, const juce::Uuid& from, const juce::Uuid& bus, double levelDb, bool preFader)
{
    auto* t = p.findTrack (from);
    if (t == nullptr || ! p.canRouteTo (from, bus)) return {};
    for (auto& s : t->sends) if (s.target == bus) return s.id;
    Send s;
    s.target = bus;
    s.levelDb = levelDb;
    s.preFader = preFader;
    t->sends.push_back (s);
    return s.id;
}

void removeSend (Project& p, const juce::Uuid& trackId, const juce::Uuid& sendId)
{
    if (auto* t = p.findTrack (trackId))
    {
        t->sends.erase (std::remove_if (t->sends.begin(), t->sends.end(), [&] (auto& s) { return s.id == sendId; }), t->sends.end());
        t->automation.erase (std::remove_if (t->automation.begin(), t->automation.end(), [&] (auto& l)
        {
            return l.target.kind == AutomationTarget::Kind::sendLevel && l.target.ref == sendId;
        }), t->automation.end());
    }
}

bool setOutputBus (Project& p, const juce::Uuid& trackId, const juce::Uuid& bus)
{
    auto* t = p.findTrack (trackId);
    if (t == nullptr) return false;
    if (bus.isNull()) { t->outputBus = bus; return true; }
    const auto old = t->outputBus;
    t->outputBus = juce::Uuid::null();
    if (! p.canRouteTo (trackId, bus)) { t->outputBus = old; return false; }
    t->outputBus = bus;
    t->directOutPair = 0;
    return true;
}

//==============================================================================
namespace automation
{
    AutomationLane& ensureLane (Track& t, const AutomationTarget& target, double initialValue)
    {
        if (auto* l = t.findLane (target)) { l->visible = true; return *l; }
        AutomationLane l;
        l.target = target;
        l.points.push_back ({ 0.0, initialValue });
        t.automation.push_back (std::move (l));
        return t.automation.back();
    }

    int addPoint (AutomationLane& l, Beats time, double value)
    {
        time = juce::jmax (0.0, time);
        value = juce::jlimit (l.target.minValue(), l.target.maxValue(), value);
        for (int i = 0; i < (int) l.points.size(); ++i)
            if (std::abs (l.points[(size_t) i].time - time) < 1.0e-4) { l.points[(size_t) i].value = value; return i; }
        auto it = std::upper_bound (l.points.begin(), l.points.end(), time, [] (Beats x, const AutomationPoint& p) { return x < p.time; });
        it = l.points.insert (it, { time, value });
        return (int) (it - l.points.begin());
    }

    void removePoints (AutomationLane& l, Beats from, Beats to)
    {
        l.points.erase (std::remove_if (l.points.begin(), l.points.end(), [&] (auto& p) { return p.time >= from && p.time <= to; }), l.points.end());
    }

    void writeValue (AutomationLane& l, Beats from, Beats to, double value)
    {
        if (to < from) std::swap (from, to);
        // Lo que viene después conserva su forma: se ancla el valor anterior justo después de lo escrito.
        const bool hasLater = ! l.points.empty() && l.points.back().time > to + 1.0e-3;
        const double laterValue = hasLater ? l.valueAt (to + 1.0e-3) : 0.0;
        removePoints (l, from + 1.0e-6, to + 1.0e-3);
        addPoint (l, to, value);
        if (hasLater) addPoint (l, to + 1.0e-3, laterValue);
    }

    void simplify (AutomationLane& l, double tolerance)
    {
        if (l.points.size() < 3) return;
        std::vector<AutomationPoint> out { l.points.front() };
        for (size_t i = 1; i + 1 < l.points.size(); ++i)
        {
            const auto& a = out.back();
            const auto& b = l.points[i];
            const auto& c = l.points[i + 1];
            // Solo se quitan puntos en tramos rectos (las curvas se respetan)
            if (a.shape != CurveShape::linear || b.shape != CurveShape::linear) { out.push_back (b); continue; }
            const auto span = c.time - a.time;
            const auto expected = span > 1.0e-9 ? a.value + (c.value - a.value) * (b.time - a.time) / span : a.value;
            if (std::abs (expected - b.value) > tolerance) out.push_back (b);
        }
        out.push_back (l.points.back());
        l.points = std::move (out);
    }

    juce::String laneName (const Project& p, const Track& t, const AutomationLane& l)
    {
        switch (l.target.kind)
        {
            case AutomationTarget::Kind::volume: return "Volumen";
            case AutomationTarget::Kind::pan:    return "Paneo";
            case AutomationTarget::Kind::sendLevel:
                for (auto& s : t.sends)
                    if (s.id == l.target.ref)
                        if (auto* b = p.findTrack (s.target)) return juce::String::fromUTF8 ("Envío › ") + b->name;
                return juce::String::fromUTF8 ("Envío");
            case AutomationTarget::Kind::pluginParam:
            {
                juce::String plugin;
                for (auto& s : t.inserts) if (s.id == l.target.ref) plugin = s.name;
                if (t.instrument && t.instrument->id == l.target.ref) plugin = t.instrument->name;
                return (plugin.isNotEmpty() ? plugin + juce::String::fromUTF8 (" › ") : juce::String()) + l.target.paramName;
            }
        }
        return {};
    }
}

void moveTrack (Project& p, const juce::Uuid& id, int newIndex)
{
    const auto from = p.trackIndex (id);
    if (from < 0) return;
    auto t = std::move (p.tracks[(size_t) from]);
    p.tracks.erase (p.tracks.begin() + from);
    newIndex = juce::jlimit (0, (int) p.tracks.size(), newIndex);
    p.tracks.insert (p.tracks.begin() + newIndex, std::move (t));
}

//==============================================================================
ClipRef findClip (const Project& p, const juce::Uuid& id)
{
    for (int t = 0; t < (int) p.tracks.size(); ++t)
    {
        auto& tr = p.tracks[(size_t) t];
        for (int c = 0; c < (int) tr.clips.size(); ++c)
            if (tr.clips[(size_t) c].id == id) return { t, c, false };
        for (int c = 0; c < (int) tr.midiClips.size(); ++c)
            if (tr.midiClips[(size_t) c].id == id) return { t, c, true };
    }
    return {};
}

static void sortClips (Track& t)
{
    std::stable_sort (t.clips.begin(), t.clips.end(), [] (auto& a, auto& b) { return a.start < b.start; });
    std::stable_sort (t.midiClips.begin(), t.midiClips.end(), [] (auto& a, auto& b) { return a.start < b.start; });
}

void addClip (Project& p, const juce::Uuid& trackId, AudioClip c)
{
    if (auto* t = p.findTrack (trackId)) { t->clips.push_back (std::move (c)); sortClips (*t); }
}

void addMidiClip (Project& p, const juce::Uuid& trackId, MidiClip c)
{
    if (auto* t = p.findTrack (trackId)) { c.sortNotes(); t->midiClips.push_back (std::move (c)); sortClips (*t); }
}

MidiClip* findMidiClip (Project& p, const juce::Uuid& id)
{
    auto ref = findClip (p, id);
    if (! ref || ! ref.isMidi) return nullptr;
    return &p.tracks[(size_t) ref.track].midiClips[(size_t) ref.clip];
}

void removeClip (Project& p, const juce::Uuid& id)
{
    auto ref = findClip (p, id);
    if (! ref) return;
    auto& t = p.tracks[(size_t) ref.track];
    if (ref.isMidi) t.midiClips.erase (t.midiClips.begin() + ref.clip);
    else            t.clips.erase (t.clips.begin() + ref.clip);
}

void moveClip (Project& p, const juce::Uuid& id, Beats newStart, std::optional<juce::Uuid> toTrack)
{
    auto ref = findClip (p, id);
    if (! ref) return;
    auto& src = p.tracks[(size_t) ref.track];
    auto* dst = toTrack ? p.findTrack (*toTrack) : nullptr;
    // El MIDI solo puede ir a pistas de instrumento; el audio, a pistas que no lo son.
    if (dst != nullptr && dst->isInstrument() != ref.isMidi) dst = nullptr;
    newStart = juce::jmax (0.0, newStart);

    if (ref.isMidi)
    {
        auto clip = src.midiClips[(size_t) ref.clip];
        clip.start = newStart;
        if (dst != nullptr && dst != &src)
        {
            src.midiClips.erase (src.midiClips.begin() + ref.clip);
            dst->midiClips.push_back (clip);
            sortClips (*dst);
        }
        else { src.midiClips[(size_t) ref.clip] = clip; sortClips (src); }
    }
    else
    {
        auto clip = src.clips[(size_t) ref.clip];
        clip.start = newStart;
        if (dst != nullptr && dst != &src)
        {
            src.clips.erase (src.clips.begin() + ref.clip);
            dst->clips.push_back (clip);
            sortClips (*dst);
        }
        else { src.clips[(size_t) ref.clip] = clip; sortClips (src); }
    }
}

void trimClipStart (Project& p, const juce::Uuid& id, Beats newStart)
{
    auto ref = findClip (p, id);
    if (! ref) return;
    const auto tl = p.timeline();
    auto& t = p.tracks[(size_t) ref.track];
    if (ref.isMidi)
    {
        auto& c = t.midiClips[(size_t) ref.clip];
        const auto s = juce::jlimit (0.0, c.end() - 0.01, newStart);
        const auto delta = s - c.start;
        for (auto& n : c.notes) n.start -= delta;
        for (auto& cc : c.controls) cc.time -= delta;
        c.notes.erase (std::remove_if (c.notes.begin(), c.notes.end(), [] (auto& n) { return n.start < -epsilon; }), c.notes.end());
        c.controls.erase (std::remove_if (c.controls.begin(), c.controls.end(), [] (auto& x) { return x.time < -epsilon; }), c.controls.end());
        c.start = s;
        c.length -= delta;
        return;
    }
    auto& c = t.clips[(size_t) ref.clip];
    const auto earliest = juce::jmax (0.0, c.start - tl.toBeats (c.offset));
    const auto latest = p.audioClipEnd (c) - tl.toBeats (0.01);
    const auto s = juce::jlimit (earliest, latest, newStart);
    const auto deltaSec = tl.toSeconds (s - c.start);
    c.start = s;
    c.offset = juce::jmax (0.0, c.offset + deltaSec);
    c.length -= deltaSec;
}

void trimClipEnd (Project& p, const juce::Uuid& id, Beats newEnd)
{
    auto ref = findClip (p, id);
    if (! ref) return;
    const auto tl = p.timeline();
    auto& t = p.tracks[(size_t) ref.track];
    if (ref.isMidi)
    {
        auto& c = t.midiClips[(size_t) ref.clip];
        c.length = juce::jmax (0.01, newEnd - c.start);
        return;
    }
    auto& c = t.clips[(size_t) ref.clip];
    const auto maxEnd = c.start + tl.toBeats (c.sourceLength - c.offset);
    const auto e = juce::jlimit (c.start + tl.toBeats (0.01), maxEnd, newEnd);
    c.length = tl.toSeconds (e - c.start);
}

static MidiClip splitMidi (MidiClip& left, Beats at)
{
    auto right = left;
    right.id = juce::Uuid();
    const auto cut = at - left.start;
    right.start = at;
    right.length = left.length - cut;
    left.length = cut;
    right.notes.clear();
    right.controls.clear();
    std::vector<MidiNote> keep;
    for (auto n : left.notes)
    {
        if (n.start >= cut - epsilon) { n.start -= cut; n.id = juce::Uuid(); right.notes.push_back (n); }
        else { n.length = juce::jmin (n.length, cut - n.start); keep.push_back (n); }
    }
    left.notes = keep;
    std::vector<MidiControl> keepC;
    for (auto c : left.controls)
    {
        if (c.time >= cut - epsilon) { c.time -= cut; right.controls.push_back (c); }
        else keepC.push_back (c);
    }
    left.controls = keepC;
    return right;
}

std::optional<juce::Uuid> splitClip (Project& p, const juce::Uuid& id, Beats at)
{
    auto ref = findClip (p, id);
    if (! ref) return {};
    auto& t = p.tracks[(size_t) ref.track];
    if (ref.isMidi)
    {
        auto& c = t.midiClips[(size_t) ref.clip];
        if (at <= c.start + epsilon || at >= c.end() - epsilon) return {};
        auto right = splitMidi (c, at);
        auto rid = right.id;
        t.midiClips.insert (t.midiClips.begin() + ref.clip + 1, std::move (right));
        return rid;
    }
    auto& c = t.clips[(size_t) ref.clip];
    if (at <= c.start + epsilon || at >= p.audioClipEnd (c) - epsilon) return {};
    const auto leftLen = p.timeline().toSeconds (at - c.start);
    auto right = c;
    right.id = juce::Uuid();
    right.start = at;
    right.offset = c.offset + leftLen;
    right.length = c.length - leftLen;
    c.length = leftLen;
    auto rid = right.id;
    t.clips.insert (t.clips.begin() + ref.clip + 1, std::move (right));
    return rid;
}

void splitAll (Project& p, Beats at)
{
    std::vector<juce::Uuid> ids;
    for (auto& t : p.tracks)
    {
        for (auto& c : t.clips) if (c.start < at && p.audioClipEnd (c) > at) ids.push_back (c.id);
        for (auto& c : t.midiClips) if (c.start < at && c.end() > at) ids.push_back (c.id);
    }
    for (auto& id : ids) splitClip (p, id, at);
}

std::optional<juce::Uuid> duplicateClip (Project& p, const juce::Uuid& id)
{
    auto ref = findClip (p, id);
    if (! ref) return {};
    auto& t = p.tracks[(size_t) ref.track];
    if (ref.isMidi)
    {
        auto copy = t.midiClips[(size_t) ref.clip];
        copy.id = juce::Uuid();
        copy.start = copy.end();
        for (auto& n : copy.notes) n.id = juce::Uuid();
        t.midiClips.push_back (copy);
        sortClips (t);
        return copy.id;
    }
    auto copy = t.clips[(size_t) ref.clip];
    copy.id = juce::Uuid();
    copy.start = p.audioClipEnd (copy);
    t.clips.push_back (copy);
    sortClips (t);
    return copy.id;
}

void clearRange (Project& p, const juce::Uuid& trackId, Beats from, Beats to)
{
    auto* t = p.findTrack (trackId);
    if (t == nullptr || to <= from) return;
    // Dividir en los bordes y quitar lo que queda adentro.
    std::vector<juce::Uuid> ids;
    for (auto& c : t->clips) ids.push_back (c.id);
    for (auto& c : t->midiClips) ids.push_back (c.id);
    for (auto& id : ids) { splitClip (p, id, from); }
    ids.clear();
    t = p.findTrack (trackId);
    for (auto& c : t->clips) ids.push_back (c.id);
    for (auto& c : t->midiClips) ids.push_back (c.id);
    for (auto& id : ids) { splitClip (p, id, to); }
    t = p.findTrack (trackId);
    t->clips.erase (std::remove_if (t->clips.begin(), t->clips.end(), [&] (auto& c)
                    { return c.start >= from - epsilon && p.audioClipEnd (c) <= to + epsilon; }), t->clips.end());
    t->midiClips.erase (std::remove_if (t->midiClips.begin(), t->midiClips.end(), [&] (auto& c)
                        { return c.start >= from - epsilon && c.end() <= to + epsilon; }), t->midiClips.end());
}

//==============================================================================
Clipboard copyClips (const Project& p, const std::vector<juce::Uuid>& ids)
{
    Clipboard cb;
    Beats first = std::numeric_limits<double>::max();
    int firstTrack = std::numeric_limits<int>::max();
    for (auto& id : ids)
        if (auto ref = findClip (p, id))
        {
            auto& t = p.tracks[(size_t) ref.track];
            first = juce::jmin (first, ref.isMidi ? t.midiClips[(size_t) ref.clip].start : t.clips[(size_t) ref.clip].start);
            firstTrack = juce::jmin (firstTrack, ref.track);
        }
    for (auto& id : ids)
        if (auto ref = findClip (p, id))
        {
            auto& t = p.tracks[(size_t) ref.track];
            Clipboard::Entry e;
            e.trackOffset = ref.track - firstTrack;
            if (ref.isMidi) { auto c = t.midiClips[(size_t) ref.clip]; c.start -= first; e.midi = c; }
            else            { auto c = t.clips[(size_t) ref.clip]; c.start -= first; e.audio = c; }
            cb.entries.push_back (std::move (e));
        }
    return cb;
}

std::vector<juce::Uuid> paste (Project& p, const Clipboard& cb, Beats at, int firstTrack)
{
    std::vector<juce::Uuid> created;
    for (auto& e : cb.entries)
    {
        const auto ti = firstTrack + e.trackOffset;
        if (ti < 0 || ti >= (int) p.tracks.size()) continue;
        auto& t = p.tracks[(size_t) ti];
        if (e.audio && ! t.isInstrument())
        {
            auto c = *e.audio; c.id = juce::Uuid(); c.start += at;
            created.push_back (c.id); t.clips.push_back (c);
        }
        else if (e.midi && t.isInstrument())
        {
            auto c = *e.midi; c.id = juce::Uuid(); c.start += at;
            for (auto& n : c.notes) n.id = juce::Uuid();
            created.push_back (c.id); t.midiClips.push_back (c);
        }
        sortClips (t);
    }
    return created;
}

//==============================================================================
Marker& addMarker (Project& p, Beats at, const juce::String& name)
{
    int n = 1;
    for (bool used = true; used; ++n)
    {
        used = false;
        for (auto& m : p.markers) if (m.number == n) used = true;
        if (! used) break;
    }
    Marker m;
    m.number = n;
    m.name = name;
    m.position = juce::jmax (0.0, at);
    p.markers.push_back (m);
    std::stable_sort (p.markers.begin(), p.markers.end(), [] (auto& a, auto& b) { return a.position < b.position; });
    for (auto& x : p.markers) if (x.id == m.id) return x;
    return p.markers.back();
}

void removeMarker (Project& p, const juce::Uuid& id)
{
    p.markers.erase (std::remove_if (p.markers.begin(), p.markers.end(), [&] (auto& m) { return m.id == id; }), p.markers.end());
}

const Marker* markerNumber (const Project& p, int number)
{
    for (auto& m : p.markers) if (m.number == number) return &m;
    return nullptr;
}

Section& addSection (Project& p, SectionKind kind, int bars, std::optional<Beats> at)
{
    Beats start = 0;
    if (at) start = *at;
    else for (auto& s : p.sections) start = juce::jmax (start, s.end());
    Section s;
    s.kind = kind;
    s.name = displayName (kind);
    s.start = start;
    s.length = juce::jmax (1, bars) * p.beatsPerBar;
    p.sections.push_back (s);
    std::stable_sort (p.sections.begin(), p.sections.end(), [] (auto& a, auto& b) { return a.start < b.start; });
    for (auto& x : p.sections) if (x.id == s.id) return x;
    return p.sections.back();
}

const Section* sectionAt (const Project& p, Beats b)
{
    const Section* found = nullptr;
    for (auto& s : p.sections) if (s.start <= b + epsilon && b < s.end() - epsilon) found = &s;
    return found;
}

std::optional<Beats> nextStop (const Project& p, Beats after)
{
    std::optional<Beats> best;
    auto consider = [&] (Beats b) { if (b > after + 0.01 && (! best || b < *best)) best = b; };
    for (auto& m : p.markers) consider (m.position);
    for (auto& s : p.sections) consider (s.start);
    return best;
}

std::optional<Beats> previousStop (const Project& p, Beats before)
{
    std::optional<Beats> best;
    auto consider = [&] (Beats b) { if (b < before - 0.01 && (! best || b > *best)) best = b; };
    for (auto& m : p.markers) consider (m.position);
    for (auto& s : p.sections) consider (s.start);
    return best;
}

void setTempo (Project& p, double bpm)
{
    p.bpm = juce::jlimit (20.0, 300.0, bpm);
}

//==============================================================================
namespace midi
{
    void quantize (MidiClip& c, Beats clipStart, const std::vector<juce::Uuid>& ids, Beats grid, double strength, bool lengths)
    {
        if (grid <= 0) return;
        const auto s = juce::jlimit (0.0, 1.0, strength);
        for (auto& n : c.notes)
        {
            if (! contains (ids, n.id)) continue;
            const auto absolute = clipStart + n.start;
            const auto target = std::round (absolute / grid) * grid;
            n.start = juce::jmax (0.0, absolute + (target - absolute) * s - clipStart);
            if (lengths)
            {
                const auto q = juce::jmax (grid, std::round (n.length / grid) * grid);
                n.length += (q - n.length) * s;
            }
        }
        c.sortNotes();
    }

    void transpose (MidiClip& c, const std::vector<juce::Uuid>& ids, int semitones)
    {
        for (auto& n : c.notes) if (contains (ids, n.id)) n.pitch = juce::jlimit (0, 127, n.pitch + semitones);
    }

    void legato (MidiClip& c, const std::vector<juce::Uuid>& ids)
    {
        c.sortNotes();
        std::vector<Beats> starts;
        for (auto& n : c.notes) starts.push_back (n.start);
        std::sort (starts.begin(), starts.end());
        for (auto& n : c.notes)
        {
            if (! contains (ids, n.id)) continue;
            auto next = std::upper_bound (starts.begin(), starts.end(), n.start + epsilon);
            n.length = next != starts.end() ? *next - n.start : juce::jmax (n.length, c.length - n.start);
        }
    }

    void setVelocity (MidiClip& c, const std::vector<juce::Uuid>& ids, int velocity)
    {
        for (auto& n : c.notes) if (contains (ids, n.id)) n.velocity = juce::jlimit (1, 127, velocity);
    }

    void move (MidiClip& c, const std::vector<juce::Uuid>& ids, Beats delta, int semitones)
    {
        for (auto& n : c.notes)
            if (contains (ids, n.id))
            {
                n.start = juce::jmax (0.0, n.start + delta);
                n.pitch = juce::jlimit (0, 127, n.pitch + semitones);
            }
        c.sortNotes();
        c.fitToNotes();
    }

    void resize (MidiClip& c, const std::vector<juce::Uuid>& ids, Beats delta, Beats minimum)
    {
        for (auto& n : c.notes) if (contains (ids, n.id)) n.length = juce::jmax (minimum, n.length + delta);
        c.fitToNotes();
    }

    void remove (MidiClip& c, const std::vector<juce::Uuid>& ids)
    {
        c.notes.erase (std::remove_if (c.notes.begin(), c.notes.end(), [&] (auto& n)
                       { return std::find (ids.begin(), ids.end(), n.id) != ids.end(); }), c.notes.end());
    }

    std::vector<juce::Uuid> duplicate (MidiClip& c, const std::vector<juce::Uuid>& ids)
    {
        std::vector<MidiNote> chosen;
        for (auto& n : c.notes) if (std::find (ids.begin(), ids.end(), n.id) != ids.end()) chosen.push_back (n);
        if (chosen.empty()) return {};
        Beats first = chosen[0].start, last = 0;
        for (auto& n : chosen) { first = juce::jmin (first, n.start); last = juce::jmax (last, n.end()); }
        std::vector<juce::Uuid> created;
        for (auto n : chosen)
        {
            n.id = juce::Uuid();
            n.start += last - first;
            created.push_back (n.id);
            c.notes.push_back (n);
        }
        c.sortNotes();
        c.fitToNotes();
        return created;
    }

    juce::Uuid addNote (MidiClip& c, int pitch, Beats at, Beats length, int velocity)
    {
        MidiNote n;
        n.pitch = juce::jlimit (0, 127, pitch);
        n.velocity = juce::jlimit (1, 127, velocity);
        n.start = juce::jmax (0.0, at);
        n.length = juce::jmax (0.01, length);
        c.notes.push_back (n);
        c.sortNotes();
        c.fitToNotes();
        return n.id;
    }

    juce::String noteName (int pitch)
    {
        static const char* names[] = { "Do", "Do#", "Re", "Re#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si" };
        return juce::String (names[((pitch % 12) + 12) % 12]) + juce::String (pitch / 12 - 1);
    }

    bool isBlackKey (int pitch)
    {
        const auto k = ((pitch % 12) + 12) % 12;
        return k == 1 || k == 3 || k == 6 || k == 8 || k == 10;
    }
}

//==============================================================================
juce::String displayName (Template t)
{
    switch (t)
    {
        case Template::empty:       return juce::String::fromUTF8 ("Vacío");
        case Template::fullBand:    return "Banda completa";
        case Template::acoustic:    return juce::String::fromUTF8 ("Acústico");
        case Template::vocals:      return "Voces";
        case Template::multitracks: return "Multitracks en vivo";
    }
    return {};
}

juce::String subtitle (Template t)
{
    switch (t)
    {
        case Template::empty:       return "Empieza desde cero";
        case Template::fullBand:    return juce::String::fromUTF8 ("Batería, bajo, guitarras, keys, pad y voces");
        case Template::acoustic:    return juce::String::fromUTF8 ("Guitarra acústica, piano y voces");
        case Template::vocals:      return juce::String::fromUTF8 ("Voz líder, coros y pad");
        case Template::multitracks: return juce::String::fromUTF8 ("Click, guía y stems para el domingo");
    }
    return {};
}

Project makeProject (Template tmpl, const juce::String& name, double bpm, const juce::String& key, int beatsPerBar)
{
    Project p;
    p.name = name;
    p.bpm = bpm;
    p.key = key;
    p.beatsPerBar = beatsPerBar;

    struct Spec { const char* name; const char* colour; TrackRole role; };
    std::vector<Spec> specs;
    switch (tmpl)
    {
        case Template::empty:       specs = { { "Audio 1", "violet", TrackRole::audio } }; break;
        case Template::fullBand:    specs = { { "Batería", "coral", TrackRole::audio }, { "Bajo", "amber", TrackRole::audio }, // utf8-ok (se convierte con fromUTF8)
                                              { "Guitarra Eléctrica", "rose", TrackRole::audio }, { "Guitarra Acústica", "lime", TrackRole::audio }, // utf8-ok (se convierte con fromUTF8)
                                              { "Keys", "sky", TrackRole::instrument }, { "Pad", "indigo", TrackRole::instrument },
                                              { "Voz Líder", "violet", TrackRole::audio }, { "Coros", "teal", TrackRole::audio } }; break; // utf8-ok (se convierte con fromUTF8)
        case Template::acoustic:    specs = { { "Guitarra Acústica", "lime", TrackRole::audio }, { "Piano", "sky", TrackRole::instrument }, // utf8-ok (se convierte con fromUTF8)
                                              { "Pad", "indigo", TrackRole::instrument }, { "Voz Líder", "violet", TrackRole::audio }, // utf8-ok (se convierte con fromUTF8)
                                              { "Coros", "teal", TrackRole::audio } }; break;
        case Template::vocals:      specs = { { "Voz Líder", "violet", TrackRole::audio }, { "Coro 1", "teal", TrackRole::audio }, // utf8-ok (se convierte con fromUTF8)
                                              { "Coro 2", "mint", TrackRole::audio }, { "Pad", "indigo", TrackRole::instrument } }; break;
        case Template::multitracks: specs = { { "Guía", "slate", TrackRole::guide }, { "Batería", "coral", TrackRole::audio }, // utf8-ok (se convierte con fromUTF8)
                                              { "Percusión", "amber", TrackRole::audio }, { "Bajo", "amber", TrackRole::audio }, // utf8-ok (se convierte con fromUTF8)
                                              { "Guitarras", "rose", TrackRole::audio }, { "Keys", "sky", TrackRole::audio },
                                              { "Pad", "indigo", TrackRole::audio }, { "Coros", "teal", TrackRole::audio } }; break;
    }
    for (auto& s : specs)
    {
        auto& t = addTrack (p, juce::String::fromUTF8 (s.name), s.role, s.colour);
        if (s.role == TrackRole::guide) t.directOutPair = 0;
    }
    if (tmpl == Template::multitracks)
        p.clickOutPair = 0;

    // Bus de reverb para las voces (con la "Reverb básica" incluida), como una plantilla típica de REAPER.
    if (tmpl == Template::fullBand || tmpl == Template::acoustic || tmpl == Template::vocals)
    {
        std::vector<juce::Uuid> voices;
        for (auto& t : p.tracks)
            if (t.name.startsWith ("Voz") || t.name.startsWith ("Coro")) voices.push_back (t.id);
        auto& bus = addTrack (p, "Reverb", TrackRole::bus, "slate");
        PluginSlot reverb;
        reverb.name = juce::String::fromUTF8 ("Reverb básica");
        reverb.manufacturer = "Worship DAW";
        reverb.descriptionXml = "internal:reverb";
        bus.inserts.push_back (reverb);
        const auto busId = bus.id;
        for (auto& id : voices) addSend (p, id, busId, -14.0);
    }

    if (tmpl != Template::empty && tmpl != Template::vocals)
    {
        const std::pair<SectionKind, int> plan[] = { { SectionKind::intro, 4 }, { SectionKind::verse, 8 }, { SectionKind::preChorus, 4 },
                                                     { SectionKind::chorus, 8 }, { SectionKind::verse, 8 }, { SectionKind::preChorus, 4 },
                                                     { SectionKind::chorus, 8 }, { SectionKind::bridge, 8 }, { SectionKind::chorus, 8 },
                                                     { SectionKind::outro, 4 } };
        std::map<SectionKind, int> counts;
        for (auto& [kind, bars] : plan)
        {
            auto& s = addSection (p, kind, bars);
            const auto n = ++counts[kind];
            if (n > 1 || kind == SectionKind::verse) s.name = displayName (kind) + " " + juce::String (n);
        }
    }
    return p;
}

} // namespace wdaw::edit
