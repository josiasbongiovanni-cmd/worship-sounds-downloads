#include "UI/MackieControl.h"
#include "Model/Serialization.h"

using namespace wdaw;

/** Mackie Control: se simulan los mensajes de una superficie y se revisa lo que la app le devuelve. */
struct SurfaceTests : juce::UnitTest
{
    SurfaceTests() : juce::UnitTest ("Superficie de control (Mackie)", "Engine") {}

    struct Rig
    {
        PluginManager plugins;
        AudioEngine engine { plugins };
        MidiLearn learn;
        juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("wdaw-mcu-" + juce::Uuid().toString());
        std::unique_ptr<Session> session;
        MackieControl mcu;
        std::vector<juce::MidiMessage> sent;

        Rig()
        {
            engine.prepareForTesting (48000, 512, 2, 2);
            folder.createDirectory();
            auto p = edit::makeProject (edit::Template::empty, "Prueba", 120, "G", 4);
            for (int i = 1; i <= 10; ++i) edit::addTrack (p, "Pista " + juce::String (i));
            p.tracks[0].name = "Voz Líder"_u;
            io::save (p, folder);
            session = std::make_unique<Session> (engine, plugins, learn, folder, p);
            mcu.sendHook = [this] (const juce::MidiMessage& m) { sent.push_back (m); };
            mcu.setSession (session.get());
        }
        ~Rig() { mcu.setSession (nullptr); session.reset(); folder.deleteRecursively(); }

        const Track& track (int i) const { return session->project().tracks[(size_t) i]; }
        void press (int note) { mcu.handleMessage (juce::MidiMessage::noteOn (1, note, (juce::uint8) 127)); mcu.handleMessage (juce::MidiMessage::noteOn (1, note, (juce::uint8) 0)); }
        juce::String lcdLine (int line) const
        {
            juce::String text;
            for (auto& m : sent)
                if (m.isSysEx() && m.getSysExDataSize() > 6)
                {
                    auto* d = m.getSysExData();
                    if (d[2] == 0x66 && d[4] == 0x12 && d[5] == line * 56)
                        text = juce::String::createStringFromData (d + 6, m.getSysExDataSize() - 6);
                }
            return text;
        }
    };

    void runTest() override
    {
        beginTest ("Fader, toque y paneo desde la superficie"_u);
        {
            Rig rig;
            const auto pos = Decibels::faderPosition (-6.0);
            rig.mcu.handleMessage (juce::MidiMessage::pitchWheel (2, (int) std::round (pos * 16383)));   // canal 2
            expectWithinAbsoluteError (rig.track (1).volumeDb, -6.0, 0.11);
            // V-Pot del canal 1: 5 pasos a la derecha
            rig.mcu.handleMessage (juce::MidiMessage::controllerEvent (1, 16, 5));
            expectWithinAbsoluteError (rig.track (0).pan, 0.1, 1.0e-6);
            rig.mcu.handleMessage (juce::MidiMessage::controllerEvent (1, 16, 0x40 | 10));
            expectWithinAbsoluteError (rig.track (0).pan, -0.1, 1.0e-6);
            // Master (canal 9)
            rig.mcu.handleMessage (juce::MidiMessage::pitchWheel (9, 0));
            expectEquals (rig.session->project().masterVolumeDb, Decibels::silence);
        }

        beginTest ("Botones de canal, bancos y transporte");
        {
            Rig rig;
            rig.press (MackieControl::mute + 2);
            expect (rig.track (2).muted);
            rig.press (MackieControl::solo + 0);
            expect (rig.track (0).solo);
            rig.press (MackieControl::recArm + 1);
            expect (rig.track (1).armed);
            rig.press (MackieControl::bankRight);           // canales 9–11
            expectEquals (rig.mcu.bankOffset, 8);
            rig.press (MackieControl::select + 1);
            expect (rig.session->selectedTrack == rig.track (9).id);
            rig.press (MackieControl::bankRight);           // no pasa del final
            expectEquals (rig.mcu.bankOffset, 10);
            rig.press (MackieControl::channelLeft);
            expectEquals (rig.mcu.bankOffset, 9);
            rig.press (MackieControl::cycle);
            expect (rig.session->project().loopEnabled || rig.session->project().loopEnd <= rig.session->project().loopStart);
            rig.press (MackieControl::touch);
            expect (rig.session->project().findTrack (rig.session->selectedTrack)->automationMode == AutomationMode::touch);
            const auto before = rig.session->editCursor;
            rig.mcu.handleMessage (juce::MidiMessage::controllerEvent (1, 60, 4));   // Jog: 4 semicorcheas
            expectWithinAbsoluteError (rig.session->editCursor, before + 1.0, 1.0e-9);
            rig.press (MackieControl::undo);
        }

        beginTest ("Respuesta a la superficie: faders, LEDs y pantalla"_u);
        {
            Rig rig;
            rig.session->updateTrack (rig.track (0).id, [] (Track& t) { t.volumeDb = 0; t.muted = true; });
            rig.sent.clear();
            rig.mcu.resendAll();
            bool faderOk = false, muteLed = false;
            for (auto& m : rig.sent)
            {
                if (m.isPitchWheel() && m.getChannel() == 1)
                    faderOk = std::abs (m.getPitchWheelValue() - (int) std::round (Decibels::faderPosition (0) * 16383)) <= 1;
                if (m.isNoteOn() && m.getNoteNumber() == MackieControl::mute && m.getVelocity() == 127) muteLed = true;
            }
            expect (faderOk, "el fader 1 debe ir a 0 dB");
            expect (muteLed, "el LED de mute del canal 1 debe encenderse");
            const auto top = rig.lcdLine (0);
            expectEquals (top.length(), 56);
            expect (top.startsWith ("Voz Li"), "nombre sin acentos en la pantalla: " + top);
            expect (top.contains ("Pista "));
            // Mientras se toca un fader, la app no lo mueve
            rig.mcu.handleMessage (juce::MidiMessage::noteOn (1, MackieControl::faderTouch, (juce::uint8) 127));
            rig.session->updateTrack (rig.track (0).id, [] (Track& t) { t.volumeDb = -20; }, false);
            rig.sent.clear();
            rig.mcu.updateSurface();
            for (auto& m : rig.sent) expect (! (m.isPitchWheel() && m.getChannel() == 1), "no debe mover un fader tocado");
            rig.mcu.handleMessage (juce::MidiMessage::noteOn (1, MackieControl::faderTouch, (juce::uint8) 0));
            rig.mcu.updateSurface();
            bool moved = false;
            for (auto& m : rig.sent) if (m.isPitchWheel() && m.getChannel() == 1) moved = true;
            expect (moved, "al soltarlo, el fader va al valor de la pista");
        }

        beginTest ("Modo envíos: V-Pot controla el envío al bus"_u);
        {
            Rig rig;
            const auto bus = rig.session->addTrack (TrackRole::bus);
            rig.press (MackieControl::assignSend);
            expect (rig.mcu.mode == MackieControl::Mode::send);
            rig.press (MackieControl::vpotPush + 0);            // crea el envío
            expectEquals ((int) rig.track (0).sends.size(), 1);
            expect (rig.track (0).sends[0].target == bus);
            const auto before = rig.track (0).sends[0].levelDb;
            rig.mcu.handleMessage (juce::MidiMessage::controllerEvent (1, 16, 3));
            expect (rig.track (0).sends[0].levelDb > before);
        }
    }
};

static SurfaceTests surfaceTests;
