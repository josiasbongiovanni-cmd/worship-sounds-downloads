#include "MackieControl.h"

namespace wdaw
{

namespace
{
    constexpr int faderMax = 16383;

    /** Texto para la pantalla de la superficie: solo ASCII imprimible (sin acentos). */
    juce::String lcdText (const juce::String& s, int width)
    {
        static const juce::String from = juce::String::fromUTF8 ("áéíóúÁÉÍÓÚñÑüÜ·→");
        static const juce::String to = "aeiouAEIOUnNuU.>";
        juce::String out;
        for (auto c : s)
        {
            const auto i = from.indexOfChar (c);
            if (i >= 0) c = to[i];
            out << (c >= 32 && c < 127 ? juce::String::charToString (c) : juce::String ("?"));
        }
        return out.substring (0, width).paddedRight (' ', width);
    }

    /** Caracteres del display de 7 segmentos (contador y asignación). */
    int segmentChar (juce::juce_wchar c)
    {
        if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
        if (c >= 'A' && c <= 'Z') return c - 'A' + 1;
        if (c >= '0' && c <= '9') return 0x30 + (c - '0');
        if (c == '-') return 0x2D;
        return 0x20;
    }
}

MackieControl::MackieControl()
{
    sentFader.fill (-1); sentRing.fill (-1); sentLed.fill (-1); sentDigits.fill (-1);
    startTimerHz (30);
}

MackieControl::~MackieControl()
{
    stopTimer();
    closeDevices();
}

void MackieControl::setSession (Session* s)
{
    session = s;
    bankOffset = 0;
    sendBus = 0;
    touched.fill (false);
    resendAll();
}

juce::String MackieControl::openDevices (const juce::String& inputId, const juce::String& outputId)
{
    closeDevices();
    juce::StringArray problems;
    if (inputId.isNotEmpty())
    {
        input = juce::MidiInput::openDevice (inputId, this);
        if (input != nullptr) input->start(); else problems.add ("no se pudo abrir la entrada");
    }
    if (outputId.isNotEmpty())
    {
        output = juce::MidiOutput::openDevice (outputId);
        if (output == nullptr) problems.add (juce::String::fromUTF8 ("no se pudo abrir la salida (faders y pantalla no se moverán)"));
    }
    if (input == nullptr && output == nullptr && problems.isEmpty()) statusText = "Desconectada";
    else if (problems.isEmpty()) statusText = "Conectada: " + (input != nullptr ? input->getName() : juce::String ("-"));
    else statusText = problems.joinIntoString ("; ");
    resendAll();
    return statusText;
}

void MackieControl::closeDevices()
{
    if (output != nullptr)
    {
        // Dejar la superficie limpia: pantalla en blanco, LEDs apagados, faders abajo.
        sendLcd (0, {}); sendLcd (1, {});
        for (int n = 0; n < 128; ++n) if (sentLed[(size_t) n] > 0) send (juce::MidiMessage::noteOn (1, n, (juce::uint8) 0));
        for (int s = 0; s < 9; ++s) send (juce::MidiMessage::pitchWheel (s + 1, 0));
    }
    if (input != nullptr) input->stop();
    input.reset();
    output.reset();
    statusText = "Desconectada";
}

void MackieControl::send (const juce::MidiMessage& m)
{
    if (sendHook) sendHook (m);
    if (output != nullptr) output->sendMessageNow (m);
}

void MackieControl::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m)
{
    juce::WeakReference<MackieControl> weak (this);
    juce::MessageManager::callAsync ([weak, m] { if (weak != nullptr) weak->handleMessage (m); });
}

//==============================================================================
std::vector<juce::Uuid> MackieControl::visibleTracks() const
{
    std::vector<juce::Uuid> ids;
    if (session == nullptr) return ids;
    auto& p = session->project();
    for (auto& t : p.tracks) if (! p.isInsideCollapsedFolder (t)) ids.push_back (t.id);
    return ids;
}

const Track* MackieControl::stripTrack (int strip) const
{
    if (session == nullptr || strip < 0 || strip >= 8) return nullptr;
    const auto ids = visibleTracks();
    const auto i = bankOffset + strip;
    return i < (int) ids.size() ? session->project().findTrack (ids[(size_t) i]) : nullptr;
}

