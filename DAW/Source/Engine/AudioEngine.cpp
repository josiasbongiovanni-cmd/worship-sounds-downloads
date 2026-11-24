#include "AudioEngine.h"
#include "Model/Serialization.h"

namespace wdaw
{

//==============================================================================
DelayLine::DelayLine (int cap) : buffer (2, juce::jmax (1, cap)) { buffer.clear(); }

void DelayLine::process (float* const* ch, int numChannels, int n, int delay) noexcept
{
    const int size = buffer.getNumSamples();
    if (delay <= 0 || delay >= size) return;
    for (int i = 0; i < n; ++i)
    {
        const int readPos = (writePos - delay + size) % size;
        for (int c = 0; c < juce::jmin (2, numChannels); ++c)
        {
            auto* d = buffer.getWritePointer (c);
            d[writePos] = ch[c][i];
            ch[c][i] = d[readPos];
        }
        writePos = (writePos + 1) % size;
    }
}

float Envelope::at (juce::int64 pos) const noexcept
{
    if (points.empty()) return 0;
    if (pos <= points.front().pos) return points.front().value;
    if (pos >= points.back().pos) return points.back().value;
    auto it = std::upper_bound (points.begin(), points.end(), pos, [] (juce::int64 p, const Point& e) { return p < e.pos; });
    const auto& b = *it;
    const auto& a = *(it - 1);
    if (b.pos <= a.pos) return b.value;
    const auto x = (double) (pos - a.pos) / (double) (b.pos - a.pos);
    return a.value + (b.value - a.value) * (float) AutomationPoint::curve (a.shape, a.tension, x);
}

void TrackDSP::prepare (double sr, int maxBlock)
{
    buffer.setSize (2, maxBlock, false, true, true);
    instrumentBuffer.setSize (2, maxBlock, false, true, true);
    scratch.setSize (2, maxBlock * 8 + 64, false, true, true);
    midi.ensureSize (4096);
    juce::dsp::ProcessSpec spec { sr, (juce::uint32) maxBlock, 1 };
    for (auto& f : eq) { f.prepare (spec); f.reset(); }
    busInput.setSize (2, maxBlock, false, true, true);
    sendScratch.setSize (2, maxBlock, false, true, true);
    curL = curR = -1;
    for (auto& [id, line] : connections) { line->buffer.clear(); line->curL = line->curR = -1; }
    for (auto& v : voices) { v.nextOut = -1; v.interp[0].reset(); v.interp[1].reset(); }
}

//==============================================================================
struct AudioEngine::Prefetcher : juce::Thread
{
    explicit Prefetcher (AudioEngine& e) : juce::Thread ("Precarga de audio"), engine (e) { startThread (juce::Thread::Priority::low); }
    ~Prefetcher() override { stopThread (2000); }

    void run() override
    {
        while (! threadShouldExit())
        {
            if (auto snap = engine.currentSnapshot())
            {
                const auto pos = engine.position.load();
                const auto ahead = (juce::int64) (snap->sampleRate * 4);
                auto touchRange = [&] (juce::int64 from, juce::int64 to)
                {
                    for (auto& t : snap->tracks)
                        for (auto& c : t.clips)
                        {
                            const auto a = juce::jmax (from, c.start), b = juce::jmin (to, c.start + c.length);
                            if (a < b && c.file != nullptr)
                                c.file->touch ((juce::int64) (c.fileStart + (double) (a - c.start) * c.ratio),
                                               (juce::int64) ((double) (b - a) * c.ratio) + 16);
                        }
                };
                touchRange (pos, pos + ahead);
                if (snap->loopOn) touchRange (snap->loopStart, snap->loopStart + ahead);
            }
            wait (40);
        }
    }

    AudioEngine& engine;
};

//==============================================================================
AudioEngine::AudioEngine (PluginManager& pm) : pluginManager (pm)
{
    for (auto& p : inputPeaks) p = 0.0f;
    lastLiveTargets.reserve (256);
    writerThread.startThread (juce::Thread::Priority::high);
    prepareBuffers (48000, 512, 2, 2);
    prefetcher = std::make_unique<Prefetcher> (*this);
    startTimer (100);
}

AudioEngine::~AudioEngine()
{
    stopTimer();
    devices.removeAudioCallback (this);
    devices.removeMidiInputDeviceCallback ({}, this);
    devices.closeAudioDevice();
    prefetcher.reset();
    if (activeSession != nullptr) stopRecording();
    writerThread.stopThread (5000);
    audioSnapshot = nullptr;
    published = nullptr;
    retired.clear();
}

juce::String AudioEngine::openDevices (const juce::XmlElement* savedState)
{
    auto error = devices.initialise (256, 256, savedState, true);
    enableAllChannels();
    devices.addAudioCallback (this);
    devices.addMidiInputDeviceCallback ({}, this);
    return error;
}

void AudioEngine::enableAllChannels()
{
    if (auto* d = devices.getCurrentAudioDevice())
    {
        auto setup = devices.getAudioDeviceSetup();
        setup.useDefaultInputChannels = false;
        setup.useDefaultOutputChannels = false;
        setup.inputChannels.clear();
        setup.outputChannels.clear();
        setup.inputChannels.setRange (0, d->getInputChannelNames().size(), true);
        setup.outputChannels.setRange (0, d->getOutputChannelNames().size(), true);
        devices.setAudioDeviceSetup (setup, true);
    }
}

double AudioEngine::reportedRoundTripSeconds() const
{
    if (auto* d = devices.getCurrentAudioDevice())
    {
        const auto sr = d->getCurrentSampleRate();
        if (sr > 0) return (d->getInputLatencyInSamples() + d->getOutputLatencyInSamples()) / sr;
    }
    return 0;
}

float AudioEngine::cpuLoad() const { return (float) devices.getCpuUsage(); }

void AudioEngine::refreshMidiInputs (const juce::StringArray& disabled)
{
    for (auto& info : juce::MidiInput::getAvailableDevices())
    {
        const bool enable = ! disabled.contains (info.identifier);
        if (devices.isMidiInputDeviceEnabled (info.identifier) != enable)
            devices.setMidiInputDeviceEnabled (info.identifier, enable);
    }
}

void AudioEngine::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m)
{
    if (midiFilter && midiFilter (m)) return;   // usado por Aprender MIDI / una asignación
    if (m.isActiveSense() || m.isMidiClock()) return;
    liveMidi.addMessageToQueue (m);
}

//==============================================================================
void AudioEngine::prepareBuffers (double sr, int bs, int numIns, int numOuts)
{
    sampleRate = sr;
    blockSize = bs;
    numInputs = numIns;
    numOutputs = numOuts;
    maxBlock = juce::jmax (bs, 4096);
    masterBus.setSize (2, maxBlock, false, true, true);
    directBus.setSize (juce::jmax (2, numOuts), maxBlock, false, true, true);
    zeros.setSize (1, maxBlock, false, true, true);
    zeros.clear();
    liveBlock.ensureSize (8192);
    liveMidi.reset (sr);
    clickDecay = std::exp (-1.0 / (0.012 * sr));
    for (auto& [id, dsp] : trackDSP) dsp->prepare (sr, maxBlock);
}

void AudioEngine::preparePlugins()
{
    for (auto& [id, p] : plugins)
        PluginManager::prepare (*p->processor, sampleRate.load(), maxBlock, p->isInstrument);
}

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* d)
{
    const auto oldRate = sampleRate.load();
    prepareBuffers (d->getCurrentSampleRate(), d->getCurrentBufferSizeSamples(),
                    d->getActiveInputChannels().countNumberOfSetBits(), d->getActiveOutputChannels().countNumberOfSetBits());
    if (oldRate > 0 && oldRate != sampleRate.load())
        position = (juce::int64) ((double) position.load() * sampleRate.load() / oldRate);
    preparePlugins();
    juce::MessageManager::callAsync ([this] { rebuildSnapshot(); });
    rebuildSnapshot();
}

