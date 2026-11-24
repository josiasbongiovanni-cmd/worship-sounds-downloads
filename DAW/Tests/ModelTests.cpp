#include <juce_core/juce_core.h>
#include "Model/Editing.h"
#include "Model/Serialization.h"

using namespace wdaw;

static bool near (double a, double b, double tol = 1.0e-6) { return std::abs (a - b) < tol; }

struct TimelineTests : juce::UnitTest
{
    TimelineTests() : juce::UnitTest ("Timeline", "Model") {}
    void runTest() override
    {
        beginTest ("Compás y tiempo"_u);
        Timeline t { 72, 4 };
        expectEquals (t.barBeatString (0), juce::String ("1.1.00"));
        expectEquals (t.barBeatString (4), juce::String ("2.1.00"));
        expectEquals (t.barBeatString (6.5), juce::String ("2.3.50"));
        expect (near (t.toSeconds (72), 60));

        beginTest ("Snap");
        expect (near (Timeline::snap (1.26, 0.5), 1.5));
        expect (near (Timeline::snap (1.24, 0.5), 1.0));
        expect (near (Timeline::snapDown (1.99, 0.5), 1.5));
        expectEquals (Timeline::timecode (83.5), juce::String ("1:23.500"));
    }
};

struct EditingTests : juce::UnitTest
{
    EditingTests() : juce::UnitTest ("Edición"_u, "Model") {}

    Project withClip (juce::Uuid& trackId, juce::Uuid& clipId)
    {
        Project p;
        p.bpm = 120; // 0,5 s por beat
        auto& t = edit::addTrack (p, "Voz");
        trackId = t.id;
        AudioClip c;
        c.file = "a.wav"; c.start = 4; c.offset = 0; c.length = 5; c.sourceLength = 5; // 4 → 14 beats
        clipId = c.id;
        edit::addClip (p, trackId, c);
        return p;
    }

    void runTest() override
    {
        juce::Uuid tid, cid;

        beginTest ("Dividir audio");
        {
            auto p = withClip (tid, cid);
            auto right = edit::splitClip (p, cid, 6);   // 1 s dentro
            expect (right.has_value());
            auto& clips = p.tracks[0].clips;
            expectEquals ((int) clips.size(), 2);
            expect (near (clips[0].length, 1));
            expect (near (clips[1].offset, 1));
            expect (near (clips[1].length, 4));
            expect (! edit::splitClip (p, cid, 100).has_value());
        }

        beginTest ("Recortar respeta el archivo fuente");
        {
            auto p = withClip (tid, cid);
            edit::trimClipStart (p, cid, 6);
            expect (near (p.tracks[0].clips[0].offset, 1));
            edit::trimClipStart (p, cid, 0);            // no puede ir antes del inicio del archivo
            expect (near (p.tracks[0].clips[0].start, 4));
            edit::trimClipEnd (p, cid, 100);            // ni después del final
            expect (near (p.audioClipEnd (p.tracks[0].clips[0]), 14));
        }

        beginTest ("Grabar encima libera el rango (sin superposiciones)");
        {
            auto p = withClip (tid, cid);
            edit::clearRange (p, tid, 6, 8);
            auto& clips = p.tracks[0].clips;
            expectEquals ((int) clips.size(), 2);
            expect (near (p.audioClipEnd (clips[0]), 6));
            expect (near (clips[1].start, 8));
            expect (near (clips[1].offset, 2));
            edit::clearRange (p, tid, 0, 100);
            expect (p.tracks[0].clips.empty());
        }

        beginTest ("Copiar y pegar entre pistas");
        {
            auto p = withClip (tid, cid);
            auto& other = edit::addTrack (p, "Coros");
            auto otherId = other.id;
            auto cb = edit::copyClips (p, { cid });
            auto created = edit::paste (p, cb, 20, 1);
            expectEquals ((int) created.size(), 1);
            expect (near (p.findTrack (otherId)->clips[0].start, 20));
            expect (p.findTrack (otherId)->clips[0].id != cid);
        }

        beginTest ("Reordenar pistas");
        {
            Project p;
            for (auto n : { "A", "B", "C", "D" }) edit::addTrack (p, n);
            edit::moveTrack (p, p.tracks[0].id, 2);
            juce::StringArray names;
            for (auto& t : p.tracks) names.add (t.name);
            expectEquals (names.joinIntoString (""), juce::String ("BCAD"));
        }

        beginTest ("Cambiar el tempo mantiene MIDI y secciones en la grilla");
        {
            auto p = edit::makeProject (edit::Template::fullBand, "Gracia", 60, "G", 4);
            const auto verse = p.sections[1].start;       // beats
            edit::setTempo (p, 120);
            expect (near (p.sections[1].start, verse));   // sigue en el compás 5
            expect (near (p.timeline().toSeconds (verse), 8));
        }

        beginTest ("Marcadores y navegación"_u);
        {
            auto p = edit::makeProject (edit::Template::fullBand, "M", 60, "G", 4);
            edit::addMarker (p, 10);
            edit::addMarker (p, 5);
            expectEquals (p.markers[0].number, 2);
            edit::removeMarker (p, edit::markerNumber (p, 1)->id);
            expectEquals (edit::addMarker (p, 30).number, 1);
            expect (near (*edit::nextStop (p, 0), 5));
            expect (near (*edit::nextStop (p, 5), 16));      // verso 1 en el compás 5 = beat 16
            expect (near (*edit::previousStop (p, 16), 5));
            expectEquals (edit::sectionAt (p, 17)->name, juce::String ("Verso 1"));
        }

        beginTest ("Solo y mute");
        {
            Project p;
            edit::addTrack (p, "A"); edit::addTrack (p, "B");
            expect (p.isAudible (p.tracks[0]));
            p.tracks[1].solo = true;
            expect (! p.isAudible (p.tracks[0]));
            expect (p.isAudible (p.tracks[1]));
        }
    }
};