const Send* MackieControl::stripSend (const Track& t) const
{
    const auto buses = session->project().buses();
    if (sendBus < 0 || sendBus >= (int) buses.size()) return nullptr;
    for (auto& s : t.sends) if (s.target == buses[(size_t) sendBus]->id) return &s;
    return nullptr;
}

void MackieControl::clampBank()
{
    bankOffset = juce::jlimit (0, juce::jmax (0, (int) visibleTracks().size() - 1), bankOffset);
}

//==============================================================================
void MackieControl::handleMessage (const juce::MidiMessage& m)
{
    if (session == nullptr) return;
    auto& s = *session;
    const auto now = juce::Time::getMillisecondCounter();

    // Faders (pitch bend por canal: 1–8 canales, 9 = Master)
    if (m.isPitchWheel())
    {
        const int strip = m.getChannel() - 1;
        const auto db = std::round (Decibels::faderDb (m.getPitchWheelValue() / (double) faderMax) * 10) / 10;
        if (strip == 8) s.edit ([db] (Project& p) { p.masterVolumeDb = db; }, false);
        else if (auto* t = stripTrack (strip))
        {
            s.setControl (t->id, { AutomationTarget::Kind::volume }, db);
            valueUntil[(size_t) strip] = now + 1500;
        }
        sentFader[(size_t) juce::jlimit (0, 8, strip)] = m.getPitchWheelValue();   // el motor no tiene que "corregir" al usuario
        return;
    }

    // V-Pots (CC 16–23, relativos) y rueda Jog (CC 60)
    if (m.isController())
    {
        const int cc = m.getControllerNumber(), v = m.getControllerValue();
        const int delta = (v & 0x40) != 0 ? -(v & 0x3F) : (v & 0x3F);
        if (cc >= 16 && cc < 24)
        {
            const int strip = cc - 16;
            auto* t = stripTrack (strip);
            if (t == nullptr || delta == 0) return;
            if (now - lastVpotTurn[(size_t) strip] > 1000) s.beginGesture();   // un paso de deshacer por "giro"
            lastVpotTurn[(size_t) strip] = now;
            valueUntil[(size_t) strip] = now + 1500;
            if (mode == Mode::pan)
            {
                const auto pan = s.effectiveValue (*t, { AutomationTarget::Kind::pan });
                s.setControl (t->id, { AutomationTarget::Kind::pan }, std::round ((pan + delta * 0.02) * 100) / 100);
            }
            else if (auto* snd = stripSend (*t))
            {
                const AutomationTarget target { AutomationTarget::Kind::sendLevel, snd->id };
                const auto pos = Decibels::faderPosition (s.effectiveValue (*t, target)) + delta * 0.01;
                s.setControl (t->id, target, std::round (Decibels::faderDb (juce::jlimit (0.0, 1.0, pos)) * 10) / 10);
            }
            return;
        }
        if (cc == 60 && delta != 0)
        {
            // Jog: mueve el cursor de a semicorcheas (con Shift, de a compás)
            const auto step = shiftDown ? (double) s.project().beatsPerBar : 0.25;
            s.seek (juce::jmax (0.0, std::round ((s.editCursor + delta * step) / step) * step));
        }
        return;
    }

    if (! m.isNoteOnOrOff()) return;
    const int note = m.getNoteNumber();
    const bool down = m.isNoteOn() && m.getVelocity() > 0;

    // Toque de fader: empieza / termina la escritura de automatización (Touch / Latch)
    if (note >= faderTouch && note <= masterTouch)
    {
        const int strip = note - faderTouch;
        touched[(size_t) strip] = down;
        if (strip == 8) { if (down) s.beginGesture(); return; }
        if (auto* t = stripTrack (strip))
        {
            if (down) { s.touchBegin (t->id, { AutomationTarget::Kind::volume }); valueUntil[(size_t) strip] = now + 60000; }
            else { s.touchEnd (t->id, { AutomationTarget::Kind::volume }); valueUntil[(size_t) strip] = now + 1200; }
        }
        return;
    }
    if (note == shift) { shiftDown = down; return; }
    if (down) pressed (note);
}

