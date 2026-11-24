#include <juce_audio_formats/juce_audio_formats.h>
#include "Engine/AudioEngine.h"
#include "Engine/Exporter.h"
#include "Model/Editing.h"
#include "Model/Serialization.h"

using namespace wdaw;

namespace
{
    constexpr double sr = 48000;
    constexpr int block = 512;

    /** Escribe un WAV de `seconds` con impulsos (valor 1) en las muestras indicadas. */
    juce::String writeImpulseFile (const juce::File& folder, const juce::String& name, double seconds, std::vector<int> impulses,
                                   double fileRate = sr)
    {
        auto f = io::audioFolder (folder).getChildFile (name);
        f.getParentDirectory().createDirectory();
        f.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (f.createOutputStream().release(), fileRate, 1, 32, {}, 0));
        juce::AudioBuffer<float> b (1, (int) (seconds * fileRate));
        b.clear();
        for (auto i : impulses) b.setSample (0, i, 1.0f);
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        return name;
    }

    struct Rig
    {
        PluginManager plugins;
        AudioEngine engine { plugins };
        juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("wdaw-engine-" + juce::Uuid().toString());
        Project project;

        Rig()
        {
            folder.createDirectory();
            io::audioFolder (folder).createDirectory();
            project.bpm = 120;            // 1 beat = 0,5 s = 24000 muestras
            project.clickEnabled = false;
            engine.prepareForTesting (sr, block, 2, 4);
        }
        ~Rig() { folder.deleteRecursively(); }

        void apply() { engine.setProject (project, folder); }

        /** Reproduce `seconds` desde `from` y devuelve la salida (canal izquierdo, salidas 1-4). */
        juce::AudioBuffer<float> render (Beats from, double seconds, const juce::AudioBuffer<float>* input = nullptr)
        {
            const int total = (int) (seconds * sr);
            juce::AudioBuffer<float> out (4, total);
            out.clear();
            engine.play (from);
            juce::AudioBuffer<float> tmp (4, block);
            for (int pos = 0; pos < total; pos += block)
            {
                const int n = juce::jmin (block, total - pos);
                const float* in[2] = { nullptr, nullptr };
                if (input != nullptr) { in[0] = input->getReadPointer (0, pos); in[1] = input->getReadPointer (juce::jmin (1, input->getNumChannels() - 1), pos); }
                engine.processForTesting (input != nullptr ? in : nullptr, input != nullptr ? 2 : 0, tmp.getArrayOfWritePointers(), 4, n);
                for (int c = 0; c < 4; ++c) out.copyFrom (c, pos, tmp, c, 0, n);
            }
            return out;
        }

        AudioClip clip (const juce::String& file, Beats start, double length, double offset = 0)
        {
            AudioClip c;
            c.file = file; c.start = start; c.offset = offset; c.length = length; c.sourceLength = offset + length;
            c.fadeIn = 0; c.fadeOut = 0;
            return c;
        }
    };

    std::vector<int> peaksAbove (const juce::AudioBuffer<float>& b, float threshold, int channel = 0)
    {
        std::vector<int> idx;
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (std::abs (b.getSample (channel, i)) > threshold) idx.push_back (i);
        return idx;
    }
}

struct EngineTests : juce::UnitTest
{
    EngineTests() : juce::UnitTest ("Motor de audio", "Engine") {}