void AudioEngine::audioDeviceStopped() {}

void AudioEngine::audioDeviceError (const juce::String& e)
{
    juce::MessageManager::callAsync ([this, e] { if (onError) onError ("Error del dispositivo de audio: " + e); });
}

void AudioEngine::prepareForTesting (double sr, int bs, int numIns, int numOuts)
{
    prepareBuffers (sr, bs, numIns, numOuts);
    preparePlugins();
    rebuildSnapshot();
}

//==============================================================================
void AudioEngine::setProject (const Project& p, const juce::File& folder)
{
    project = p;
    projectFolder = folder;
    fileCache.setProjectFolder (folder);
    loadMissingPlugins();
    rebuildSnapshot();
}

void AudioEngine::setLiveMidiTargets (const std::vector<juce::Uuid>& ids)
{
    liveTargetIds = ids;
    rebuildSnapshot();
}

void AudioEngine::loadMissingPlugins()
{
    std::vector<PluginSlot> wanted;
    for (auto& t : project.tracks)
    {
        for (auto& s : t.inserts) wanted.push_back (s);
        if (t.isInstrument()) wanted.push_back (t.instrument ? *t.instrument : PluginManager::defaultInstrumentSlot());
    }
    for (auto& s : project.masterInserts) wanted.push_back (s);

    // Quitar los que ya no están (el snapshot retirado los mantiene vivos hasta que sea seguro liberarlos).
    for (auto it = plugins.begin(); it != plugins.end();)
    {
        const bool used = std::any_of (wanted.begin(), wanted.end(), [&] (auto& s) { return s.id == it->first; });
        if (! used) { retired.emplace_back (it->second.get()); it = plugins.erase (it); }
        else ++it;
    }

    for (auto& slot : wanted)
    {
        if (plugins.count (slot.id) > 0 || pendingPlugins.count (slot.id) > 0) continue;
        pendingPlugins.insert (slot.id);
        pluginManager.createInstance (slot, sampleRate.load(), maxBlock,
            [this, id = slot.id, name = slot.name] (PluginInstance::Ptr inst, juce::String error)
            {
                pendingPlugins.erase (id);
                if (inst == nullptr)
                {
                    if (onError) onError (error.isNotEmpty() ? error : "No se pudo cargar " + name);
                    return;
                }
                inst->slotId = id;
                inst->processor->setPlayHead (&hostPlayHead);
                plugins[id] = inst;
                rebuildSnapshot();
                if (onPluginsLoaded) onPluginsLoaded();
            });
    }
}

PluginInstance::Ptr AudioEngine::findPlugin (const juce::Uuid& slotId) const
{
    if (auto it = plugins.find (slotId); it != plugins.end()) return it->second;
    return {};
}

std::map<juce::Uuid, juce::MemoryBlock> AudioEngine::capturePluginStates() const
{
    std::map<juce::Uuid, juce::MemoryBlock> states;
    for (auto& [id, p] : plugins)
    {
        juce::MemoryBlock mb;
        p->processor->getStateInformation (mb);
        states[id] = std::move (mb);
    }
    return states;
}

PlaySnapshot::Ptr AudioEngine::currentSnapshot() const
{
    const juce::SpinLock::ScopedLockType sl (const_cast<juce::SpinLock&> (snapshotLock));
    return published;
}