void MackieControl::stripButton (int kind, int strip)
{
    auto& s = *session;
    auto* t = stripTrack (strip);
    if (t == nullptr) return;
    const auto id = t->id;
    switch (kind)
    {
        case recArm:  if (! t->isBus()) s.updateTrack (id, [] (Track& x) { x.armed = ! x.armed; }); break;
        case solo:    s.updateTrack (id, [] (Track& x) { x.solo = ! x.solo; }); break;
        case mute:    s.updateTrack (id, [] (Track& x) { x.muted = ! x.muted; }); break;
        case select:  s.selectTrack (id); break;
        case vpotPush:
            if (mode == Mode::pan) { s.beginGesture(); s.setControl (id, { AutomationTarget::Kind::pan }, 0.0); }
            else if (auto* snd = stripSend (*t))
                s.updateTrack (id, [sid = snd->id] (Track& x) { for (auto& e : x.sends) if (e.id == sid) e.muted = ! e.muted; });
            else
            {
                const auto buses = s.project().buses();
                if (sendBus < (int) buses.size()) s.addSend (id, buses[(size_t) sendBus]->id);
            }
            break;
        default: break;
    }
}

void MackieControl::selectRelative (int delta)
{
    auto& s = *session;
    const auto ids = visibleTracks();
    if (ids.empty()) return;
    auto it = std::find (ids.begin(), ids.end(), s.selectedTrack);
    int i = it == ids.end() ? 0 : (int) (it - ids.begin()) + delta;
    i = juce::jlimit (0, (int) ids.size() - 1, i);
    s.selectTrack (ids[(size_t) i]);
    // El banco sigue a la selección
    if (i < bankOffset) bankOffset = i;
    if (i >= bankOffset + 8) bankOffset = i - 7;
}

void MackieControl::pressed (int note)
{
    auto& s = *session;
    if (note < 40) { stripButton (note / 8 * 8, note % 8); return; }
    if (note >= f1 && note < f1 + 8) { s.goToMarker (note - f1 + 1); return; }

    auto setMode = [&s] (AutomationMode m)
    {
        if (s.project().findTrack (s.selectedTrack) != nullptr)
            s.updateTrack (s.selectedTrack, [m] (Track& t) { t.automationMode = m; });
    };
    switch (note)
    {
        case assignTrack: case assignPan: mode = Mode::pan; break;
        case assignSend:
        {
            const auto n = (int) s.project().buses().size();
            if (mode == Mode::send && n > 0) sendBus = (sendBus + 1) % n;   // otra vez SEND: el bus siguiente
            mode = Mode::send;
            break;
        }
        case bankLeft:     bankOffset -= 8; clampBank(); break;
        case bankRight:    bankOffset += 8; clampBank(); break;
        case channelLeft:  bankOffset -= 1; clampBank(); break;
        case channelRight: bankOffset += 1; clampBank(); break;
        case nameValue:    showValues = ! showValues; break;
        case readOff:
        {
            auto* t = s.project().findTrack (s.selectedTrack);
            setMode (t != nullptr && t->automationMode == AutomationMode::read ? AutomationMode::off : AutomationMode::read);
            break;
        }
        case write: case latch: setMode (AutomationMode::latch); break;
        case touch:  setMode (AutomationMode::touch); break;
        case trim:   setMode (AutomationMode::off); break;
        case save:   s.saveNow(); break;
        case undo:   if (shiftDown) s.redo(); else s.undo(); break;
        case marker: s.insertMarker(); break;
        case cycle:  s.toggleRepeat(); break;
        case click:  s.edit ([] (Project& p) { p.clickEnabled = ! p.clickEnabled; }); break;
        case soloGlobal: s.edit ([] (Project& p) { for (auto& t : p.tracks) t.solo = false; }); break;
        case rewind:      s.goToPreviousStop(); break;
        case fastForward: s.goToNextStop(); break;
        case stop:   if (s.isPlaying()) s.stop(); else s.goToStart(); break;   // Stop con todo parado: al inicio
        case play:   if (! s.isPlaying()) s.play(); break;
        case record: s.toggleRecord(); break;
        case cursorUp:   selectRelative (-1); break;
        case cursorDown: selectRelative (1); break;
        case cursorLeft:  s.seek (juce::jmax (0.0, s.editCursor - s.project().beatsPerBar)); break;
        case cursorRight: s.seek (s.editCursor + s.project().beatsPerBar); break;
        default: break;
    }
}

//==============================================================================
void MackieControl::setLed (int note, bool on)
{
    if (note < 0 || note >= 128) return;
    const int v = on ? 127 : 0;
    if (sentLed[(size_t) note] == v) return;
    sentLed[(size_t) note] = v;
    send (juce::MidiMessage::noteOn (1, note, (juce::uint8) v));
}

