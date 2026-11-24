#include <juce_gui_extra/juce_gui_extra.h>
#include "UI/MainComponent.h"
#include "Model/Serialization.h"

namespace wdaw
{

/** Proyecto de demostración (para las capturas de prueba): audio sintético, MIDI con pedal, secciones y marcadores. */
static juce::File makeDemoProject()
{
    auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Worship DAW Demo.worship");
    folder.deleteRecursively();
    auto p = edit::makeProject (edit::Template::fullBand, "Gracia Sublime", 72, "G", 4);
    const auto tl = p.timeline();
    auto audio = io::audioFolder (folder);
    audio.createDirectory();
    auto makeFile = [&] (const juce::String& name, double seconds, float energy, int seed)
    {
        auto f = audio.getChildFile (name);
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (f.createOutputStream().release(), 48000, 1, 24, {}, 0));
        juce::Random rng (seed);
        juce::AudioBuffer<float> b (1, (int) (seconds * 48000));
        float env = 0;
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const auto t = i / 48000.0;
            const auto beat = std::fmod (t / tl.secondsPerBeat(), 1.0);
            const auto section = edit::sectionAt (p, tl.toBeats (t));
            const float sectionEnergy = section == nullptr ? 0.3f : section->kind == SectionKind::chorus ? 1.0f : section->kind == SectionKind::verse ? 0.5f : 0.7f;
            env = 0.999f * env + 0.001f * sectionEnergy * energy * (seed == 2 && beat < 0.15 ? 1.6f : 1.0f);
            b.setSample (0, i, (rng.nextFloat() * 2 - 1) * env * 0.6f * (float) (0.6 + 0.4 * std::sin (t * 3 + seed)));
        }
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        return name;
    };
    const auto songEnd = tl.toSeconds (p.sections[7].end());
    auto clip = [&] (int track, const juce::String& file, Beats start, double length, const juce::String& name)
    {
        AudioClip c; c.file = file; c.start = start; c.length = length; c.sourceLength = length; c.name = name;
        edit::addClip (p, p.tracks[(size_t) track].id, c);
    };
    clip (0, makeFile ("bateria.wav", songEnd, 1.0f, 2), p.sections[0].start + 8, songEnd - tl.toSeconds (8), "Batería"_u);
    clip (1, makeFile ("bajo.wav", songEnd, 0.9f, 3), 0, songEnd, "Bajo");
    clip (2, makeFile ("guitarras.wav", songEnd, 0.8f, 4), p.sections[1].start, songEnd - tl.toSeconds (p.sections[1].start), "Guitarras");
    clip (6, makeFile ("voz.wav", tl.toSeconds (p.sections[3].end() - p.sections[1].start), 0.85f, 5), p.sections[1].start,
          tl.toSeconds (p.sections[3].end() - p.sections[1].start), "Voz Líder · Toma 3"_u);
    clip (7, makeFile ("coros.wav", tl.toSeconds (32), 0.7f, 6), p.sections[3].start, tl.toSeconds (32), "Coros 1");
    // MIDI: acordes G–D–Em–C con pedal
    MidiClip keys; keys.name = "Keys · MIDI"_u; keys.start = p.sections[2].start; keys.length = 32;
    const int chords[4][3] = { { 55, 59, 62 }, { 54, 57, 62 }, { 55, 59, 64 }, { 55, 60, 64 } };
    for (int bar = 0; bar < 8; ++bar)
    {
        for (auto pitch : chords[bar % 4]) edit::midi::addNote (keys, pitch, bar * 4.0, 3.5, 90);
        edit::midi::addNote (keys, chords[bar % 4][0] - 12, bar * 4.0, 1.8, 100);
        keys.controls.push_back ({ bar * 4.0, 1, 64, 127 });
        keys.controls.push_back ({ bar * 4.0 + 3.8, 1, 64, 0 });
    }
    edit::addMidiClip (p, p.tracks[4].id, keys);
    p.tracks[4].instrument = PluginManager::internalSlot ("piano");
    p.tracks[5].instrument = PluginManager::internalSlot ("pad");
    p.tracks[6].armed = true; p.tracks[6].monitor = MonitorMode::on;
    p.tracks[7].armed = true; p.tracks[7].input = { TrackInput::Kind::stereo, 2 };
    p.tracks[2].inserts.push_back (PluginManager::internalSlot ("latency:0"));
    p.tracks[2].inserts.back().name = "WorshipGuitar";
    p.tracks[6].inserts.push_back (PluginManager::internalSlot ("latency:0"));
    p.tracks[6].inserts.back().name = "Worship Vocal Rack";
    p.masterInserts.push_back (PluginManager::internalSlot ("latency:0"));
    p.masterInserts.back().name = "WorshipMastering";
    // Automatización: la voz sube en el coro; el pad crece hacia el puente
    {
        auto& voz = edit::automation::ensureLane (p.tracks[6], { AutomationTarget::Kind::volume }, -3.0);
        edit::automation::addPoint (voz, p.sections[3].start - 1, -3.0);
        edit::automation::addPoint (voz, p.sections[3].start, 0.5);
        edit::automation::addPoint (voz, p.sections[3].end() - 2, 0.5);
        edit::automation::addPoint (voz, p.sections[3].end(), -4.0);
        auto& pad = edit::automation::ensureLane (p.tracks[5], { AutomationTarget::Kind::volume }, -24.0);
        edit::automation::addPoint (pad, p.sections[1].start, -18.0);
        edit::automation::addPoint (pad, p.sections[3].start, -4.0);
        edit::automation::addPoint (pad, p.sections[4].start, -10.0);
        p.tracks[6].automationMode = AutomationMode::touch;
        pad.points[1].shape = CurveShape::bezier;   // el pad crece en curva hacia el coro
        pad.points[1].tension = -0.6;
        voz.points[1].shape = CurveShape::slowStartEnd;
    }
    edit::addMarker (p, p.sections[1].start + 16, "Respiración"_u);
    // Carpeta con las guitarras (como en REAPER)
    edit::makeFolder (p, { p.tracks[2].id, p.tracks[3].id }, "Guitarras");
    p.loopStart = p.sections[3].start; p.loopEnd = p.sections[3].end(); p.loopEnabled = true;
    io::save (p, folder);
    return folder;
}

} // namespace wdaw

