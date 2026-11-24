#include "Model.h"

namespace wdaw
{

//==============================================================================
Beats Timeline::snap (Beats b, Beats division) noexcept
{
    if (division <= 0) return juce::jmax (0.0, b);
    return juce::jmax (0.0, std::round (b / division) * division);
}

Beats Timeline::snapDown (Beats b, Beats division) noexcept
{
    if (division <= 0) return juce::jmax (0.0, b);
    return juce::jmax (0.0, std::floor (b / division + 1.0e-9) * division);
}

juce::String Timeline::barBeatString (Beats b) const
{
    const auto total = juce::jmax (0.0, b) + 1.0e-9;
    const auto bar = (int) (total / beatsPerBar);
    const auto inBar = total - bar * beatsPerBar;
    const auto beat = (int) inBar;
    const auto tick = juce::jmin (99, (int) ((inBar - beat) * 100));
    return juce::String (bar + 1) + "." + juce::String (beat + 1) + "." + juce::String (tick).paddedLeft ('0', 2);
}

juce::String Timeline::timecode (double seconds)
{
    const auto s = juce::jmax (0.0, seconds);
    const auto minutes = (int) (s / 60);
    return juce::String (minutes) + ":" + juce::String (s - minutes * 60, 3).paddedLeft ('0', 6);
}

//==============================================================================
juce::String toString (TrackRole r)
{
    switch (r) { case TrackRole::guide: return "guide"; case TrackRole::instrument: return "instrument"; case TrackRole::bus: return "bus"; default: return "audio"; }
}

juce::String toString (AutomationMode m)
{
    switch (m) { case AutomationMode::off: return "off"; case AutomationMode::touch: return "touch"; case AutomationMode::latch: return "latch"; default: return "read"; }
}

AutomationMode automationModeFromString (const juce::String& s)
{
    return s == "off" ? AutomationMode::off : s == "touch" ? AutomationMode::touch : s == "latch" ? AutomationMode::latch : AutomationMode::read;
}

juce::String displayName (AutomationMode m)
{
    switch (m)
    {
        case AutomationMode::off:   return "Apagada (ignorar carriles)";
        case AutomationMode::touch: return "Touch (escribe mientras tocas)";
        case AutomationMode::latch: return "Latch (escribe desde que tocas hasta parar)";
        default:                    return "Leer";
    }
}

juce::String toString (MonitorMode m)
{
    switch (m) { case MonitorMode::on: return "on"; case MonitorMode::autoTape: return "auto"; default: return "off"; }
}

static const char* sectionIds[] = { "intro", "verse", "preChorus", "chorus", "bridge", "interlude", "tag", "vamp", "outro" };

juce::String toString (SectionKind k)                 { return sectionIds[(int) k]; }
TrackRole trackRoleFromString (const juce::String& s) { return s == "guide" ? TrackRole::guide : s == "instrument" ? TrackRole::instrument : s == "bus" ? TrackRole::bus : TrackRole::audio; }
MonitorMode monitorModeFromString (const juce::String& s) { return s == "on" ? MonitorMode::on : s == "auto" ? MonitorMode::autoTape : MonitorMode::off; }

SectionKind sectionKindFromString (const juce::String& s)
{
    for (int i = 0; i < (int) std::size (sectionIds); ++i)
        if (s == sectionIds[i]) return (SectionKind) i;
    return SectionKind::verse;
}

juce::String displayName (SectionKind k)
{
    static const char* names[] = { "Intro", "Verso", "Pre-Coro", "Coro", "Puente", "Interludio", "Tag", "Vamp", "Final" };
    return juce::String::fromUTF8 (names[(int) k]);
}

juce::String shortName (SectionKind k)
{
    static const char* names[] = { "In", "V", "PC", "C", "P", "Int", "T", "Vp", "F" };
    return names[(int) k];
}

juce::String defaultColourId (SectionKind k)
{
    static const char* ids[] = { "slate", "sky", "teal", "violet", "amber", "indigo", "rose", "coral", "slate" };
    return ids[(int) k];
}

juce::String displayName (MonitorMode m)
{
    switch (m)
    {
        case MonitorMode::on:       return "Encendido";
        case MonitorMode::autoTape: return juce::String::fromUTF8 ("Automático (estilo cinta)");
        default:                    return "Apagado";
    }
}

juce::String TrackInput::displayName() const
{
    switch (kind)
    {
        case Kind::off:    return "Sin entrada";
        case Kind::mono:   return "Entrada " + juce::String (channel + 1);
        default:           return "Entradas " + juce::String (channel + 1) + "/" + juce::String (channel + 2);
    }
}

juce::String TrackInput::shortName() const
{
    switch (kind)
    {
        case Kind::off:    return "-";
        case Kind::mono:   return "In " + juce::String (channel + 1);
        default:           return "In " + juce::String (channel + 1) + "/" + juce::String (channel + 2);
    }
}

//==============================================================================
std::pair<int, int> MidiClip::pitchRange() const
{
    if (notes.empty()) return { 48, 72 };
    int lo = 127, hi = 0;
    for (auto& n : notes) { lo = juce::jmin (lo, n.pitch); hi = juce::jmax (hi, n.pitch); }
    return { lo, juce::jmax (hi, lo + 11) };
}

void MidiClip::sortNotes()
{
    std::stable_sort (notes.begin(), notes.end(), [] (auto& a, auto& b)
                      { return a.start != b.start ? a.start < b.start : a.pitch < b.pitch; });
    std::stable_sort (controls.begin(), controls.end(), [] (auto& a, auto& b) { return a.time < b.time; });
}

void MidiClip::fitToNotes()
{
    for (auto& n : notes) length = juce::jmax (length, n.end());
}

//==============================================================================
Track* Project::findTrack (const juce::Uuid& id)
{
    for (auto& t : tracks) if (t.id == id) return &t;
    return nullptr;
}

const Track* Project::findTrack (const juce::Uuid& id) const
{
    for (auto& t : tracks) if (t.id == id) return &t;
    return nullptr;
}

int Project::trackIndex (const juce::Uuid& id) const
{
    for (int i = 0; i < (int) tracks.size(); ++i) if (tracks[(size_t) i].id == id) return i;
    return -1;
}

bool Project::hasSolo() const
{
    for (auto& t : tracks) if (t.solo) return true;
    return false;
}

bool Project::isAudible (const Track& t) const
{
    if (t.muted) return false;
    if (! hasSolo() || t.solo) return true;
    // Solo con buses: el bus de una pista en solo suena, y las pistas que alimentan a un bus en solo también.
    for (auto& s : tracks)
        if (s.solo && (feedsInto (s.id, t.id) || feedsInto (t.id, s.id))) return true;
    return false;
}

std::vector<juce::Uuid> Project::destinations (const Track& t) const
{
    std::vector<juce::Uuid> out;
    auto addIfBus = [&] (const juce::Uuid& id)
    {
        if (id.isNull() || id == t.id) return;
        if (auto* b = findTrack (id); b != nullptr && b->isBus() && std::find (out.begin(), out.end(), id) == out.end()) out.push_back (id);
    };
    if (t.directOutPair < 2) addIfBus (t.outputBus);
    for (auto& s : t.sends) addIfBus (s.target);
    return out;
}

bool Project::feedsInto (const juce::Uuid& from, const juce::Uuid& to) const
{
    std::vector<juce::Uuid> stack { from }, seen;
    while (! stack.empty())
    {
        const auto id = stack.back();
        stack.pop_back();
        if (std::find (seen.begin(), seen.end(), id) != seen.end()) continue;
        seen.push_back (id);
        if (auto* t = findTrack (id))
            for (auto& d : destinations (*t))
            {
                if (d == to) return true;
                stack.push_back (d);
            }
    }
    return false;
}

bool Project::canRouteTo (const juce::Uuid& from, const juce::Uuid& bus) const
{
    auto* b = findTrack (bus);
    return b != nullptr && b->isBus() && bus != from && ! feedsInto (bus, from);
}

std::vector<const Track*> Project::buses() const
{
    std::vector<const Track*> out;
    for (auto& t : tracks) if (t.isBus()) out.push_back (&t);
    return out;
}

//==============================================================================
double AutomationTarget::minValue() const noexcept
{
    switch (kind) { case Kind::volume: case Kind::sendLevel: return Decibels::silence; case Kind::pan: return -1; default: return 0; }
}
double AutomationTarget::maxValue() const noexcept
{
    switch (kind) { case Kind::volume: case Kind::sendLevel: return 6; default: return 1; }
}
double AutomationTarget::defaultValue() const noexcept
{
    switch (kind) { case Kind::volume: return 0; case Kind::sendLevel: return -12; case Kind::pan: return 0; default: return 0.5; }
}
double AutomationTarget::toNormalised (double v) const noexcept
{
    v = juce::jlimit (minValue(), maxValue(), v);
    if (kind == Kind::volume || kind == Kind::sendLevel) return Decibels::faderPosition (v);
    return (v - minValue()) / (maxValue() - minValue());
}
double AutomationTarget::fromNormalised (double n) const noexcept
{
    n = juce::jlimit (0.0, 1.0, n);
    if (kind == Kind::volume || kind == Kind::sendLevel) return Decibels::faderDb (n);
    return minValue() + n * (maxValue() - minValue());
}
juce::String AutomationTarget::formatValue (double v) const
{
    switch (kind)
    {
        case Kind::volume: case Kind::sendLevel:
            return v <= Decibels::silence ? "-∞ dB"_u : (v > 0 ? "+" : "") + juce::String (v, 1) + " dB";
        case Kind::pan:
            return std::abs (v) < 0.01 ? juce::String ("Centro") : juce::String ((int) std::round (std::abs (v) * 100)) + (v < 0 ? "% L" : "% R");
        default: return juce::String ((int) std::round (v * 100)) + " %";
    }
}

double AutomationLane::valueAt (Beats t) const noexcept
{
    if (points.empty()) return target.defaultValue();
    if (t <= points.front().time) return points.front().value;
    if (t >= points.back().time) return points.back().value;
    auto it = std::upper_bound (points.begin(), points.end(), t, [] (Beats x, const AutomationPoint& p) { return x < p.time; });
    const auto& b = *it;
    const auto& a = *(it - 1);
    if (b.time - a.time <= 1e-9) return b.value;
    return a.value + (b.value - a.value) * (t - a.time) / (b.time - a.time);
}

void AutomationLane::sortPoints()
{
    std::stable_sort (points.begin(), points.end(), [] (auto& a, auto& b) { return a.time < b.time; });
}

AutomationLane* Track::findLane (const AutomationTarget& t)
{
    for (auto& l : automation) if (l.target == t) return &l;
    return nullptr;
}
const AutomationLane* Track::findLane (const AutomationTarget& t) const
{
    for (auto& l : automation) if (l.target == t) return &l;
    return nullptr;
}
const AutomationLane* Track::playingLane (const AutomationTarget& t) const
{
    if (automationMode == AutomationMode::off) return nullptr;
    auto* l = findLane (t);
    return l != nullptr && l->active && ! l->writing && ! l->points.empty() ? l : nullptr;
}

Beats Project::audioClipEnd (const AudioClip& c) const
{
    return c.start + timeline().toBeats (c.length);
}

Beats Project::contentEnd() const
{
    Beats end = 0;
    for (auto& t : tracks)
    {
        for (auto& c : t.clips) end = juce::jmax (end, audioClipEnd (c));
        for (auto& c : t.midiClips) end = juce::jmax (end, c.end());
    }
    for (auto& s : sections) end = juce::jmax (end, s.end());
    return end;
}

std::vector<juce::String> Project::referencedAudioFiles() const
{
    std::vector<juce::String> files;
    for (auto& t : tracks)
        for (auto& c : t.clips)
            if (std::find (files.begin(), files.end(), c.file) == files.end())
                files.push_back (c.file);
    return files;
}

} // namespace wdaw