void AudioEngine::rebuildSnapshot()
{
    PlaySnapshot::Ptr s (new PlaySnapshot());
    const auto sr = sampleRate.load();
    const auto tl = project.timeline();
    s->sampleRate = sr;
    s->samplesPerBeat = tl.secondsPerBeat() * sr;
    s->beatsPerBar = project.beatsPerBar;
    s->masterGain = Decibels::toGain (project.masterVolumeDb);
    s->clickOn = project.clickEnabled;
    s->clickGain = Decibels::toGain (project.clickVolumeDb);
    s->clickOutPair = project.clickOutPair;
    s->loopOn = project.loopEnabled && project.loopEnd > project.loopStart + 0.01;
    s->loopStart = (juce::int64) (project.loopStart * s->samplesPerBeat);
    s->loopEnd = (juce::int64) (project.loopEnd * s->samplesPerBeat);
    auto toSamples = [&] (Beats b) { return (juce::int64) std::llround (b * s->samplesPerBeat); };

    // DSP persistente por pista
    for (auto it = trackDSP.begin(); it != trackDSP.end();)
    {
        if (project.findTrack (it->first) == nullptr) { retired.emplace_back (it->second.get()); it = trackDSP.erase (it); }
        else ++it;
    }

    for (auto& m : project.masterInserts)
        if (auto p = findPlugin (m.id)) { s->master.push_back (p); s->masterBypassed.push_back (m.bypassed); }

    auto envelope = [&] (const AutomationLane* lane)
    {
        Envelope e;
        if (lane != nullptr)
            for (auto& pt : lane->points) e.points.push_back ({ toSamples (pt.time), (float) pt.value, pt.shape, (float) pt.tension });
        return e;
    };

    std::vector<int> latencies;
    for (auto& t : project.tracks)
    {
        PlaySnapshot::Track pt;
        pt.id = t.id;
        auto& dsp = trackDSP[t.id];
        if (dsp == nullptr) { dsp = TrackDSP::Ptr (new TrackDSP()); dsp->prepare (sr, maxBlock); }
        pt.dsp = dsp;
        pt.isBus = t.isBus();
        pt.audible = project.isAudible (t);
        pt.volumeDb = (float) t.volumeDb;
        pt.pan = (float) t.pan;
        pt.volumeEnv = envelope (t.playingLane ({ AutomationTarget::Kind::volume }));
        pt.panEnv = envelope (t.playingLane ({ AutomationTarget::Kind::pan }));

        for (auto& c : t.clips)
        {
            if (t.isBus()) break;
            auto file = fileCache.get (c.file);
            if (file == nullptr) continue;
            PlaySnapshot::Clip pc;
            pc.id = c.id;
            pc.file = file;
            pc.start = toSamples (c.start);
            pc.length = (juce::int64) std::llround (c.length * sr);
            pc.ratio = file->sampleRate / sr;
            pc.fileStart = c.offset * file->sampleRate;
            pc.gain = Decibels::toGain (c.gainDb);
            pc.fadeIn = juce::jmax (0, (int) (c.fadeIn * sr));
            pc.fadeOut = juce::jmax (0, (int) (c.fadeOut * sr));
            pt.clips.push_back (pc);
        }

        if (t.isInstrument())
        {
            const auto slot = t.instrument ? *t.instrument : PluginManager::defaultInstrumentSlot();
            pt.instrument = findPlugin (slot.id);
            for (auto& clip : t.midiClips)
            {
                for (auto& n : clip.notes)
                {
                    const auto on = clip.start + n.start;
                    const auto off = juce::jmin (clip.end(), clip.start + n.end());
                    if (off <= on) continue;
                    const auto ch = juce::jlimit (1, 16, n.channel);
                    pt.midi.push_back ({ toSamples (on), juce::MidiMessage::noteOn (ch, n.pitch, (juce::uint8) juce::jlimit (1, 127, n.velocity)) });
                    pt.midi.push_back ({ toSamples (off), juce::MidiMessage::noteOff (ch, n.pitch) });
                }
                for (auto& x : clip.controls)
                {
                    const auto at = toSamples (clip.start + x.time);
                    const auto ch = juce::jlimit (1, 16, x.channel);
                    if (x.controller == 128) pt.midi.push_back ({ at, juce::MidiMessage::pitchWheel (ch, x.value) });
                    else if (x.controller == 129) pt.midi.push_back ({ at, juce::MidiMessage::channelPressureChange (ch, x.value) });
                    else pt.midi.push_back ({ at, juce::MidiMessage::controllerEvent (ch, x.controller, x.value) });
                }
                // Al final del ítem: soltar el pedal para que no quede sostenido.
                pt.midi.push_back ({ toSamples (clip.end()), juce::MidiMessage::controllerEvent (1, 64, 0) });
            }
            // Los note-off antes que los note-on en la misma muestra.
            std::stable_sort (pt.midi.begin(), pt.midi.end(), [] (auto& a, auto& b)
            {
                if (a.pos != b.pos) return a.pos < b.pos;
                return a.message.isNoteOff() && ! b.message.isNoteOff();
            });
        }

        int latency = pt.instrument != nullptr ? pt.instrument->processor->getLatencySamples() : 0;
        for (auto& ins : t.inserts)
            if (auto p = findPlugin (ins.id))
            {
                pt.inserts.push_back (p);
                pt.bypassed.push_back (ins.bypassed);
                if (! ins.bypassed) latency += p->processor->getLatencySamples();
            }
        latencies.push_back (latency);

        // Automatización de parámetros de plugins
        for (auto& lane : t.automation)
        {
            if (lane.target.kind != AutomationTarget::Kind::pluginParam) continue;
            auto* playing = t.playingLane (lane.target);
            auto plugin = playing != nullptr ? findPlugin (lane.target.ref) : PluginInstance::Ptr();
            if (plugin == nullptr || lane.target.param < 0 || lane.target.param >= plugin->processor->getParameters().size()) continue;
            pt.params.push_back ({ plugin, lane.target.param, envelope (playing) });
        }

        pt.eqOn = t.eq.enabled && ! t.eq.isFlat();
        if (pt.eqOn)
        {
            using C = juce::dsp::IIR::Coefficients<float>;
            pt.eq[0] = C::makeLowShelf (sr, 100.0f, 0.707f, juce::Decibels::decibelsToGain (t.eq.lowGain));
            pt.eq[1] = C::makePeakFilter (sr, 1000.0f, 0.7f, juce::Decibels::decibelsToGain (t.eq.midGain));
            pt.eq[2] = C::makeHighShelf (sr, 8000.0f, 0.707f, juce::Decibels::decibelsToGain (t.eq.highGain));
        }
        pt.armed = t.armed && ! t.isBus();
        pt.isInstrument = t.isInstrument();
        pt.input = t.input;
        pt.monitor = t.monitor;
        pt.directOutPair = t.directOutPair;
        s->tracks.push_back (std::move (pt));
    }

    // Ruteo: salida y envíos de cada pista (los destinos que no son buses válidos van al Master / se ignoran).
    const int numTracks = (int) project.tracks.size();
    std::vector<std::vector<int>> edges ((size_t) numTracks);
    for (int i = 0; i < numTracks; ++i)
    {
        const auto& t = project.tracks[(size_t) i];
        auto& pt = s->tracks[(size_t) i];
        pt.output.target = PlaySnapshot::Track::Connection::master;
        if (t.directOutPair >= 2) pt.output.target = PlaySnapshot::Track::Connection::direct;
        else if (const auto out = project.outputOf (t); ! out.isNull())
            if (auto bi = project.trackIndex (out); bi >= 0 && bi != i && project.tracks[(size_t) bi].isBus())
                pt.output.target = bi;
        if (pt.output.target >= 0) edges[(size_t) i].push_back (pt.output.target);
        for (auto& snd : t.sends)
        {
            const auto bi = project.trackIndex (snd.target);
            if (bi < 0 || bi == i || ! project.tracks[(size_t) bi].isBus()) continue;
            PlaySnapshot::Track::Connection c;
            c.target = bi;
            c.key = snd.id;
            c.levelDb = (float) snd.levelDb;
            c.pan = (float) snd.pan;
            c.preFader = snd.preFader;
            c.muted = snd.muted;
            c.levelEnv = envelope (t.playingLane ({ AutomationTarget::Kind::sendLevel, snd.id }));
            pt.sends.push_back (std::move (c));
            edges[(size_t) i].push_back (bi);
        }
    }

    // Orden topológico (Kahn): una pista se procesa después de todo lo que la alimenta.
    {
        std::vector<int> indegree ((size_t) numTracks, 0);
        for (auto& e : edges) for (auto d : e) ++indegree[(size_t) d];
        std::vector<int> ready;
        for (int i = numTracks - 1; i >= 0; --i) if (indegree[(size_t) i] == 0) ready.push_back (i);
        std::vector<bool> done ((size_t) numTracks, false);
        while (! ready.empty())
        {
            const auto i = ready.back();
            ready.pop_back();
            s->order.push_back (i);
            done[(size_t) i] = true;
            for (auto d : edges[(size_t) i]) if (--indegree[(size_t) d] == 0) ready.push_back (d);
        }
        for (int i = 0; i < numTracks; ++i) if (! done[(size_t) i]) s->order.push_back (i);   // bucle (archivo dañado): no se cuelga
    }

    // Compensación de latencia por conexión: cada destino espera a su entrada más lenta.
    std::vector<int> arrival ((size_t) numTracks, 0), inputTime ((size_t) numTracks, 0);
    for (auto i : s->order)
    {
        arrival[(size_t) i] = inputTime[(size_t) i] + latencies[(size_t) i];
        for (auto d : edges[(size_t) i]) inputTime[(size_t) d] = juce::jmax (inputTime[(size_t) d], arrival[(size_t) i]);
    }
    s->maxLatency = 0;
    for (int i = 0; i < numTracks; ++i)
        if (s->tracks[(size_t) i].output.target < 0) s->maxLatency = juce::jmax (s->maxLatency, arrival[(size_t) i]);

    auto line = [&] (TrackDSP& dsp, const juce::Uuid& key, int delay)
    {
        auto& l = dsp.connections[key];
        if (l == nullptr || l->capacity() < delay + 1)
        {
            if (l != nullptr) retired.emplace_back (l.get());
            l = DelayLine::Ptr (new DelayLine (juce::nextPowerOfTwo (juce::jmax (64, delay + 1))));
        }
        return l;
    };
    for (int i = 0; i < numTracks; ++i)
    {
        const auto& t = project.tracks[(size_t) i];
        auto& pt = s->tracks[(size_t) i];
        auto& dsp = *pt.dsp;
        const auto destTime = [&] (int target) { return target >= 0 ? inputTime[(size_t) target] : s->maxLatency; };
        pt.output.delay = juce::jmax (0, destTime (pt.output.target) - arrival[(size_t) i]);
        pt.output.line = line (dsp, t.id, pt.output.delay);
        for (auto& c : pt.sends)
        {
            c.delay = juce::jmax (0, destTime (c.target) - arrival[(size_t) i]);
            c.line = line (dsp, c.key, c.delay);
        }
        pt.stemDelay = juce::jmax (0, s->maxLatency - arrival[(size_t) i]);
        pt.stemLine = line (dsp, juce::Uuid ("00000000-0000-0000-0000-00000000057e"), pt.stemDelay);
        // Conexiones que ya no existen
        for (auto it = dsp.connections.begin(); it != dsp.connections.end();)
        {
            const bool used = it->second == pt.output.line || it->second == pt.stemLine
                           || std::any_of (pt.sends.begin(), pt.sends.end(), [&] (auto& c) { return c.line == it->second; });
            if (! used) { retired.emplace_back (it->second.get()); it = dsp.connections.erase (it); }
            else ++it;
        }
    }
    s->bpm = project.bpm;

    for (auto& id : liveTargetIds)
        if (auto i = project.trackIndex (id); i >= 0) s->liveTargets.push_back (i);

    PlaySnapshot::Ptr old;
    {
        const juce::SpinLock::ScopedLockType sl (snapshotLock);
        old = published;
        published = s;
    }
    if (old != nullptr) retired.emplace_back (old.get());
}