void MackieControl::sendLcd (int line, const juce::String& text)
{
    const auto padded = lcdText (text, 56);
    if (sentLcd[line] == padded) return;
    sentLcd[line] = padded;
    juce::MemoryBlock data;
    const juce::uint8 header[] = { 0x00, 0x00, 0x66, 0x14, 0x12, (juce::uint8) (line * 56) };
    data.append (header, sizeof (header));
    for (auto c : padded) { const auto b = (juce::uint8) (c & 0x7F); data.append (&b, 1); }
    send (juce::MidiMessage::createSysExMessage (data.getData(), (int) data.getSize()));
}

void MackieControl::sendDigits (const juce::String& text, int firstCc, int count)
{
    // El primer CC es el dígito de la derecha
    for (int i = 0; i < count; ++i)
    {
        const auto c = text.length() - 1 - i >= 0 ? text[text.length() - 1 - i] : ' ';
        const auto v = segmentChar (c);
        const auto slot = (size_t) (firstCc - 0x40 + i);
        if (slot < sentDigits.size() && sentDigits[slot] == v) continue;
        if (slot < sentDigits.size()) sentDigits[slot] = v;
        send (juce::MidiMessage::controllerEvent (1, firstCc + i, v));
    }
}

juce::String MackieControl::stripValue (int strip) const
{
    auto* t = stripTrack (strip);
    if (t == nullptr) return {};
    auto& s = *session;
    const auto now = juce::Time::getMillisecondCounter();
    // Tocando el fader (o recién movido): el volumen en dB
    if (touched[(size_t) strip] || (now < valueUntil[(size_t) strip] && now - lastVpotTurn[(size_t) strip] > 1500))
    {
        const auto db = s.effectiveValue (*t, { AutomationTarget::Kind::volume });
        return db <= Decibels::silence ? juce::String ("-inf") : juce::String (db, 1);
    }
    if (mode == Mode::send)
    {
        auto* snd = stripSend (*t);
        if (snd == nullptr) return " --";
        const auto db = s.effectiveValue (*t, { AutomationTarget::Kind::sendLevel, snd->id });
        return snd->muted ? juce::String ("mute") : db <= Decibels::silence ? juce::String ("-inf") : juce::String (db, 1);
    }
    const auto pan = s.effectiveValue (*t, { AutomationTarget::Kind::pan });
    if (std::abs (pan) < 0.01) return "  C";
    return (pan < 0 ? "L" : "R") + juce::String ((int) std::round (std::abs (pan) * 100));
}

void MackieControl::resendAll()
{
    sentFader.fill (-1); sentRing.fill (-1); sentLed.fill (-1); sentDigits.fill (-1);
    sentLcd[0] = sentLcd[1] = "\x01";
    updateSurface();
}

