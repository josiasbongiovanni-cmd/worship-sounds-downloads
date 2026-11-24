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
    switch (r) { case TrackRole::guide: return "guide"; case TrackRole::instrument: return "instrument"; default: return "audio"; }
}

juce::String toString (MonitorMode m)
{
    switch (m) { case MonitorMode::on: return "on"; case MonitorMode::autoTape: return "auto"; default: return "off"; }
}

static const char* sectionIds[] = { "intro", "verse", "preChorus", "chorus", "bridge", "interlude", "tag", "vamp", "outro" };

juce::String toString (SectionKind k)                 { return sectionIds[(int) k]; }
TrackRole trackRoleFromString (const juce::String& s) { return s == "guide" ? TrackRole::guide : s == "instrument" ? TrackRole::instrument : TrackRole::audio; }
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
    return hasSolo() ? t.solo : true;
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