void AudioEngine::timerCallback()
{
    // Liberar lo retirado cuando el hilo de audio ya no lo usa (nunca se libera memoria en el hilo de audio).
    retired.erase (std::remove_if (retired.begin(), retired.end(), [] (auto& r) { return r->getReferenceCount() <= 1; }),
                   retired.end());
    drainRecordedMidi();
}

//==============================================================================
void AudioEngine::play (Beats from)
{
    const auto spb = project.timeline().secondsPerBeat() * sampleRate.load();
    seekRequest = (juce::int64) std::llround (from * spb);
    playing = true;
}

void AudioEngine::stop()
{
    playing = false;
}

void AudioEngine::seek (Beats b)
{
    const auto spb = project.timeline().secondsPerBeat() * sampleRate.load();
    seekRequest = (juce::int64) std::llround (b * spb);
    if (! playing) position = seekRequest.load();
}

Beats AudioEngine::playheadBeats() const
{
    const auto sr = sampleRate.load();
    const auto spb = project.timeline().secondsPerBeat() * sr;
    auto pos = position.load();
    if (playing)
        if (auto* d = devices.getCurrentAudioDevice())
            pos -= d->getOutputLatencyInSamples();
    if (auto s = currentSnapshot(); s != nullptr && playing) pos -= s->maxLatency;
    return spb > 0 ? (double) pos / spb : 0;
}

void AudioEngine::previewNote (const juce::Uuid& trackId, int pitch, int velocity)
{
    const auto index = project.trackIndex (trackId);
    if (index < 0) return;
    auto push = [&] (juce::uint8 status, juce::uint8 vel)
    {
        int s1, n1, s2, n2;
        previewFifo.prepareToWrite (1, s1, n1, s2, n2);
        if (n1 + n2 == 0) return;
        auto& p = previewData[(size_t) (n1 > 0 ? s1 : s2)];
        p.trackIndex = index;
        p.data[0] = status; p.data[1] = (juce::uint8) juce::jlimit (0, 127, pitch); p.data[2] = vel;
        previewFifo.finishedWrite (1);
    };
    push (0x90, (juce::uint8) juce::jlimit (1, 127, velocity));
    juce::Timer::callAfterDelay (350, [this, index, pitch]
    {
        int s1, n1, s2, n2;
        previewFifo.prepareToWrite (1, s1, n1, s2, n2);
        if (n1 + n2 == 0) return;
        auto& p = previewData[(size_t) (n1 > 0 ? s1 : s2)];
        p.trackIndex = index;
        p.data[0] = 0x80; p.data[1] = (juce::uint8) juce::jlimit (0, 127, pitch); p.data[2] = 0;
        previewFifo.finishedWrite (1);
    });
}

//==============================================================================
juce::Result AudioEngine::startRecording (const RecordRequest& req, const Project& p, const juce::File& folder)
{
    if (activeSession != nullptr) return juce::Result::fail ("Ya se está grabando."_u);
    setProject (p, folder);
    RecordSession::Ptr session (new RecordSession());
    const auto sr = sampleRate.load();
    const auto spb = p.timeline().secondsPerBeat() * sr;
    session->recordStart = req.keepTransport && playing ? position.load() : (juce::int64) std::llround (req.recordStart * spb);
    session->recordMidi = ! req.midiTracks.empty();
    recordStartBeats = (double) session->recordStart / spb;

    const auto audioDir = io::audioFolder (folder);
    audioDir.createDirectory();
    for (auto& id : req.audioTracks)
    {
        auto* t = p.findTrack (id);
        if (t == nullptr || t->input.isOff()) continue;
        RecordSession::Take take;
        take.trackId = id;
        const auto [l, r] = t->input.channels();
        take.chL = l; take.chR = r;
        take.numChannels = t->input.kind == TrackInput::Kind::stereo ? 2 : 1;
        // La grabación va directo a la carpeta del proyecto: si la app se cierra, la toma no se pierde.
        take.file = audioDir.getNonexistentChildFile (io::sanitize (t->name) + " " + juce::Time::getCurrentTime().formatted ("%H-%M-%S"), ".wav", false);
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream (take.file.createOutputStream());
        if (stream == nullptr) return juce::Result::fail (juce::String::fromUTF8 ("No se puede escribir en la carpeta del proyecto (¿disco lleno o sin permisos?)."));
        auto* writer = wav.createWriterFor (stream.get(), sr, (unsigned) take.numChannels, 24, {}, 0);
        if (writer == nullptr) return juce::Result::fail ("No se pudo crear el archivo de la toma.");
        stream.release();
        take.writer = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (writer, writerThread, (int) (sr * 4));
        session->takes.push_back (std::move (take));
    }

    midiFifo.reset();
    recordedMidi.clear();
    activeSession = session;
    {
        const juce::SpinLock::ScopedLockType sl (recordLock);
        pendingRecord = session;
    }
    recording = true;
    if (! (req.keepTransport && playing.load())) play (req.playFrom);
    return juce::Result::ok();
}

void AudioEngine::drainRecordedMidi()
{
    int s1, n1, s2, n2;
    midiFifo.prepareToRead (midiFifo.getNumReady(), s1, n1, s2, n2);
    for (int i = 0; i < n1; ++i) recordedMidi.push_back (midiFifoData[(size_t) (s1 + i)]);
    for (int i = 0; i < n2; ++i) recordedMidi.push_back (midiFifoData[(size_t) (s2 + i)]);
    midiFifo.finishedRead (n1 + n2);
}