    void runTest() override
    {
        beginTest ("Ítems en la muestra exacta"_u);
        {
            Rig rig;
            auto f = writeImpulseFile (rig.folder, "imp.wav", 1.0, { 1000 });
            auto& t = edit::addTrack (rig.project, "Voz");
            edit::addClip (rig.project, t.id, rig.clip (f, 1.0, 1.0));    // beat 1 = 24000
            rig.apply();
            auto out = rig.render (0, 1.0);
            auto peaks = peaksAbove (out, 0.5f);
            expectEquals ((int) peaks.size(), 1);
            if (! peaks.empty()) expectEquals (peaks[0], 25000);
            expectWithinAbsoluteError (out.getSample (0, 25000), 1.0f, 1.0e-4f);
        }

        beginTest ("Ítems superpuestos se mezclan (no se desfasan)"_u);
        {
            Rig rig;
            auto f = writeImpulseFile (rig.folder, "imp.wav", 1.0, { 100 });
            auto& t = edit::addTrack (rig.project, "Voz");
            edit::addClip (rig.project, t.id, rig.clip (f, 0.0, 1.0));     // impulso en 100
            edit::addClip (rig.project, t.id, rig.clip (f, 0.5, 1.0));     // se superpone: impulso en 12100
            rig.apply();
            auto peaks = peaksAbove (rig.render (0, 1.0), 0.5f);
            expectEquals ((int) peaks.size(), 2);
            if (peaks.size() == 2) { expectEquals (peaks[0], 100); expectEquals (peaks[1], 12100); }
        }

        beginTest ("Loop sin cortes, en la muestra exacta"_u);
        {
            Rig rig;
            auto f = writeImpulseFile (rig.folder, "imp.wav", 2.0, { 1000 });
            auto& t = edit::addTrack (rig.project, "Pad");
            edit::addClip (rig.project, t.id, rig.clip (f, 0.0, 2.0));
            rig.project.loopStart = 0; rig.project.loopEnd = 2; rig.project.loopEnabled = true;   // 1 s
            rig.apply();
            auto peaks = peaksAbove (rig.render (0, 3.1), 0.5f);
            expectEquals ((int) peaks.size(), 4);
            if (peaks.size() >= 3) { expectEquals (peaks[0], 1000); expectEquals (peaks[1], 49000); expectEquals (peaks[2], 97000); }
        }

        beginTest ("Compensación de latencia de plugins (PDC)"_u);
        {
            Rig rig;
            auto f = writeImpulseFile (rig.folder, "imp.wav", 1.0, { 2000 });
            auto& a = edit::addTrack (rig.project, "Con plugin");
            a.inserts.push_back (PluginManager::internalSlot ("latency:300"));
            auto aid = a.id;
            auto& b = edit::addTrack (rig.project, "Sin plugin");
            auto bid = b.id;
            edit::addClip (rig.project, aid, rig.clip (f, 0.0, 1.0));
            edit::addClip (rig.project, bid, rig.clip (f, 0.0, 1.0));
            rig.apply();
            auto snap = rig.engine.currentSnapshot();
            expectEquals (snap->maxLatency, 300);
            auto out = rig.render (0, 1.0);
            auto peaks = peaksAbove (out, 0.5f);
            // Las dos pistas llegan juntas (una sola muestra con valor 2), retrasadas 300 muestras.
            expectEquals ((int) peaks.size(), 1);
            if (! peaks.empty()) { expectEquals (peaks[0], 2300); expectWithinAbsoluteError (out.getSample (0, 2300), 2.0f, 1.0e-3f); }
        }

        beginTest ("Archivo a otra frecuencia (44,1 kHz) se remuestrea"_u);
        {
            Rig rig;
            auto f = writeImpulseFile (rig.folder, "imp441.wav", 1.0, { 4410 }, 44100);   // 0,1 s
            auto& t = edit::addTrack (rig.project, "Voz");
            edit::addClip (rig.project, t.id, rig.clip (f, 0.0, 1.0));
            rig.apply();
            auto out = rig.render (0, 0.5);
            int best = 0;
            for (int i = 1; i < out.getNumSamples(); ++i) if (std::abs (out.getSample (0, i)) > std::abs (out.getSample (0, best))) best = i;
            expect (std::abs (best - 4800) <= 3, "pico en " + juce::String (best));
        }

        beginTest ("Instrumento incluido y MIDI con pedal de sustain"_u);
        {
            Rig rig;
            auto& t = edit::addTrack (rig.project, "Keys", TrackRole::instrument);
            t.instrument = PluginManager::internalSlot ("piano");
            MidiClip c; c.start = 0; c.length = 4;
            edit::midi::addNote (c, 60, 0, 0.25);
            c.controls.push_back ({ 0.0, 1, 64, 127 });   // pedal abajo
            edit::addMidiClip (rig.project, t.id, c);
            rig.apply();
            auto out = rig.render (0, 1.5);
            expect (out.getMagnitude (0, 0, 2000) > 0.01f, "la nota suena");
            // Con el pedal, la nota sigue sonando después de soltarla (0,25 beats = 6000 muestras)
            expect (out.getMagnitude (0, 30000, 4000) > 0.001f, "sostenida por el pedal");
        }

        beginTest ("Monitoreo de entrada y grabación directa al proyecto"_u);
        {
            Rig rig;
            auto& t = edit::addTrack (rig.project, "Voz");
            t.armed = true;
            t.input = { TrackInput::Kind::mono, 1 };     // Entrada 2
            t.monitor = MonitorMode::on;
            rig.apply();
            juce::AudioBuffer<float> input (2, (int) sr);
            input.clear();
            for (int i = 0; i < input.getNumSamples(); ++i) input.setSample (1, i, 0.25f);
            auto out = rig.render (0, 0.25, &input);
            expectWithinAbsoluteError (out.getSample (0, 5000), 0.25f, 1.0e-3f);   // se escucha la entrada 2

            AudioEngine::RecordRequest req;
            req.recordStart = 0; req.playFrom = 0; req.audioTracks = { t.id };
            expect (rig.engine.startRecording (req, rig.project, rig.folder).wasOk());
            rig.render (0, 0.5, &input);
            auto result = rig.engine.stopRecording();
            expectEquals ((int) result.audio.size(), 1);
            if (! result.audio.empty())
            {
                auto file = io::audioFolder (rig.folder).getChildFile (result.audio[0].fileName);
                expect (file.existsAsFile(), "la toma está en la carpeta del proyecto"_u);
                expectWithinAbsoluteError (result.audio[0].sourceSeconds, 0.5, 0.02);
                auto info = probeAudioFile (file);
                expect (info.has_value() && std::abs (info->lengthSeconds - 0.5) < 0.02);
            }
        }

        beginTest ("Grabación MIDI conserva notas y pedal"_u);
        {
            Rig rig;
            auto& t = edit::addTrack (rig.project, "Keys", TrackRole::instrument);
            t.armed = true;
            rig.apply();
            rig.engine.setLiveMidiTargets ({ t.id });
            AudioEngine::RecordRequest req;
            req.recordStart = 0; req.playFrom = 0; req.midiTracks = { t.id };
            expect (rig.engine.startRecording (req, rig.project, rig.folder).wasOk());
            rig.render (0, 0.05);
            auto now = [] { return juce::Time::getMillisecondCounterHiRes() * 0.001; };
            auto msg = [&] (juce::MidiMessage m) { m.setTimeStamp (now()); rig.engine.injectMidiForTesting (m); };
            msg (juce::MidiMessage::controllerEvent (1, 64, 127));
            msg (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 90));
            rig.render (0, 0.05);
            msg (juce::MidiMessage::noteOff (1, 64));
            msg (juce::MidiMessage::controllerEvent (1, 64, 0));
            rig.render (0, 0.05);
            auto result = rig.engine.stopRecording();
            expectEquals ((int) result.midi.size(), 1);
            if (! result.midi.empty())
            {
                auto& clip = result.midi[0].second;
                expectEquals ((int) clip.notes.size(), 1);
                expectEquals ((int) clip.controls.size(), 2);
                if (! clip.notes.empty()) expectEquals (clip.notes[0].pitch, 64);
            }
        }

