#pragma once

#include "Session.h"

namespace wdaw
{

/** Superficie de control con protocolo Mackie Control Universal (MCU), como en REAPER:
    Behringer X-Touch, iCON Platform, PreSonus FaderPort 8/16 (modo MCU), Mackie MCU Pro, etc.

      - 8 canales por banco (Bank ◀ ▶ de a 8, Channel ◀ ▶ de a 1) + fader del Master.
      - Faders motorizados (con "touch" para escribir automatización), V-Pots para paneo o envíos.
      - Botones REC / SOLO / MUTE / SELECT, transporte, ciclo, click, marcadores, F1–F8 = marcadores 1–8,
        modos de automatización (Read, Touch, Latch, Trim = apagado), Save, Undo (Shift = Redo).
      - Pantalla: nombres de pista y valores; contador en compases; medidores por canal.

    Los mensajes del dispositivo llegan en el hilo MIDI y se procesan en el hilo de mensajes. */
class MackieControl : private juce::Timer, private juce::MidiInputCallback
{
public:
    MackieControl();
    ~MackieControl() override;

    /** Proyecto abierto (nulo al cerrar). Los medidores se piden con estas funciones (0…1 lineal). */
    void setSession (Session*);
    std::function<float (const juce::Uuid&)> trackLevel;
    std::function<float()> masterLevel;

    /** Abre los puertos (identificadores de juce::MidiDeviceInfo). Vacío = cerrar. Devuelve un texto de estado. */
    juce::String openDevices (const juce::String& inputId, const juce::String& outputId);
    void closeDevices();
    bool isConnected() const { return input != nullptr || output != nullptr; }
    juce::String statusText;

    //--- Lógica (pública para las pruebas)
    void handleMessage (const juce::MidiMessage&);              // hilo de mensajes
    std::function<void (const juce::MidiMessage&)> sendHook;    // en pruebas: captura lo que se enviaría
    void updateSurface();                                       // manda solo lo que cambió
    void resendAll();

    enum class Mode { pan, send };
    Mode mode = Mode::pan;
    int sendBus = 0;          // en modo envíos: índice del bus (entre los buses del proyecto)
    int bankOffset = 0;
    std::vector<juce::Uuid> visibleTracks() const;
    const Track* stripTrack (int strip) const;

    // Notas del protocolo (MCU)
    enum Note : int
    {
        recArm = 0, solo = 8, mute = 16, select = 24, vpotPush = 32,
        assignTrack = 40, assignSend = 41, assignPan = 42, assignPlugin = 43, assignEq = 44, assignInstrument = 45,
        bankLeft = 46, bankRight = 47, channelLeft = 48, channelRight = 49, flip = 50, globalView = 51,
        nameValue = 52, smpteBeats = 53, f1 = 54, shift = 70, option = 71, control = 72, alt = 73,
        readOff = 74, write = 75, trim = 76, touch = 77, latch = 78, group = 79,
        save = 80, undo = 81, cancel = 82, enter = 83, marker = 84, nudge = 85, cycle = 86, drop = 87, replace = 88,
        click = 89, soloGlobal = 90, rewind = 91, fastForward = 92, stop = 93, play = 94, record = 95,
        cursorUp = 96, cursorDown = 97, cursorLeft = 98, cursorRight = 99, zoom = 100, scrub = 101,
        faderTouch = 104, masterTouch = 112
    };

private:
    Session* session = nullptr;
    std::unique_ptr<juce::MidiInput> input;
    std::unique_ptr<juce::MidiOutput> output;

    bool shiftDown = false, showValues = false;
    std::array<bool, 9> touched {};
    std::array<juce::uint32, 8> valueUntil {};       // mostrar el valor en la línea de abajo hasta…
    std::array<juce::uint32, 8> lastVpotTurn {};

    // Lo último enviado (para mandar solo los cambios)
    std::array<int, 9> sentFader;
    std::array<int, 8> sentRing;
    std::array<int, 128> sentLed;
    std::array<int, 12> sentDigits;
    juce::String sentLcd[2];
    juce::uint32 lastMeters = 0;

    void send (const juce::MidiMessage&);
    void setLed (int note, bool on);
    void sendLcd (int line, const juce::String& text);
    void sendDigits (const juce::String& text, int firstCc, int count);
    void pressed (int note);
    void stripButton (int kind, int strip);
    void selectRelative (int delta);
    void clampBank();
    const Send* stripSend (const Track&) const;
    juce::String stripValue (int strip) const;

    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
    void timerCallback() override { updateSurface(); }

    JUCE_DECLARE_WEAK_REFERENCEABLE (MackieControl)
};

} // namespace wdaw