AudioEngine::RecordingResult AudioEngine::stopRecording()
{
    RecordingResult result;
    if (activeSession == nullptr) return result;
    auto session = activeSession;
    activeSession = nullptr;
    {
        const juce::SpinLock::ScopedLockType sl (recordLock);
        pendingRecord = nullptr;
    }
    // Esperar a que el hilo de audio suelte la sesión antes de cerrar los archivos.
    if (devices.getCurrentAudioDevice() != nullptr && devices.getCurrentAudioDevice()->isPlaying())
        for (int i = 0; i < 200 && recordInUse.load() == session.get(); ++i)
            juce::Thread::sleep (2);
    recording = false;

    const auto sr = sampleRate.load();
    const auto spb = project.timeline().secondsPerBeat() * sr;
    const auto endPos = position.load();
    result.start = recordStartBeats;
    result.end = juce::jmax (recordStartBeats, (double) endPos / spb);
    result.overflow = session->overflow.load();

    // Lo grabado llega tarde por la latencia de entrada + salida + compensación de plugins.
    const auto snap = currentSnapshot();
    double latencySec = (useReportedLatency ? reportedRoundTripSeconds() : 0.0)
                      + (snap != nullptr ? snap->maxLatency / sr : 0.0)
                      + recordingOffsetMs / 1000.0;
    latencySec = juce::jmax (0.0, latencySec);

    for (auto& take : session->takes)
    {
        take.writer.reset();   // vacía el buffer al disco
        const auto written = (double) session->samplesWritten.load() / sr;
        if (written <= latencySec + 0.01) { take.file.deleteFile(); continue; }
        TakeResult tr;
        tr.trackId = take.trackId;
        tr.fileName = take.file.getFileName();
        tr.offsetSeconds = latencySec;
        tr.sourceSeconds = written;
        tr.lengthSeconds = written - latencySec;
        result.audio.push_back (tr);
    }

    // MIDI: notas, pedal de sustain, modulación, expresión y pitch bend.
    drainRecordedMidi();
    if (session->recordMidi && ! recordedMidi.empty())
    {
        const auto outLatency = devices.getCurrentAudioDevice() != nullptr
                                    ? devices.getCurrentAudioDevice()->getOutputLatencyInSamples() : 0;
        const auto compensate = outLatency + (snap != nullptr ? snap->maxLatency : 0);
        MidiClip clip;
        clip.start = recordStartBeats;
        clip.length = juce::jmax (0.25, result.end - recordStartBeats);
        std::map<int, std::vector<std::pair<Beats, int>>> open;   // (canal<<8 | nota) → inicios
        for (auto& e : recordedMidi)
        {
            const auto beat = (double) (e.pos - compensate) / spb - recordStartBeats;
            const juce::MidiMessage m (e.data, e.size);
            const auto ch = m.getChannel();
            if (m.isNoteOn())
                open[ch << 8 | m.getNoteNumber()].push_back ({ juce::jmax (0.0, beat), m.getVelocity() });
            else if (m.isNoteOff())
            {
                auto& stack = open[ch << 8 | m.getNoteNumber()];
                if (stack.empty()) continue;
                auto [start, vel] = stack.front();
                stack.erase (stack.begin());
                MidiNote n; n.pitch = m.getNoteNumber(); n.velocity = vel; n.channel = ch;
                n.start = start; n.length = juce::jmax (0.01, beat - start);
                clip.notes.push_back (n);
            }
            else if (m.isController())
                clip.controls.push_back ({ juce::jmax (0.0, beat), ch, m.getControllerNumber(), m.getControllerValue() });
            else if (m.isPitchWheel())
                clip.controls.push_back ({ juce::jmax (0.0, beat), ch, 128, m.getPitchWheelValue() });
            else if (m.isChannelPressure())
                clip.controls.push_back ({ juce::jmax (0.0, beat), ch, 129, m.getChannelPressureValue() });
        }
        for (auto& [key, stack] : open)
            for (auto& [start, vel] : stack)
            {
                MidiNote n; n.pitch = key & 0xff; n.channel = key >> 8; n.velocity = vel;
                n.start = start; n.length = juce::jmax (0.01, clip.length - start);
                clip.notes.push_back (n);
            }
        if (! clip.notes.empty() || ! clip.controls.empty())
        {
            clip.sortNotes();
            clip.fitToNotes();
            for (auto& t : project.tracks)
                if (t.armed && t.isInstrument())
                {
                    auto c = clip;
                    c.id = juce::Uuid();
                    for (auto& n : c.notes) n.id = juce::Uuid();
                    c.name = t.name + " · MIDI"_u;
                    result.midi.push_back ({ t.id, c });
                }
        }
    }
    recordedMidi.clear();
    return result;
}

//==============================================================================
float AudioEngine::takeTrackPeak (const juce::Uuid& id)
{
    if (auto it = trackDSP.find (id); it != trackDSP.end()) return it->second->peak.exchange (0.0f);
    return 0;
}

float AudioEngine::takeMasterPeak() { return masterPeak.exchange (0.0f); }

float AudioEngine::takeInputPeak (int ch)
{
    return ch >= 0 && ch < (int) inputPeaks.size() ? inputPeaks[(size_t) ch].exchange (0.0f) : 0.0f;
}

//==============================================================================
void AudioEngine::beginOffline()
{
    offline = true;
    // Esperar a que termine el callback en curso (si hay dispositivo).
    const auto before = callbacksDone.load();
    if (auto* d = devices.getCurrentAudioDevice(); d != nullptr && d->isPlaying())
        for (int i = 0; i < 100 && callbacksDone.load() < before + 2; ++i) juce::Thread::sleep (2);
    for (auto& [id, p] : plugins) p->processor->setNonRealtime (true);
}

void AudioEngine::endOffline()
{
    for (auto& [id, p] : plugins) p->processor->setNonRealtime (false);
    playing = false;
    offline = false;
}

void AudioEngine::renderOffline (juce::AudioBuffer<float>& out, int n, std::vector<juce::AudioBuffer<float>>* stems)
{
    out.clear();
    renderBlock (nullptr, 0, out.getArrayOfWritePointers(), out.getNumChannels(), n, stems);
}

void AudioEngine::processForTesting (const float* const* in, int numIns, float* const* out, int numOuts, int n)
{
    for (int c = 0; c < numOuts; ++c) juce::FloatVectorOperations::clear (out[c], n);
    renderBlock (in, numIns, out, numOuts, n, nullptr);
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* in, int numIns, float* const* out, int numOuts,
                                                     int n, const juce::AudioIODeviceCallbackContext&)
{
    for (int c = 0; c < numOuts; ++c) if (out[c] != nullptr) juce::FloatVectorOperations::clear (out[c], n);
    if (! offline.load())
        renderBlock (in, numIns, out, numOuts, n, nullptr);
    ++callbacksDone;
}

//==============================================================================
void AudioEngine::renderBlock (const float* const* in, int numIns, float* const* out, int numOuts, int n,
                               std::vector<juce::AudioBuffer<float>>* stems)
{
    juce::ScopedNoDenormals noDenormals;

    // Tomar el último snapshot (si el hilo de mensajes lo está publicando, se usa el anterior).
    {
        const juce::SpinLock::ScopedTryLockType sl (snapshotLock);
        if (sl.isLocked() && audioSnapshot != published) audioSnapshot = published;
    }
    {
        const juce::SpinLock::ScopedTryLockType sl (recordLock);
        if (sl.isLocked() && audioRecord != pendingRecord) audioRecord = pendingRecord;
        recordInUse = audioRecord.get();
    }
    if (audioSnapshot == nullptr) return;
    auto& s = *audioSnapshot;

    // Bloques más grandes que lo preparado: procesar en partes.
    if (n > maxBlock)
    {
        for (int done = 0; done < n;)
        {
            const int part = juce::jmin (maxBlock, n - done);
            const float* inPtrs[64] {}; float* outPtrs[64] {};
            for (int c = 0; c < juce::jmin (numIns, 64); ++c) inPtrs[c] = in[c] != nullptr ? in[c] + done : nullptr;
            for (int c = 0; c < juce::jmin (numOuts, 64); ++c) outPtrs[c] = out[c] != nullptr ? out[c] + done : nullptr;
            renderBlock (inPtrs, juce::jmin (numIns, 64), outPtrs, juce::jmin (numOuts, 64), part, stems);
            done += part;
        }
        return;
    }

    // Medidores de entrada
    for (int c = 0; c < juce::jmin (numIns, (int) inputPeaks.size()); ++c)
        if (in[c] != nullptr)
        {
            const auto r = juce::FloatVectorOperations::findMinAndMax (in[c], n);
            const auto pk = juce::jmax (std::abs (r.getStart()), std::abs (r.getEnd()));
            if (pk > inputPeaks[(size_t) c].load()) inputPeaks[(size_t) c] = pk;
        }

    // MIDI en vivo para este bloque
    liveBlock.clear();
    liveMidi.removeNextBlockOfMessages (liveBlock, n);

    // Cambió la pista que recibe el teclado: apagar las notas de la anterior (no quedan notas colgadas).
    if (s.liveTargets != lastLiveTargets)
    {
        for (auto i : lastLiveTargets)
            if (i >= 0 && i < (int) s.tracks.size() && std::find (s.liveTargets.begin(), s.liveTargets.end(), i) == s.liveTargets.end())
                s.tracks[(size_t) i].dsp->notesOffPending = true;
        lastLiveTargets = s.liveTargets;
    }
    if (panicRequested.exchange (false))
        for (auto& t : s.tracks) t.dsp->notesOffPending = true;

    // Transporte
    const bool isPlayingNow = playing.load();
    auto pos = position.load();
    if (const auto req = seekRequest.exchange (std::numeric_limits<juce::int64>::min()); req != std::numeric_limits<juce::int64>::min())
    {
        if (req != pos) for (auto& t : s.tracks) t.dsp->notesOffPending = true;
        pos = req;
    }
    if (wasPlaying && ! isPlayingNow)
        for (auto& t : s.tracks) t.dsp->notesOffPending = true;
    wasPlaying = isPlayingNow;

    RecordSession* rec = recording.load() ? audioRecord.get() : nullptr;

    if (! isPlayingNow)
    {
        renderSegment (s, in, numIns, out, numOuts, 0, n, pos, false, nullptr, stems);
        position = pos;
        return;
    }

    // Loop sin cortes: si el bloque cruza el final del loop, se divide y se salta al inicio en la muestra exacta.
    int offset = 0;
    while (offset < n)
    {
        int len = n - offset;
        const bool canLoop = s.loopOn && rec == nullptr && pos < s.loopEnd;
        if (canLoop && pos + len > s.loopEnd) len = (int) (s.loopEnd - pos);
        renderSegment (s, in, numIns, out, numOuts, offset, len, pos, true, rec, stems);
        pos += len;
        offset += len;
        if (canLoop && pos >= s.loopEnd)
        {
            pos = s.loopStart;
            for (auto& t : s.tracks) t.dsp->notesOffPending = true;
        }
    }
    position = pos;
}