        beginTest ("Click en cada tiempo, a una salida aparte para in-ears"_u);
        {
            Rig rig;
            rig.project.clickEnabled = true;
            rig.project.clickVolumeDb = 0;
            rig.project.clickOutPair = 2;      // salidas 3/4
            edit::addTrack (rig.project, "Vacía"_u);
            rig.apply();
            auto out = rig.render (0, 1.1);
            expect (out.getMagnitude (0, 0, out.getNumSamples()) < 1.0e-6f, "nada en el Master");
            expect (out.getMagnitude (2, 0, 200) > 0.1f, "click en el tiempo 1");
            expect (out.getMagnitude (2, 23000, 900) < 0.01f);
            expect (out.getMagnitude (2, 24000, 200) > 0.1f, "click en el tiempo 2");
        }

        beginTest ("Render (exportar) con stems y normalización"_u);
        {
            Rig rig;
            auto f = writeImpulseFile (rig.folder, "imp.wav", 1.0, { 500 });
            auto& t = edit::addTrack (rig.project, "Voz");
            edit::addClip (rig.project, t.id, rig.clip (f, 0.0, 1.0));
            rig.apply();
            ExportOptions o;
            o.destination = rig.folder.getChildFile ("Exports").getChildFile ("Mezcla");
            o.destination.getParentDirectory().createDirectory();
            o.tailSeconds = 0.5;
            o.stems = true;
            o.normalize = true;
            o.normalizePeakDb = -6;
            o.bitDepth = 16;
            auto res = exportProject (rig.engine, rig.project, rig.folder, o);
            expect (res.ok, res.error);
            expectEquals ((int) res.files.size(), 2);
            if (! res.files.empty())
            {
                auto info = probeAudioFile (res.files[0]);
                expect (info.has_value() && std::abs (info->lengthSeconds - 1.5) < 0.01);   // 1 s de contenido + 0,5 s de cola
                expectWithinAbsoluteError (res.peakDb, -6.0f, 0.1f);
            }
        }
    }
};

static EngineTests engineTests;
