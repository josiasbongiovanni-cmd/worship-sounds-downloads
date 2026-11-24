#include "Serialization.h"
#include "Editing.h"

namespace wdaw::io
{

using juce::var;

static var obj() { return var (new juce::DynamicObject()); }
static void set (var& o, const juce::Identifier& k, const var& v) { o.getDynamicObject()->setProperty (k, v); }

static double num (const var& o, const char* k, double def = 0)
{
    auto v = o.getProperty (k, var());
    return v.isVoid() ? def : (double) v;
}

static int integer (const var& o, const char* k, int def = 0)
{
    auto v = o.getProperty (k, var());
    return v.isVoid() ? def : (int) v;
}

static bool flag (const var& o, const char* k, bool def = false)
{
    auto v = o.getProperty (k, var());
    return v.isVoid() ? def : (bool) v;
}

static juce::String str (const var& o, const char* k, const juce::String& def = {})
{
    auto v = o.getProperty (k, var());
    return v.isVoid() ? def : v.toString();
}

static juce::Uuid uuid (const var& o, const char* k = "id")
{
    auto s = str (o, k);
    return s.isEmpty() ? juce::Uuid() : juce::Uuid (s);
}

static juce::Time time (const var& o, const char* k)
{
    auto s = str (o, k);
    return s.isEmpty() ? juce::Time::getCurrentTime() : juce::Time::fromISO8601 (s);
}

//==============================================================================
static var pluginToVar (const PluginSlot& s)
{
    auto o = obj();
    set (o, "id", s.id.toDashedString());
    set (o, "name", s.name);
    set (o, "manufacturer", s.manufacturer);
    set (o, "description", s.descriptionXml);
    set (o, "state", s.state.toBase64Encoding());
    set (o, "bypassed", s.bypassed);
    return o;
}

static PluginSlot pluginFromVar (const var& o)
{
    PluginSlot s;
    s.id = uuid (o);
    s.name = str (o, "name");
    s.manufacturer = str (o, "manufacturer");
    s.descriptionXml = str (o, "description");
    s.state.fromBase64Encoding (str (o, "state"));
    s.bypassed = flag (o, "bypassed");
    return s;
}

static var list (const juce::Array<var>& a) { return var (a); }

var toVar (const Project& p)
{
    auto root = obj();
    set (root, "version", Project::currentVersion);
    set (root, "id", p.id.toDashedString());
    set (root, "name", p.name);
    set (root, "bpm", p.bpm);
    set (root, "beatsPerBar", p.beatsPerBar);
    set (root, "key", p.key);
    set (root, "loopStart", p.loopStart);
    set (root, "loopEnd", p.loopEnd);
    set (root, "loopEnabled", p.loopEnabled);
    set (root, "clickEnabled", p.clickEnabled);
    set (root, "clickVolumeDb", p.clickVolumeDb);
    set (root, "clickOutPair", p.clickOutPair);
    set (root, "masterVolumeDb", p.masterVolumeDb);
    set (root, "created", p.created.toISO8601 (true));
    set (root, "modified", p.modified.toISO8601 (true));

    juce::Array<var> master;
    for (auto& s : p.masterInserts) master.add (pluginToVar (s));
    set (root, "masterInserts", list (master));

    juce::Array<var> tracks;
    for (auto& t : p.tracks)
    {
        auto o = obj();
        set (o, "id", t.id.toDashedString());
        set (o, "name", t.name);
        set (o, "colour", t.colour);
        set (o, "role", toString (t.role));
        set (o, "volumeDb", t.volumeDb);
        set (o, "pan", t.pan);
        set (o, "muted", t.muted);
        set (o, "solo", t.solo);
        set (o, "armed", t.armed);
        set (o, "inputKind", t.input.kind == TrackInput::Kind::off ? "off" : t.input.kind == TrackInput::Kind::mono ? "mono" : "stereo");
        set (o, "inputChannel", t.input.channel);
        set (o, "monitor", toString (t.monitor));
        set (o, "directOutPair", t.directOutPair);
        if (! t.outputBus.isNull()) set (o, "outputBus", t.outputBus.toDashedString());
        set (o, "automationMode", toString (t.automationMode));
        if (t.isFolder) { set (o, "folder", true); set (o, "folderCollapsed", t.folderCollapsed); }
        if (! t.parent.isNull()) set (o, "parent", t.parent.toDashedString());
        juce::Array<var> sends;
        for (auto& s : t.sends)
        {
            auto so = obj();
            set (so, "id", s.id.toDashedString()); set (so, "target", s.target.toDashedString());
            set (so, "levelDb", s.levelDb); set (so, "pan", s.pan); set (so, "preFader", s.preFader); set (so, "muted", s.muted);
            sends.add (so);
        }
        set (o, "sends", list (sends));
        juce::Array<var> lanes;
        for (auto& l : t.automation)
        {
            auto lo = obj();
            static const char* kinds[] = { "volume", "pan", "send", "plugin" };
            set (lo, "id", l.id.toDashedString());
            set (lo, "target", kinds[(int) l.target.kind]);
            if (! l.target.ref.isNull()) set (lo, "ref", l.target.ref.toDashedString());
            if (l.target.param >= 0) { set (lo, "param", l.target.param); set (lo, "paramName", l.target.paramName); }
            set (lo, "visible", l.visible); set (lo, "active", l.active);
            juce::Array<var> pts;
            for (auto& pt : l.points)
            {
                juce::Array<var> item { pt.time, pt.value };
                if (pt.shape != CurveShape::linear) { item.add (toString (pt.shape)); item.add (pt.tension); }
                pts.add (var (item));
            }
            set (lo, "points", list (pts));
            lanes.add (lo);
        }
        set (o, "automation", list (lanes));
        auto eq = obj();
        set (eq, "low", t.eq.lowGain); set (eq, "mid", t.eq.midGain); set (eq, "high", t.eq.highGain); set (eq, "enabled", t.eq.enabled);
        set (o, "eq", eq);
        juce::Array<var> inserts;
        for (auto& s : t.inserts) inserts.add (pluginToVar (s));
        set (o, "inserts", list (inserts));
        if (t.instrument) set (o, "instrument", pluginToVar (*t.instrument));

        juce::Array<var> clips;
        for (auto& c : t.clips)
        {
            auto co = obj();
            set (co, "id", c.id.toDashedString()); set (co, "name", c.name); set (co, "file", c.file);
            set (co, "start", c.start); set (co, "offset", c.offset); set (co, "length", c.length);
            set (co, "sourceLength", c.sourceLength); set (co, "gainDb", c.gainDb);
            set (co, "fadeIn", c.fadeIn); set (co, "fadeOut", c.fadeOut);
            clips.add (co);
        }
        set (o, "clips", list (clips));

        juce::Array<var> midiClips;
        for (auto& c : t.midiClips)
        {
            auto co = obj();
            set (co, "id", c.id.toDashedString()); set (co, "name", c.name);
            set (co, "start", c.start); set (co, "length", c.length);
            juce::Array<var> notes;
            for (auto& n : c.notes)
            {
                // Compacto: [pitch, velocity, channel, start, length]
                notes.add (var (juce::Array<var> { n.pitch, n.velocity, n.channel, n.start, n.length }));
            }
            set (co, "notes", list (notes));
            juce::Array<var> controls;
            for (auto& x : c.controls)
                controls.add (var (juce::Array<var> { x.time, x.channel, x.controller, x.value }));
            set (co, "controls", list (controls));
            midiClips.add (co);
        }
        set (o, "midiClips", list (midiClips));
        tracks.add (o);
    }
    set (root, "tracks", list (tracks));

    juce::Array<var> sections;
    for (auto& s : p.sections)
    {
        auto o = obj();
        set (o, "id", s.id.toDashedString()); set (o, "name", s.name); set (o, "kind", toString (s.kind));
        set (o, "start", s.start); set (o, "length", s.length);
        sections.add (o);
    }
    set (root, "sections", list (sections));

    juce::Array<var> markers;
    for (auto& m : p.markers)
    {
        auto o = obj();
        set (o, "id", m.id.toDashedString()); set (o, "number", m.number); set (o, "name", m.name); set (o, "position", m.position);
        markers.add (o);
    }
    set (root, "markers", list (markers));
    return root;
}

//==============================================================================
/** Proyectos de la versión Swift (v1): todo en segundos. Se convierte a beats con su BPM. */
static Project fromVarV1 (const var& root)
{
    Project p;
    p.id = uuid (root);
    p.name = str (root, "name", "Proyecto");
    p.bpm = num (root, "bpm", 120);
    p.beatsPerBar = integer (root, "beatsPerBar", 4);
    p.key = str (root, "key", "C");
    const auto tl = p.timeline();
    auto beats = [&] (double s) { return tl.toBeats (s); };
    if (auto* loop = root.getProperty ("loop", var()).getDynamicObject())
    {
        var l (loop);
        p.loopStart = beats (num (l, "start"));
        p.loopEnd = beats (num (l, "end"));
        p.loopEnabled = flag (l, "isEnabled");
    }
    p.clickEnabled = flag (root, "isClickEnabled", true);
    p.clickVolumeDb = num (root, "clickVolumeDB", -6);
    p.masterVolumeDb = num (root, "masterVolumeDB", 0);

    if (auto* arr = root.getProperty ("tracks", var()).getArray())
        for (auto& to : *arr)
        {
            Track t;
            t.id = uuid (to);
            t.name = str (to, "name");
            t.colour = str (to, "color", "violet");
            t.role = trackRoleFromString (str (to, "role", "audio"));
            t.volumeDb = num (to, "volumeDB");
            t.pan = num (to, "pan");
            t.muted = flag (to, "isMuted"); t.solo = flag (to, "isSolo"); t.armed = flag (to, "isArmed");
            t.monitor = monitorModeFromString (str (to, "monitor", "off"));
            if (auto* clips = to.getProperty ("clips", var()).getArray())
                for (auto& co : *clips)
                {
                    AudioClip c;
                    c.id = uuid (co);
                    c.name = str (co, "name");
                    c.file = str (co, "fileName");
                    c.start = beats (num (co, "start"));
                    c.offset = num (co, "offset");
                    c.length = num (co, "duration");
                    c.sourceLength = num (co, "sourceDuration", c.offset + c.length);
                    c.gainDb = (float) num (co, "gainDB");
                    t.clips.push_back (c);
                }
            if (auto* clips = to.getProperty ("midiClips", var()).getArray())
                for (auto& co : *clips)
                {
                    MidiClip c;
                    c.id = uuid (co);
                    c.name = str (co, "name");
                    c.start = beats (num (co, "start"));
                    c.length = beats (num (co, "duration"));
                    if (auto* notes = co.getProperty ("notes", var()).getArray())
                        for (auto& no : *notes)
                        {
                            MidiNote n;
                            n.pitch = integer (no, "pitch", 60);
                            n.velocity = integer (no, "velocity", 100);
                            n.channel = integer (no, "channel", 1);
                            n.start = beats (num (no, "start"));
                            n.length = beats (num (no, "duration"));
                            c.notes.push_back (n);
                        }
                    t.midiClips.push_back (c);
                }
            p.tracks.push_back (t);
        }
    if (auto* arr = root.getProperty ("sections", var()).getArray())
        for (auto& so : *arr)
        {
            Section s;
            s.id = uuid (so);
            s.name = str (so, "name");
            s.kind = sectionKindFromString (str (so, "kind", "verse"));
            s.start = beats (num (so, "start"));
            s.length = beats (num (so, "length"));
            p.sections.push_back (s);
        }
    if (auto* arr = root.getProperty ("markers", var()).getArray())
        for (auto& mo : *arr)
        {
            Marker m;
            m.id = uuid (mo);
            m.number = integer (mo, "number", 1);
            m.name = str (mo, "name");
            m.position = beats (num (mo, "time"));
            p.markers.push_back (m);
        }
    return p;
}

Project fromVar (const var& root)
{
    if (integer (root, "version", 1) < 2)
        return fromVarV1 (root);

    Project p;
    p.id = uuid (root);
    p.name = str (root, "name", "Proyecto");
    p.bpm = num (root, "bpm", 120);
    p.beatsPerBar = integer (root, "beatsPerBar", 4);
    p.key = str (root, "key", "C");
    p.loopStart = num (root, "loopStart");
    p.loopEnd = num (root, "loopEnd");
    p.loopEnabled = flag (root, "loopEnabled");
    p.clickEnabled = flag (root, "clickEnabled", true);
    p.clickVolumeDb = num (root, "clickVolumeDb", -6);
    p.clickOutPair = integer (root, "clickOutPair");
    p.masterVolumeDb = num (root, "masterVolumeDb");
    p.created = time (root, "created");
    p.modified = time (root, "modified");
    if (auto* arr = root.getProperty ("masterInserts", var()).getArray())
        for (auto& s : *arr) p.masterInserts.push_back (pluginFromVar (s));

    if (auto* arr = root.getProperty ("tracks", var()).getArray())
        for (auto& o : *arr)
        {
            Track t;
            t.id = uuid (o);
            t.name = str (o, "name");
            t.colour = str (o, "colour", "violet");
            t.role = trackRoleFromString (str (o, "role"));
            t.volumeDb = num (o, "volumeDb");
            t.pan = num (o, "pan");
            t.muted = flag (o, "muted"); t.solo = flag (o, "solo"); t.armed = flag (o, "armed");
            const auto kind = str (o, "inputKind", "mono");
            t.input.kind = kind == "off" ? TrackInput::Kind::off : kind == "stereo" ? TrackInput::Kind::stereo : TrackInput::Kind::mono;
            t.input.channel = integer (o, "inputChannel");
            t.monitor = monitorModeFromString (str (o, "monitor"));
            t.directOutPair = integer (o, "directOutPair");
            if (o.hasProperty ("outputBus")) t.outputBus = uuid (o, "outputBus");
            t.automationMode = automationModeFromString (str (o, "automationMode", "read"));
            t.isFolder = flag (o, "folder"); t.folderCollapsed = flag (o, "folderCollapsed");
            if (o.hasProperty ("parent")) t.parent = uuid (o, "parent");
            if (auto* sends = o.getProperty ("sends", var()).getArray())
                for (auto& so : *sends)
                {
                    Send s;
                    s.id = uuid (so); s.target = uuid (so, "target");
                    s.levelDb = num (so, "levelDb", -12); s.pan = num (so, "pan");
                    s.preFader = flag (so, "preFader"); s.muted = flag (so, "muted");
                    t.sends.push_back (s);
                }
            if (auto* lanes = o.getProperty ("automation", var()).getArray())
                for (auto& lo : *lanes)
                {
                    AutomationLane l;
                    l.id = uuid (lo);
                    const auto kind = str (lo, "target", "volume");
                    l.target.kind = kind == "pan" ? AutomationTarget::Kind::pan : kind == "send" ? AutomationTarget::Kind::sendLevel
                                  : kind == "plugin" ? AutomationTarget::Kind::pluginParam : AutomationTarget::Kind::volume;
                    if (lo.hasProperty ("ref")) l.target.ref = uuid (lo, "ref");
                    l.target.param = integer (lo, "param", -1);
                    l.target.paramName = str (lo, "paramName");
                    l.visible = flag (lo, "visible", true); l.active = flag (lo, "active", true);
                    if (auto* pts = lo.getProperty ("points", var()).getArray())
                        for (auto& pt : *pts)
                            if (pt.isArray() && pt.size() >= 2)
                            {
                                AutomationPoint ap { (double) pt[0], (double) pt[1] };
                                if (pt.size() >= 4) { ap.shape = curveShapeFromString (pt[2].toString()); ap.tension = (double) pt[3]; }
                                l.points.push_back (ap);
                            }
                    l.sortPoints();
                    t.automation.push_back (std::move (l));
                }
            auto eq = o.getProperty ("eq", var());
            t.eq.lowGain = (float) num (eq, "low"); t.eq.midGain = (float) num (eq, "mid");
            t.eq.highGain = (float) num (eq, "high"); t.eq.enabled = flag (eq, "enabled", true);
            if (auto* ins = o.getProperty ("inserts", var()).getArray())
                for (auto& s : *ins) t.inserts.push_back (pluginFromVar (s));
            auto inst = o.getProperty ("instrument", var());
            if (inst.isObject()) t.instrument = pluginFromVar (inst);

            if (auto* clips = o.getProperty ("clips", var()).getArray())
                for (auto& co : *clips)
                {
                    AudioClip c;
                    c.id = uuid (co); c.name = str (co, "name"); c.file = str (co, "file");
                    c.start = num (co, "start"); c.offset = num (co, "offset"); c.length = num (co, "length", 1);
                    c.sourceLength = num (co, "sourceLength", c.offset + c.length);
                    c.gainDb = (float) num (co, "gainDb");
                    c.fadeIn = num (co, "fadeIn", 0.005); c.fadeOut = num (co, "fadeOut", 0.005);
                    t.clips.push_back (c);
                }
            if (auto* clips = o.getProperty ("midiClips", var()).getArray())
                for (auto& co : *clips)
                {
                    MidiClip c;
                    c.id = uuid (co); c.name = str (co, "name");
                    c.start = num (co, "start"); c.length = num (co, "length", 4);
                    if (auto* notes = co.getProperty ("notes", var()).getArray())
                        for (auto& n : *notes)
                            if (auto* a = n.getArray(); a != nullptr && a->size() >= 5)
                            {
                                MidiNote mn;
                                mn.pitch = (*a)[0]; mn.velocity = (*a)[1]; mn.channel = (*a)[2];
                                mn.start = (*a)[3]; mn.length = (*a)[4];
                                c.notes.push_back (mn);
                            }
                    if (auto* ctrls = co.getProperty ("controls", var()).getArray())
                        for (auto& x : *ctrls)
                            if (auto* a = x.getArray(); a != nullptr && a->size() >= 4)
                                c.controls.push_back ({ (double) (*a)[0], (int) (*a)[1], (int) (*a)[2], (int) (*a)[3] });
                    t.midiClips.push_back (c);
                }
            p.tracks.push_back (t);
        }

    if (auto* arr = root.getProperty ("sections", var()).getArray())
        for (auto& o : *arr)
        {
            Section s;
            s.id = uuid (o); s.name = str (o, "name"); s.kind = sectionKindFromString (str (o, "kind"));
            s.start = num (o, "start"); s.length = num (o, "length", 32);
            p.sections.push_back (s);
        }
    if (auto* arr = root.getProperty ("markers", var()).getArray())
        for (auto& o : *arr)
        {
            Marker m;
            m.id = uuid (o); m.number = integer (o, "number", 1); m.name = str (o, "name"); m.position = num (o, "position");
            p.markers.push_back (m);
        }
    edit::normalizeFolders (p);
    return p;
}

juce::String toJSON (const Project& p)
{
    return juce::JSON::toString (toVar (p));
}

std::optional<Project> fromJSON (const juce::String& text)
{
    var parsed;
    if (juce::JSON::parse (text, parsed).failed() || ! parsed.isObject()) return {};
    return fromVar (parsed);
}

//==============================================================================
juce::File audioFolder (const juce::File& f)   { return f.getChildFile ("Audio"); }
juce::File exportsFolder (const juce::File& f) { return f.getChildFile ("Exports"); }

juce::Result save (const Project& p, const juce::File& folder)
{
    if (! folder.createDirectory()) return juce::Result::fail ("No se pudo crear la carpeta del proyecto.");
    audioFolder (folder).createDirectory();
    const auto file = folder.getChildFile ("project.json");
    const auto json = toJSON (p);

    // Copia anterior (.bak) y backups con fecha cada 10 minutos, como REAPER.
    if (file.existsAsFile())
    {
        file.copyFileTo (folder.getChildFile ("project.json.bak"));
        auto backups = folder.getChildFile ("Backups");
        backups.createDirectory();
        auto existing = backups.findChildFiles (juce::File::findFiles, false, "*.json");
        std::sort (existing.begin(), existing.end(), [] (auto& a, auto& b)
                   { return a.getLastModificationTime() > b.getLastModificationTime(); });
        const bool due = existing.isEmpty()
            || (juce::Time::getCurrentTime() - existing.getFirst().getLastModificationTime()).inMinutes() >= 10;
        if (due)
        {
            file.copyFileTo (backups.getChildFile ("project " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S") + ".json"));
            existing = backups.findChildFiles (juce::File::findFiles, false, "*.json");
            std::sort (existing.begin(), existing.end(), [] (auto& a, auto& b)
                       { return a.getLastModificationTime() > b.getLastModificationTime(); });
            for (int i = 10; i < existing.size(); ++i) existing.getReference (i).deleteFile();
        }
    }

    juce::TemporaryFile temp (file);
    if (! temp.getFile().replaceWithText (json) || ! temp.overwriteTargetFileWithTemporary())
        return juce::Result::fail ("No se pudo escribir project.json (¿disco lleno o sin permisos?)."_u);
    return juce::Result::ok();
}

std::optional<Project> load (const juce::File& folder, juce::String* error)
{
    const auto file = folder.getChildFile ("project.json");
    if (! file.existsAsFile())
    {
        if (error) *error = "No se encontró project.json."_u;
        return {};
    }
    if (auto p = fromJSON (file.loadFileAsString())) return p;
    // Archivo dañado: intentar la copia anterior.
    if (auto p = fromJSON (folder.getChildFile ("project.json.bak").loadFileAsString()))
    {
        if (error) *error = "El proyecto estaba dañado; se abrió la copia anterior."_u;
        return p;
    }
    if (error) *error = "El archivo del proyecto está dañado."_u;
    return {};
}

juce::String sanitize (const juce::String& name)
{
    auto s = name.removeCharacters ("/\\:?*|\"<>").trim();
    return s.isEmpty() ? juce::String::fromUTF8 ("Sin título") : s;
}

juce::File createProjectFolder (const juce::File& root, const juce::String& name)
{
    root.createDirectory();
    const auto base = sanitize (name);
    auto f = root.getChildFile (base + extension);
    for (int n = 2; f.exists(); ++n) f = root.getChildFile (base + " " + juce::String (n) + extension);
    f.createDirectory();
    audioFolder (f).createDirectory();
    return f;
}

std::vector<Entry> listProjects (const juce::File& root)
{
    std::vector<Entry> list;
    for (auto& f : root.findChildFiles (juce::File::findDirectories, false, "*" + extension))
        if (auto p = load (f))
            list.push_back ({ f, p->name, p->bpm, p->key, (int) p->tracks.size(),
                              f.getChildFile ("project.json").getLastModificationTime() });
    std::sort (list.begin(), list.end(), [] (auto& a, auto& b) { return a.modified > b.modified; });
    return list;
}

bool moveToTrash (const juce::File& folder)
{
    return folder.moveToTrash();
}

std::vector<juce::File> unusedAudioFiles (const Project& p, const juce::File& folder)
{
    const auto used = p.referencedAudioFiles();
    std::vector<juce::File> unused;
    for (auto& f : audioFolder (folder).findChildFiles (juce::File::findFiles, false))
        if (std::find (used.begin(), used.end(), f.getFileName()) == used.end())
            unused.push_back (f);
    return unused;
}

} // namespace wdaw::io