void AudioEngine::renderClip (const PlaySnapshot::Clip& c, TrackDSP& dsp, juce::int64 segStart, int len) noexcept
{
    const auto from = juce::jmax (segStart, c.start);
    const auto to = juce::jmin (segStart + len, c.start + c.length);
    if (from >= to || c.file == nullptr) return;
    const int count = (int) (to - from);
    const int at = (int) (from - segStart);
    float* L = dsp.buffer.getWritePointer (0) + at;
    float* R = dsp.buffer.getWritePointer (1) + at;
    float* tmp[2] = { dsp.scratch.getWritePointer (0), dsp.scratch.getWritePointer (1) };
    const int fileCh = juce::jlimit (1, 2, c.file->numChannels);

    if (std::abs (c.ratio - 1.0) < 1.0e-9)
    {
        const auto fpos = (juce::int64) std::llround (c.fileStart) + (from - c.start);
        c.file->read (tmp, fileCh, fpos, count);
    }
    else
    {
        // Archivo a otra frecuencia: remuestreo continuo por ítem.
        TrackDSP::Voice* voice = nullptr;
        for (auto& v : dsp.voices) if (v.clip == c.id && v.nextOut == from) { voice = &v; break; }
        if (voice == nullptr)
        {
            voice = &dsp.voices[0];
            for (auto& v : dsp.voices) if (v.lastUsed < voice->lastUsed) voice = &v;
            voice->clip = c.id;
            voice->interp[0].reset();
            voice->interp[1].reset();
            voice->inputPos = (juce::int64) (c.fileStart + (double) (from - c.start) * c.ratio);
        }
        voice->lastUsed = ++dsp.voiceClock;
        const int needed = juce::jmin (dsp.scratch.getNumSamples() - count - 8, (int) std::ceil (count * c.ratio) + 8);
        float* src[2] = { tmp[0] + count, tmp[1] + count };
        c.file->read (src, fileCh, voice->inputPos, needed);
        int used = 0;
        for (int ch = 0; ch < fileCh; ++ch)
            used = voice->interp[ch].process (c.ratio, src[ch], tmp[ch], count);
        voice->inputPos += used;
        voice->nextOut = to;
    }
    if (fileCh == 1) juce::FloatVectorOperations::copy (tmp[1], tmp[0], count);

    // Ganancia del ítem y fundidos cortos (evitan clicks al cortar).
    for (int i = 0; i < count; ++i)
    {
        const auto local = from + i - c.start;
        float g = c.gain;
        if (local < c.fadeIn) g *= (float) (local + 1) / (float) (c.fadeIn + 1);
        const auto toEnd = c.length - local;
        if (toEnd <= c.fadeOut) g *= (float) toEnd / (float) (c.fadeOut + 1);
        L[i] += tmp[0][i] * g;
        R[i] += tmp[1][i] * g;
    }
}

void AudioEngine::renderClick (PlaySnapshot& s, float* L, float* R, int len, juce::int64 timelinePos) noexcept
{
    const auto spb = s.samplesPerBeat;
    if (spb <= 0) return;
    auto nextBeat = (juce::int64) std::ceil ((double) timelinePos / spb);
    for (int i = 0; i < len; ++i)
    {
        const auto t = (double) (timelinePos + i);
        if (t >= (double) nextBeat * spb - 0.5)
        {
            const bool accent = ((nextBeat % s.beatsPerBar) + s.beatsPerBar) % s.beatsPerBar == 0;
            clickFreq = accent ? 1760.0 : 1175.0;
            clickAmp = accent ? 0.9 : 0.6;
            clickEnv = 1.0;
            clickPhase = 0;
            ++nextBeat;
        }
        if (clickEnv > 1.0e-4)
        {
            const auto v = (float) (std::sin (clickPhase) * clickEnv * clickAmp) * s.clickGain;
            clickPhase += juce::MathConstants<double>::twoPi * clickFreq / s.sampleRate;
            clickEnv *= clickDecay;
            L[i] += v;
            R[i] += v;
        }
    }
}

void AudioEngine::updatePlayHead (const PlaySnapshot& s, juce::int64 pos, bool isPlaying) noexcept
{
    juce::AudioPlayHead::PositionInfo info;
    info.setBpm (s.bpm);
    info.setTimeSignature (juce::AudioPlayHead::TimeSignature { s.beatsPerBar, 4 });
    info.setTimeInSamples (pos);
    info.setTimeInSeconds ((double) pos / s.sampleRate);
    const auto ppq = (double) pos / s.samplesPerBeat;
    info.setPpqPosition (ppq);
    info.setPpqPositionOfLastBarStart (std::floor (ppq / s.beatsPerBar) * s.beatsPerBar);
    info.setBarCount ((juce::int64) std::floor (ppq / s.beatsPerBar));
    info.setIsPlaying (isPlaying);
    info.setIsRecording (recording.load());
    info.setIsLooping (s.loopOn);
    if (s.loopOn) info.setLoopPoints (juce::AudioPlayHead::LoopPoints { (double) s.loopStart / s.samplesPerBeat, (double) s.loopEnd / s.samplesPerBeat });
    hostPlayHead.info = info;
}