//==============================================================================
class WorshipDAWApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "Worship DAW"; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String& commandLine) override
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        window = std::make_unique<MainWindow> ();

        // Modo de prueba: --screenshot <archivo.png> [home|editor|mixer|pianoroll]
        auto args = juce::StringArray::fromTokens (commandLine, true);
        if (const auto i = args.indexOf ("--screenshot"); i >= 0 && i + 1 < args.size())
        {
            const juce::File out (args[i + 1].unquoted());
            const auto mode = i + 2 < args.size() ? args[i + 2] : juce::String ("editor");
            auto* main = window->main;
            if (mode != "home") main->openProject (wdaw::makeDemoProject());
            if (mode == "mixer") main->showMixer (true);
            juce::Timer::callAfterDelay (mode == "pianoroll" ? 300 : 1500, [this, main, out, mode]
            {
                if (mode == "pianoroll") main->openPianoRollForFirstMidiClip();
                juce::Timer::callAfterDelay (1500, [this, main, out, mode]
                {
                    juce::Component* target = main;
                    if (mode == "pianoroll" && main->pianoRollComponent() != nullptr) target = main->pianoRollComponent();
                    auto img = target->createComponentSnapshot (target->getLocalBounds(), true, 2.0f);
                    out.deleteFile();
                    juce::FileOutputStream stream (out);
                    juce::PNGImageFormat().writeImageToStream (img, stream);
                    stream.flush();
                    quit();
                });
            });
        }
    }

    void shutdown() override
    {
        window.reset();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override { quit(); }

    void anotherInstanceStarted (const juce::String& commandLine) override
    {
        // Doble clic en un .worship con la app abierta
        const juce::File f (commandLine.unquoted());
        if (f.isDirectory() && window != nullptr) window->main->openProject (f);
    }

    struct MainWindow : juce::DocumentWindow
    {
        MainWindow() : juce::DocumentWindow ("Worship DAW", wdaw::ui::colours::background, juce::DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            main = new wdaw::ui::MainComponent();
            setContentOwned (main, true);
        #if JUCE_MAC
            juce::MenuBarModel::setMacMainMenu (main);
        #else
            setMenuBar (main);
        #endif
            setResizable (true, true);
            setResizeLimits (1000, 640, 10000, 10000);
            centreWithSize (juce::jmin (1440, juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea.getWidth() - 40),
                            juce::jmin (900, juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea.getHeight() - 40));
            setVisible (true);
        }
        ~MainWindow() override
        {
        #if ! JUCE_MAC
            setMenuBar (nullptr);
        #endif
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
        wdaw::ui::MainComponent* main = nullptr;
    };

private:
    wdaw::ui::LookAndFeel lookAndFeel;
    std::unique_ptr<MainWindow> window;
};

START_JUCE_APPLICATION (WorshipDAWApplication)