struct MidiEditTests : juce::UnitTest
{
    MidiEditTests() : juce::UnitTest ("Piano roll", "Model") {}
    void runTest() override
    {
        auto clip = []
        {
            MidiClip c; c.start = 2; c.length = 8;
            edit::midi::addNote (c, 60, 0.24, 0.8);
            edit::midi::addNote (c, 64, 2.08, 0.6);
            edit::midi::addNote (c, 67, 3.92, 1.8);
            return c;
        };

        beginTest ("Buses: sin bucles, solo y borrado");
        {
            auto p = edit::makeProject (edit::Template::empty, "Ruteo", 70, "A", 4);
            auto voz = p.tracks[0].id;
            auto voces = edit::addTrack (p, "Voces", TrackRole::bus).id;
            auto reverb = edit::addTrack (p, "Reverb", TrackRole::bus).id;
            auto guitarra = edit::addTrack (p, "Guitarra").id;
            expect (edit::setOutputBus (p, voz, voces));
            expect (edit::addSend (p, voces, reverb).has_value());
            expect (! edit::addSend (p, reverb, voces).has_value());      // crearía un bucle
            expect (! edit::setOutputBus (p, reverb, voces));
            expect (edit::addSend (p, guitarra, reverb).has_value());
            expect (p.feedsInto (voz, reverb));
            // Solo en la voz: suenan sus buses, no la guitarra
            p.findTrack (voz)->solo = true;
            expect (p.isAudible (*p.findTrack (voces)));
            expect (p.isAudible (*p.findTrack (reverb)));
            expect (! p.isAudible (*p.findTrack (guitarra)));
            p.findTrack (voz)->solo = false;
            // Solo en el bus de voces: la voz que lo alimenta también suena
            p.findTrack (voces)->solo = true;
            expect (p.isAudible (*p.findTrack (voz)));
            expect (! p.isAudible (*p.findTrack (guitarra)));
            p.findTrack (voces)->solo = false;
            // Borrar el bus: la voz vuelve al Master y se quitan los envíos
            edit::removeTrack (p, reverb);
            expect (p.findTrack (guitarra)->sends.empty());
            edit::removeTrack (p, voces);
            expect (p.findTrack (voz)->outputBus.isNull());
        }

        beginTest ("Automatización: puntos, escritura y simplificación"_u);
        {
            AutomationLane l;
            l.target.kind = AutomationTarget::Kind::volume;
            expectWithinAbsoluteError (l.valueAt (3), 0.0, 1.0e-9);
            edit::automation::addPoint (l, 0, -10);
            edit::automation::addPoint (l, 4, 0);
            edit::automation::addPoint (l, 4, 2);          // reemplaza el del mismo tiempo
            expectEquals ((int) l.points.size(), 2);
            expectWithinAbsoluteError (l.valueAt (2), -4.0, 1.0e-9);
            expectWithinAbsoluteError (l.valueAt (10), 2.0, 1.0e-9);
            edit::automation::addPoint (l, 1, 99);          // se limita al rango (+6 dB)
            expectWithinAbsoluteError (l.points[1].value, 6.0, 1.0e-9);
            // Escribir de 0,5 a 2 reemplaza lo que había y conserva lo de después
            edit::automation::writeValue (l, 0.5, 2.0, -3);
            expectWithinAbsoluteError (l.valueAt (2.0), -3.0, 1.0e-9);
            expectWithinAbsoluteError (l.valueAt (4.0), 2.0, 1.0e-9);
            for (auto& p : l.points) expect (p.time <= 0.5 || p.time >= 2.0);
            // Simplificar quita puntos en línea recta
            AutomationLane r;
            for (int i = 0; i <= 10; ++i) r.points.push_back ({ (double) i, (double) i });
            edit::automation::simplify (r, 0.01);
            expectEquals ((int) r.points.size(), 2);
            // Escala de fader
            AutomationTarget v;
            expectWithinAbsoluteError (v.fromNormalised (v.toNormalised (-7.5)), -7.5, 1.0e-6);
        }

        beginTest ("Cuantizar a la grilla del proyecto");
        {
            auto c = clip();
            edit::midi::quantize (c, c.start, {}, 1.0);
            expect (near (c.notes[0].start, 0)); expect (near (c.notes[1].start, 2)); expect (near (c.notes[2].start, 4));
            auto half = clip();
            edit::midi::quantize (half, half.start, {}, 1.0, 0.5);
            expect (near (half.notes[0].start, 0.12));
            auto lengths = clip();
            edit::midi::quantize (lengths, lengths.start, {}, 1.0, 1.0, true);
            expect (near (lengths.notes[0].length, 1)); expect (near (lengths.notes[2].length, 2));
        }

        beginTest ("Transponer, legato, mover, duplicar");
        {
            auto c = clip();
            auto first = c.notes[0].id;
            edit::midi::transpose (c, { first }, 12);
            expectEquals (c.notes[0].pitch, 72);
            edit::midi::legato (c, {});
            expect (near (c.notes[0].length, 2.08 - 0.24));
            edit::midi::move (c, { c.notes[2].id }, 6, -1);
            expect (c.length >= c.notes.back().end());
            expectEquals ((int) edit::midi::duplicate (c, { first }).size(), 1);
            expectEquals ((int) c.notes.size(), 4);
            edit::midi::setVelocity (c, { first }, 300);
            expectEquals (c.notes[0].velocity, 127);
            expectEquals (edit::midi::noteName (60), juce::String ("Do4"));
            expect (edit::midi::isBlackKey (61));
        }

        beginTest ("Dividir un ítem MIDI"_u);
        {
            Project p;
            auto& t = edit::addTrack (p, "Keys", TrackRole::instrument);
            auto tid = t.id;
            auto c = clip();
            c.controls.push_back ({ 1.0, 1, 64, 127 });
            c.controls.push_back ({ 5.0, 1, 64, 0 });
            auto cid = c.id;
            edit::addMidiClip (p, tid, c);
            auto right = edit::splitClip (p, cid, 5);   // corte en el beat 3 del ítem
            expect (right.has_value());
            auto& clips = p.tracks[0].midiClips;
            expectEquals ((int) clips[0].notes.size(), 2);
            expectEquals ((int) clips[1].notes.size(), 1);
            expect (near (clips[1].notes[0].start, 0.92));
            expectEquals ((int) clips[1].controls.size(), 1);
            expect (near (clips[1].controls[0].time, 2));
        }
    }
};