void AudioEngine::routeConnection (PlaySnapshot& s, PlaySnapshot::Track& from, PlaySnapshot::Track::Connection& c,
                                   const float* const* src, float gainL, float gainR, float* const* out, int numOuts,
                                   int offset, int len) noexcept
{
    float* destL = nullptr;
    float* destR = nullptr;
    if (c.target >= 0 && c.target < (int) s.tracks.size())
    {
        auto& bus = s.tracks[(size_t) c.target].dsp->busInput;
        destL = bus.getWritePointer (0);
        destR = bus.getWritePointer (1);
    }
    else if (c.target == PlaySnapshot::Track::Connection::direct)
    {
        const int outL = (from.directOutPair - 1) * 2, outR = outL + 1;
        if (from.directOutPair >= 2 && outR < numOuts && out[outL] != nullptr && out[outR] != nullptr)
        {
            destL = out[outL] + offset;
            destR = out[outR] + offset;
        }
    }
    if (destL == nullptr) { destL = masterBus.getWritePointer (0); destR = masterBus.getWritePointer (1); }

    auto* line = c.line.get();
    float startL = gainL, startR = gainR;
    if (line != nullptr)
    {
        if (line->curL >= 0) { startL = line->curL; startR = line->curR; }
        line->curL = gainL;
        line->curR = gainR;
    }
    const bool silent = startL == 0 && startR == 0 && gainL == 0 && gainR == 0;
    if (c.delay <= 0 || line == nullptr)
    {
        if (silent) return;
        auto addRamp = [len] (float* d, const float* x, float g0, float g1)
        {
            if (g0 == g1) { if (g1 == 1.0f) juce::FloatVectorOperations::add (d, x, len); else juce::FloatVectorOperations::addWithMultiply (d, x, g1, len); return; }
            const float step = (g1 - g0) / (float) len;
            for (int i = 0; i < len; ++i) d[i] += x[i] * (g0 + step * (float) i);
        };
        addRamp (destL, src[0], startL, gainL);
        addRamp (destR, src[1], startR, gainR);
        return;
    }
    // Con retardo de alineación: la línea debe seguir avanzando aunque la ganancia sea 0 (se vacía sola).
    auto& sc = from.dsp->sendScratch;
    sc.copyFrom (0, 0, src[0], len);
    sc.copyFrom (1, 0, src[1], len);
    float* sp[2] = { sc.getWritePointer (0), sc.getWritePointer (1) };
    line->process (sp, 2, len, c.delay);
    if (silent) return;
    juce::AudioBuffer<float> view (sp, 2, len);
    view.applyGainRamp (0, 0, len, startL, gainL);
    view.applyGainRamp (1, 0, len, startR, gainR);
    juce::FloatVectorOperations::add (destL, sp[0], len);
    juce::FloatVectorOperations::add (destR, sp[1], len);
}