void MackieControl::updateSurface()
{
    if (output == nullptr && ! sendHook) return;
    if (session == nullptr)
    {
        sendLcd (0, "  Worship DAW");
        sendLcd (1, "  Abre un proyecto para usar la superficie de control");
        for (int i = 0; i < 128; ++i) setLed (i, false);
        return;
    }
    auto& s = *session;
    auto& p = s.project();
    clampBank();

    juce::String top, bottom;
    for (int strip = 0; strip < 8; ++strip)
    {
        auto* t = stripTrack (strip);
        // Fader (no se mueve mientras el usuario lo toca)
        if (! touched[(size_t) strip])
        {
            const int v = t == nullptr ? 0 : (int) std::round (Decibels::faderPosition (s.effectiveValue (*t, { AutomationTarget::Kind::volume })) * faderMax);
            if (sentFader[(size_t) strip] != v) { sentFader[(size_t) strip] = v; send (juce::MidiMessage::pitchWheel (strip + 1, v)); }
        }
        // Anillo del V-Pot: paneo (un punto) o nivel de envío (barra)
        int ring = 0;
        if (t != nullptr)
        {
            if (mode == Mode::pan)
            {
                const auto pan = s.effectiveValue (*t, { AutomationTarget::Kind::pan });
                ring = 0x40 | (1 + (int) std::round ((pan + 1.0) * 5.0));
            }
            else if (auto* snd = stripSend (*t))
            {
                const auto pos = Decibels::faderPosition (s.effectiveValue (*t, { AutomationTarget::Kind::sendLevel, snd->id }));
                ring = 0x20 | juce::jlimit (0, 11, (int) std::round (pos * 11.0));
            }
        }
        if (sentRing[(size_t) strip] != ring) { sentRing[(size_t) strip] = ring; send (juce::MidiMessage::controllerEvent (1, 48 + strip, ring)); }

        setLed (recArm + strip, t != nullptr && t->armed);
        setLed (solo + strip, t != nullptr && t->solo);
        setLed (mute + strip, t != nullptr && t->muted);
        setLed (select + strip, t != nullptr && s.isTrackSelected (t->id));

        // Pantalla: 7 caracteres por canal
        const auto name = t == nullptr ? juce::String() : (t->isFolder ? "[" + t->name : t->name);
        top << lcdText (name, 6) << " ";
        bottom << lcdText (stripValue (strip).trimStart(), 6).paddedLeft (' ', 6) << " ";
    }
    if (showValues)
    {
        // Name/Value: la línea de arriba muestra el volumen en dB
        top = {};
        for (int strip = 0; strip < 8; ++strip)
        {
            auto* t = stripTrack (strip);
            const auto db = t != nullptr ? s.effectiveValue (*t, { AutomationTarget::Kind::volume }) : 0.0;
            top << lcdText (t == nullptr ? juce::String() : db <= Decibels::silence ? juce::String ("-inf") : juce::String (db, 1), 6) << " ";
        }
    }
    sendLcd (0, top);
    sendLcd (1, bottom);

    // Master
    if (! touched[8])
    {
        const int v = (int) std::round (Decibels::faderPosition (p.masterVolumeDb) * faderMax);
        if (sentFader[8] != v) { sentFader[8] = v; send (juce::MidiMessage::pitchWheel (9, v)); }
    }

    // Transporte y estados
    setLed (play, s.isPlaying());
    setLed (stop, ! s.isPlaying());
    setLed (record, s.isRecording());
    setLed (cycle, p.loopEnabled);
    setLed (click, p.clickEnabled);
    setLed (soloGlobal, p.hasSolo());
    setLed (assignPan, mode == Mode::pan);
    setLed (assignSend, mode == Mode::send);
    setLed (nameValue, showValues);
    const auto* sel = p.findTrack (s.selectedTrack);
    const auto am = sel != nullptr ? sel->automationMode : AutomationMode::read;
    setLed (readOff, sel != nullptr && am == AutomationMode::read);
    setLed (touch, sel != nullptr && am == AutomationMode::touch);
    setLed (latch, sel != nullptr && am == AutomationMode::latch);
    setLed (trim, sel != nullptr && am == AutomationMode::off);

    // Contador: compás (3) tiempo (2) semicorchea (2) ticks (3)
    {
        const auto b = juce::jmax (0.0, s.playhead());
        const int bar = (int) (b / p.beatsPerBar) + 1;
        const auto inBar = b - (bar - 1) * p.beatsPerBar;
        const int beat = (int) inBar + 1;
        const auto frac = inBar - (beat - 1);
        const int sixteenth = (int) (frac * 4) + 1;
        const int ticks = (int) ((frac * 4 - (sixteenth - 1)) * 1000) % 1000;
        const auto text = juce::String (bar % 1000).paddedLeft (' ', 3) + juce::String (beat).paddedLeft (' ', 2)
                        + juce::String (sixteenth).paddedLeft ('0', 2) + juce::String (ticks).paddedLeft ('0', 3);
        sendDigits (text, 0x40, 10);
        // Asignación (2 dígitos): "PN" o "S1", "S2"…
        sendDigits (mode == Mode::pan ? juce::String ("PN") : "S" + juce::String ((sendBus + 1) % 10), 0x4A, 2);
    }

    // Medidores (aftertouch de canal: canal << 4 | nivel 0…12), ~10 veces por segundo
    const auto now = juce::Time::getMillisecondCounter();
    if (now - lastMeters >= 100 && trackLevel)
    {
        lastMeters = now;
        for (int strip = 0; strip < 8; ++strip)
            if (auto* t = stripTrack (strip))
            {
                const auto level = trackLevel (t->id);
                const auto db = level > 0 ? juce::Decibels::gainToDecibels (level) : -100.0f;
                const int lvl = db >= 0 ? 12 : juce::jlimit (0, 11, (int) std::round ((db + 60.0f) / 60.0f * 12.0f));
                if (lvl > 0) send (juce::MidiMessage::channelPressureChange (1, (strip << 4) | lvl));
            }
    }
}

} // namespace wdaw