struct SerializationTests : juce::UnitTest
{
    SerializationTests() : juce::UnitTest ("Guardar y abrir", "Model") {}
    void runTest() override
    {
        beginTest ("Ida y vuelta JSON");
        auto p = edit::makeProject (edit::Template::fullBand, "Way Maker / En vivo", 68, "E", 4);
        p.tracks[0].inserts.push_back ({ juce::Uuid(), "Worship Vocal Rack", "Worship Sounds", "<PLUGIN/>", juce::MemoryBlock ("abc", 3), true });
        MidiClip mc; mc.start = 8; mc.length = 4;
        edit::midi::addNote (mc, 60, 0, 1);
        mc.controls.push_back ({ 0.5, 1, 64, 127 });
        edit::addMidiClip (p, p.tracks[4].id, mc);
        edit::addMarker (p, 12, "Respiración"_u);
        auto back = io::fromJSON (io::toJSON (p));
        expect (back.has_value());
        expectEquals (back->name, p.name);
        expectEquals ((int) back->tracks.size(), (int) p.tracks.size());
        expect (back->tracks[0].inserts[0].bypassed);
        expectEquals ((int) back->tracks[0].inserts[0].state.getSize(), 3);
        expectEquals ((int) back->tracks[4].midiClips[0].controls.size(), 1);
        expectEquals (back->markers[0].name, "Respiración"_u);

        beginTest ("Ida y vuelta JSON: buses, envíos y automatización"_u);
        {
            auto q = edit::makeProject (edit::Template::empty, "Buses", 70, "A", 4);
            auto busId = edit::addTrack (q, "Reverb", TrackRole::bus).id;
            auto voz = q.tracks[0].id;
            auto sendId = edit::addSend (q, voz, busId, -8.0, true);
            expect (sendId.has_value());
            auto& t = *q.findTrack (voz);
            t.automationMode = AutomationMode::latch;
            auto& lane = edit::automation::ensureLane (t, { AutomationTarget::Kind::sendLevel, *sendId }, -8.0);
            edit::automation::addPoint (lane, 4.0, -20.0);
            auto r = io::fromJSON (io::toJSON (q));
            expect (r.has_value());
            auto& rt = *r->findTrack (voz);
            expect (r->findTrack (busId)->isBus());
            expectEquals ((int) rt.sends.size(), 1);
            expect (rt.sends[0].preFader && rt.sends[0].target == busId);
            expectWithinAbsoluteError (rt.sends[0].levelDb, -8.0, 1.0e-9);
            expect (rt.automationMode == AutomationMode::latch);
            expectEquals ((int) rt.automation.size(), 1);
            expect (rt.automation[0].target.kind == AutomationTarget::Kind::sendLevel && rt.automation[0].target.ref == *sendId);
            expectWithinAbsoluteError (rt.automation[0].valueAt (2.0), -14.0, 1.0e-9);
            expect (rt.outputBus.isNull());
        }

        beginTest ("Abre proyectos de la versión anterior (segundos → beats)"_u);
        const auto v1 = R"({"version":1,"id":"6F9619FF-8B86-D011-B42D-00C04FC964FF","name":"Viejo","bpm":60,"beatsPerBar":4,"key":"D",
            "loop":{"start":4,"end":8,"isEnabled":true},"tracks":[{"id":"6F9619FF-8B86-D011-B42D-00C04FC964F0","name":"Voz",
            "color":"violet","role":"audio","volumeDB":-3,"pan":0,"isMuted":false,"isSolo":false,"isArmed":false,
            "clips":[{"id":"6F9619FF-8B86-D011-B42D-00C04FC964F1","name":"Toma","fileName":"x.caf","start":2,"offset":0,"duration":3,"sourceDuration":3,"gainDB":0}]}],
            "sections":[{"id":"6F9619FF-8B86-D011-B42D-00C04FC964F2","name":"Coro","kind":"chorus","start":16,"length":32}]})";
        auto old = io::fromJSON (v1);
        expect (old.has_value());
        expect (near (old->tracks[0].clips[0].start, 2));       // 60 BPM: 1 s = 1 beat
        expect (near (old->sections[0].start, 16));
        expect (old->loopEnabled);

        beginTest ("Guardado con copia de seguridad y papelera de proyectos");
        auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("wdaw-test-" + juce::Uuid().toString());
        auto folder = io::createProjectFolder (root, "Gracia / Sublime");
        expect (! folder.getFileName().containsChar ('/'));
        expect (io::save (p, folder).wasOk());
        p.name = "Otra";
        expect (io::save (p, folder).wasOk());
        expect (folder.getChildFile ("project.json.bak").existsAsFile());
        folder.getChildFile ("project.json").replaceWithText ("{ dañado"_u);
        juce::String err;
        auto recovered = io::load (folder, &err);
        expect (recovered.has_value());
        expect (err.isNotEmpty());
        expectEquals ((int) io::listProjects (root).size(), 1);
        root.deleteRecursively();
    }
};

static TimelineTests timelineTests;
static EditingTests editingTests;
static MidiEditTests midiEditTests;
static SerializationTests serializationTests;