void AudioEngine::renderSegment (PlaySnapshot& s, const float* const* in, int numIns, float* const* out, int numOuts,
                                 int offset, int len, juce::int64 pos, bool isPlaying, RecordSession* rec,
                                 std::vector<juce::AudioBuffer<float>>* stems)
{
    masterBus.clear (0, len);
    const bool recordingNow = rec != nullptr;
    updatePlayHead (s, pos, isPlaying);
    for (auto& t : s.tracks) if (t.isBus) t.dsp->busInput.clear (0, len);

    // Vista previa de notas (piano roll) por pista
    struct PendingPreview { int track; juce::MidiMessage msg; };
    PendingPreview previews[32];
    int numPreviews = 0;
    {
        int s1, n1, s2, n2;
        previewFifo.prepareToRead (juce::jmin (32, previewFifo.getNumReady()), s1, n1, s2, n2);
        auto take = [&] (int idx) { auto& p = previewData[(size_t) idx]; previews[numPreviews++] = { p.trackIndex, juce::MidiMessage (p.data, 3) }; };
        for (int i = 0; i < n1; ++i) take (s1 + i);
        for (int i = 0; i < n2; ++i) take (s2 + i);
        previewFifo.finishedRead (n1 + n2);
    }

    for (const auto orderIndex : s.order)
    {
        const auto ti = (size_t) orderIndex;
        auto& t = s.tracks[ti];
        auto& dsp = *t.dsp;
        dsp.buffer.clear (0, len);
        float* ch[2] = { dsp.buffer.getWritePointer (0), dsp.buffer.getWritePointer (1) };

        // 0) Bus: lo que le mandaron las pistas (salidas y envíos) en este bloque
        if (t.isBus)
        {
            dsp.buffer.copyFrom (0, 0, dsp.busInput, 0, 0, len);
            dsp.buffer.copyFrom (1, 0, dsp.busInput, 1, 0, len);
        }

        // Automatización de parámetros de plugins (valor al inicio del bloque; también detenido, en el cursor)
        for (auto& pa : t.params)
        {
            auto& params = pa.plugin->processor->getParameters();
            if (pa.index < params.size())
            {
                const auto v = juce::jlimit (0.0f, 1.0f, pa.env.at (pos));
                auto* param = params.getUnchecked (pa.index);
                if (std::abs (param->getValue() - v) > 1.0e-5f) param->setValue (v);
            }
        }

        // 1) Ítems de audio (los superpuestos se suman, como en REAPER)
        if (isPlaying)
            for (auto& c : t.clips)
            {
                if (c.start >= pos + len) break;
                renderClip (c, dsp, pos, len);
            }

        // 2) Instrumento: ítems MIDI + teclado en vivo + vista previa
        if (t.instrument != nullptr)
        {
            dsp.midi.clear();
            if (dsp.notesOffPending)
            {
                for (int c = 1; c <= 16; ++c)
                {
                    dsp.midi.addEvent (juce::MidiMessage::controllerEvent (c, 64, 0), 0);
                    dsp.midi.addEvent (juce::MidiMessage::allNotesOff (c), 0);
                }
                dsp.notesOffPending = false;
            }
            if (isPlaying && ! t.midi.empty())
            {
                auto it = std::lower_bound (t.midi.begin(), t.midi.end(), pos, [] (auto& e, juce::int64 p) { return e.pos < p; });
                for (; it != t.midi.end() && it->pos < pos + len; ++it)
                    dsp.midi.addEvent (it->message, (int) (it->pos - pos));
            }
            if (std::find (s.liveTargets.begin(), s.liveTargets.end(), (int) ti) != s.liveTargets.end())
                for (const auto meta : liveBlock)
                {
                    const auto sp = meta.samplePosition;
                    if (sp >= offset && sp < offset + len) dsp.midi.addEvent (meta.getMessage(), sp - offset);
                    else if (offset == 0 && sp >= len) dsp.midi.addEvent (meta.getMessage(), len - 1);
                }
            for (int i = 0; i < numPreviews; ++i)
                if (previews[i].track == (int) ti) dsp.midi.addEvent (previews[i].msg, 0);

            juce::AudioBuffer<float> ib (dsp.instrumentBuffer.getArrayOfWritePointers(), 2, len);
            ib.clear();
            t.instrument->processor->processBlock (ib, dsp.midi);
            dsp.buffer.addFrom (0, 0, ib, 0, 0, len);
            dsp.buffer.addFrom (1, 0, ib, 1, 0, len);
        }

        // 3) Monitoreo de entrada: la señal en vivo pasa por los plugins de la pista
        if (t.armed && ! t.input.isOff() && ! t.isInstrument && in != nullptr)
        {
            const bool monitor = t.monitor == MonitorMode::on
                              || (t.monitor == MonitorMode::autoTape && (! isPlaying || recordingNow));
            if (monitor)
            {
                const auto [l, r] = t.input.channels();
                if (l < numIns && in[l] != nullptr) juce::FloatVectorOperations::add (ch[0], in[l] + offset, len);
                if (r < numIns && in[r] != nullptr) juce::FloatVectorOperations::add (ch[1], in[r] + offset, len);
            }
        }

        // 4) Plugins de la pista
        juce::AudioBuffer<float> view (dsp.buffer.getArrayOfWritePointers(), 2, len);
        for (size_t p = 0; p < t.inserts.size(); ++p)
        {
            if (t.bypassed[p]) continue;
            dsp.emptyMidi.clear();
            t.inserts[p]->processor->processBlock (view, dsp.emptyMidi);
        }

        // 5) EQ de canal
        if (t.eqOn)
        {
            for (int b = 0; b < 3; ++b)
                if (dsp.eqCurrent[(size_t) b] != t.eq[(size_t) b].get())
                {
                    dsp.eq[(size_t) b * 2].coefficients = t.eq[(size_t) b];
                    dsp.eq[(size_t) b * 2 + 1].coefficients = t.eq[(size_t) b];
                    dsp.eqCurrent[(size_t) b] = t.eq[(size_t) b].get();
                }
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < len; ++i)
                {
                    auto v = ch[c][i];
                    for (int b = 0; b < 3; ++b) v = dsp.eq[(size_t) b * 2 + (size_t) c].processSample (v);
                    ch[c][i] = v;
                }
        }

        // 6) Envíos pre-fader (mezclas de monitoreo): antes del volumen y el paneo de la pista
        auto sendGains = [&] (const PlaySnapshot::Track::Connection& c, float& gl, float& gr)
        {
            const float db = c.levelEnv.empty() ? c.levelDb : c.levelEnv.at (pos + len);
            const float g = (c.muted || ! t.audible) ? 0.0f : Decibels::toGain (db);
            gl = g * juce::jmin (1.0f, 1.0f - c.pan);
            gr = g * juce::jmin (1.0f, 1.0f + c.pan);
        };
        for (auto& c : t.sends)
            if (c.preFader)
            {
                float gl, gr;
                sendGains (c, gl, gr);
                routeConnection (s, t, c, ch, gl, gr, out, numOuts, offset, len);
            }

        // 7) Volumen y paneo (automatizables), en rampa lineal durante el bloque para que no haya saltos
        {
            const float db = t.volumeEnv.empty() ? t.volumeDb : t.volumeEnv.at (pos + len);
            const float pan = juce::jlimit (-1.0f, 1.0f, t.panEnv.empty() ? t.pan : t.panEnv.at (pos + len));
            const float gain = t.audible ? Decibels::toGain (db) : 0.0f;
            // Ley de paneo de -3 dB
            const auto angle = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
            const float gl = gain * std::cos (angle) * juce::MathConstants<float>::sqrt2;
            const float gr = gain * std::sin (angle) * juce::MathConstants<float>::sqrt2;
            if (dsp.curL < 0) { dsp.curL = gl; dsp.curR = gr; }
            view.applyGainRamp (0, 0, len, dsp.curL, gl);
            view.applyGainRamp (1, 0, len, dsp.curR, gr);
            dsp.curL = gl;
            dsp.curR = gr;
        }

        const auto pk = juce::jmax (view.getMagnitude (0, 0, len), view.getMagnitude (1, 0, len));
        if (pk > dsp.peak.load()) dsp.peak = pk;

        // 8) Envíos post-fader
        for (auto& c : t.sends)
            if (! c.preFader)
            {
                float gl, gr;
                sendGains (c, gl, gr);
                routeConnection (s, t, c, ch, gl, gr, out, numOuts, offset, len);
            }

        // Stem (exportar por pistas): alineado con el Master
        if (stems != nullptr && ti < stems->size() && t.stemLine != nullptr)
        {
            auto& sc = dsp.sendScratch;
            sc.copyFrom (0, 0, view, 0, 0, len);
            sc.copyFrom (1, 0, view, 1, 0, len);
            float* sp[2] = { sc.getWritePointer (0), sc.getWritePointer (1) };
            t.stemLine->process (sp, 2, len, t.stemDelay);
            (*stems)[ti].addFrom (0, offset, sc, 0, 0, len);
            (*stems)[ti].addFrom (1, offset, sc, 1, 0, len);
        }

        // Salida: al Master, a un bus o directo a una salida de la interfaz (in-ears)
        routeConnection (s, t, t.output, ch, 1.0f, 1.0f, out, numOuts, offset, len);

        // 9) Grabación (señal de entrada sin efectos), desde el punto de inicio exacto
        if (rec != nullptr && isPlaying)
        {
            const auto startIdx = (int) juce::jlimit ((juce::int64) 0, (juce::int64) len, rec->recordStart - pos);
            const int count = len - startIdx;
            if (count > 0)
                for (auto& take : rec->takes)
                    if (take.trackId == t.id && take.writer != nullptr)
                    {
                        const float* data[2] = {
                            take.chL < numIns && in != nullptr && in[take.chL] != nullptr ? in[take.chL] + offset + startIdx : zeros.getReadPointer (0),
                            take.chR < numIns && in != nullptr && in[take.chR] != nullptr ? in[take.chR] + offset + startIdx : zeros.getReadPointer (0) };
                        if (! take.writer->write (data, count)) rec->overflow = true;
                    }
        }
    }
    if (rec != nullptr && isPlaying)
    {
        const auto startIdx = juce::jlimit ((juce::int64) 0, (juce::int64) len, rec->recordStart - pos);
        rec->samplesWritten += len - startIdx;

        // MIDI grabado: todo lo que llega del teclado, con su posición en la línea de tiempo
        if (rec->recordMidi && offset == 0)
            for (const auto meta : liveBlock)
            {
                const auto m = meta.getMessage();
                if (! (m.isNoteOnOrOff() || m.isController() || m.isPitchWheel() || m.isChannelPressure())) continue;
                int s1, n1, s2, n2;
                midiFifo.prepareToWrite (1, s1, n1, s2, n2);
                if (n1 + n2 == 0) { rec->overflow = true; break; }
                auto& slot = midiFifoData[(size_t) (n1 > 0 ? s1 : s2)];
                slot.pos = pos + meta.samplePosition;
                slot.size = juce::jmin (3, m.getRawDataSize());
                std::memcpy (slot.data, m.getRawData(), (size_t) slot.size);
                midiFifo.finishedWrite (1);
            }
    }

    // Master: plugins (p. ej. WorshipMastering), volumen, medidor
    {
        juce::AudioBuffer<float> mview (masterBus.getArrayOfWritePointers(), 2, len);
        for (size_t p = 0; p < s.master.size(); ++p)
        {
            if (s.masterBypassed[p]) continue;
            emptyMidi.clear();
            s.master[p]->processor->processBlock (mview, emptyMidi);
        }
        mview.applyGain (s.masterGain);
        const auto pk = juce::jmax (mview.getMagnitude (0, 0, len), mview.getMagnitude (1, 0, len));
        if (pk > masterPeak.load()) masterPeak = pk;
        for (int c = 0; c < juce::jmin (2, numOuts); ++c)
            if (out[c] != nullptr) juce::FloatVectorOperations::add (out[c] + offset, mview.getReadPointer (c), len);
    }

    // Click: no pasa por los plugins del Master; puede ir a una salida aparte (in-ears).
    // Se retrasa igual que las pistas (PDC) para que coincida con lo que se escucha.
    if (isPlaying && s.clickOn)
    {
        const int pair = s.clickOutPair;
        int outL = 0, outR = 1;
        if (pair >= 2 && (pair - 1) * 2 + 1 < numOuts) { outL = (pair - 1) * 2; outR = outL + 1; }
        if (outR < numOuts && out[outL] != nullptr && out[outR] != nullptr)
            renderClick (s, out[outL] + offset, out[outR] + offset, len, pos - s.maxLatency);
    }
}

} // namespace wdaw
