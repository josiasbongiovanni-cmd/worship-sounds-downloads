#include "BuiltInProcessors.h"

namespace wdaw
{

namespace
{
    struct AnySound : juce::SynthesiserSound
    {
        bool appliesToNote (int) override    { return true; }
        bool appliesToChannel (int) override { return true; }
    };

    /** Voz sencilla: piano = armónicos con caída; pad = sierras desafinadas con filtro y ataque lento. */
    struct BasicVoice : juce::SynthesiserVoice
    {
        BasicVoice (BasicSynthProcessor::Kind k, std::function<float()> brightnessFn, std::function<float()> releaseFn)
            : kind (k), brightness (std::move (brightnessFn)), releaseTime (std::move (releaseFn)) {}

        bool canPlaySound (juce::SynthesiserSound*) override { return true; }

        void startNote (int note, float velocity, juce::SynthesiserSound*, int) override
        {
            freq = juce::MidiMessage::getMidiNoteInHertz (note);
            vel = velocity;
            for (auto& p : phase) p = juce::Random::getSystemRandom().nextDouble();
            juce::ADSR::Parameters params;
            if (kind == BasicSynthProcessor::Kind::piano)
                params = { 0.002f, 1.6f + (float) (60 - note) * 0.02f, 0.0f, juce::jmax (0.05f, releaseTime() * 0.4f) };
            else
                params = { 0.6f, 0.4f, 0.85f, juce::jmax (0.2f, releaseTime() * 2.0f) };
            params.decay = juce::jmax (0.3f, params.decay);
            env.setSampleRate (getSampleRate());
            env.setParameters (params);
            env.noteOn();
            lp = 0;
        }

        void stopNote (float, bool allowTailOff) override
        {
            if (allowTailOff) env.noteOff();
            else { env.reset(); clearCurrentNote(); }
        }

        void pitchWheelMoved (int) override {}
        void controllerMoved (int, int) override {}

        void renderNextBlock (juce::AudioBuffer<float>& out, int start, int num) override
        {
            if (! isVoiceActive()) return;
            const auto sr = getSampleRate();
            const auto bright = juce::jlimit (0.05f, 1.0f, brightness());
            const auto cutoff = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * (300.0f + 7000.0f * bright * vel) / (float) sr);
            for (int i = 0; i < num; ++i)
            {
                float s = 0;
                if (kind == BasicSynthProcessor::Kind::piano)
                {
                    static constexpr float amps[] = { 1.0f, 0.45f, 0.22f, 0.12f, 0.06f };
                    for (int h = 0; h < 5; ++h)
                    {
                        s += amps[h] * (float) std::sin (juce::MathConstants<double>::twoPi * phase[(size_t) h]) * (h == 0 ? 1.0f : bright);
                        phase[(size_t) h] += freq * (h + 1) * (1.0 + 0.0004 * h * h) / sr;
                        if (phase[(size_t) h] >= 1.0) phase[(size_t) h] -= 1.0;
                    }
                    s *= 0.28f;
                }
                else
                {
                    static constexpr double detune[] = { 1.0, 1.0035, 0.9965 };
                    for (int v = 0; v < 3; ++v)
                    {
                        s += (float) (2.0 * phase[(size_t) v] - 1.0);
                        phase[(size_t) v] += freq * detune[v] / sr;
                        if (phase[(size_t) v] >= 1.0) phase[(size_t) v] -= 1.0;
                    }
                    s *= 0.12f;
                }
                lp += cutoff * (s - lp);
                const auto e = env.getNextSample() * (0.35f + 0.65f * vel);
                const auto sample = (kind == BasicSynthProcessor::Kind::pad ? lp : s) * e;
                for (int c = 0; c < out.getNumChannels(); ++c)
                    out.addSample (c, start + i, sample);
                if (! env.isActive()) { clearCurrentNote(); break; }
            }
        }

        BasicSynthProcessor::Kind kind;
        std::function<float()> brightness, releaseTime;
        double freq = 440;
        float vel = 1;
        std::array<double, 5> phase {};
        juce::ADSR env;
        float lp = 0;
    };
}

BasicSynthProcessor::BasicSynthProcessor (Kind k)
    : juce::AudioProcessor (BusesProperties().withOutput ("Salida", juce::AudioChannelSet::stereo(), true)),
      kind (k)
{
    addParameter (volume = new juce::AudioParameterFloat ({ "volume", 1 }, "Volumen", 0.0f, 1.0f, 0.8f));
    addParameter (brightness = new juce::AudioParameterFloat ({ "brightness", 1 }, "Brillo", 0.0f, 1.0f, kind == Kind::piano ? 0.6f : 0.4f));
    addParameter (release = new juce::AudioParameterFloat ({ "release", 1 }, juce::String::fromUTF8 ("Liberación"), 0.05f, 3.0f, kind == Kind::piano ? 0.5f : 1.2f));
    for (int i = 0; i < 32; ++i)
        synth.addVoice (new BasicVoice (kind, [this] { return brightness->get(); }, [this] { return release->get(); }));
    synth.addSound (new AnySound());
}

const juce::String BasicSynthProcessor::getName() const
{
    return kind == Kind::piano ? juce::String::fromUTF8 ("Piano básico") : juce::String::fromUTF8 ("Pad básico");
}

void BasicSynthProcessor::prepareToPlay (double sampleRate, int)
{
    synth.setCurrentPlaybackSampleRate (sampleRate);
}

void BasicSynthProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    buffer.clear();
    synth.renderNextBlock (buffer, midi, 0, buffer.getNumSamples());
    buffer.applyGain (volume->get());
}

void BasicSynthProcessor::getStateInformation (juce::MemoryBlock& block)
{
    juce::MemoryOutputStream out (block, false);
    out.writeFloat (volume->get());
    out.writeFloat (brightness->get());
    out.writeFloat (release->get());
}

void BasicSynthProcessor::setStateInformation (const void* data, int size)
{
    juce::MemoryInputStream in (data, (size_t) size, false);
    if (size >= 12)
    {
        *volume = in.readFloat();
        *brightness = in.readFloat();
        *release = in.readFloat();
    }
}

//==============================================================================
BuiltInEffect::BuiltInEffect()
    : juce::AudioProcessor (BusesProperties().withInput ("In", juce::AudioChannelSet::stereo(), true)
                                             .withOutput ("Out", juce::AudioChannelSet::stereo(), true)) {}

void BuiltInEffect::getStateInformation (juce::MemoryBlock& block)
{
    juce::MemoryOutputStream out (block, false);
    for (auto* p : getParameters()) out.writeFloat (p->getValue());
}

void BuiltInEffect::setStateInformation (const void* data, int size)
{
    juce::MemoryInputStream in (data, (size_t) size, false);
    for (auto* p : getParameters())
        if (in.getNumBytesRemaining() >= 4) p->setValueNotifyingHost (in.readFloat());
}

ReverbProcessor::ReverbProcessor()
{
    addParameter (size = new juce::AudioParameterFloat ({ "size", 1 }, juce::String::fromUTF8 ("Tamaño"), 0.0f, 1.0f, 0.75f));
    addParameter (damping = new juce::AudioParameterFloat ({ "damping", 1 }, juce::String::fromUTF8 ("Amortiguación"), 0.0f, 1.0f, 0.45f));
    addParameter (width = new juce::AudioParameterFloat ({ "width", 1 }, "Ancho", 0.0f, 1.0f, 1.0f));
    addParameter (wet = new juce::AudioParameterFloat ({ "wet", 1 }, juce::String::fromUTF8 ("Húmedo"), 0.0f, 1.0f, 1.0f));
    addParameter (dry = new juce::AudioParameterFloat ({ "dry", 1 }, "Seco", 0.0f, 1.0f, 0.0f));
}

const juce::String ReverbProcessor::getName() const { return juce::String::fromUTF8 ("Reverb básica"); }

void ReverbProcessor::prepareToPlay (double sampleRate, int)
{
    reverb.setSampleRate (sampleRate);
    reverb.reset();
}

void ReverbProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::Reverb::Parameters p;
    p.roomSize = size->get();
    p.damping = damping->get();
    p.width = width->get();
    p.wetLevel = wet->get();
    p.dryLevel = dry->get();
    reverb.setParameters (p);
    if (buffer.getNumChannels() >= 2) reverb.processStereo (buffer.getWritePointer (0), buffer.getWritePointer (1), buffer.getNumSamples());
    else if (buffer.getNumChannels() == 1) reverb.processMono (buffer.getWritePointer (0), buffer.getNumSamples());
}

juce::StringArray DelayProcessor::syncNames()
{
    return { "Libre (ms)", "1/4", "1/8 con puntillo", "1/8", "1/16" };
}

DelayProcessor::DelayProcessor()
{
    addParameter (sync = new juce::AudioParameterChoice ({ "sync", 1 }, "Sincronizar al tempo", syncNames(), 2));
    addParameter (timeMs = new juce::AudioParameterFloat ({ "time", 1 }, "Tiempo (ms)", 20.0f, 1500.0f, 375.0f));
    addParameter (feedback = new juce::AudioParameterFloat ({ "feedback", 1 }, juce::String::fromUTF8 ("Realimentación"), 0.0f, 0.95f, 0.35f));
    addParameter (tone = new juce::AudioParameterFloat ({ "tone", 1 }, "Tono", 0.0f, 1.0f, 0.6f));
    addParameter (mix = new juce::AudioParameterFloat ({ "mix", 1 }, juce::String::fromUTF8 ("Húmedo"), 0.0f, 1.0f, 1.0f));
}

const juce::String DelayProcessor::getName() const { return juce::String::fromUTF8 ("Delay básico"); }

void DelayProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    line.setSize (2, (int) (sampleRate * 3.2) + 8);
    line.clear();
    writePos = 0;
    lowState[0] = lowState[1] = 0;
    delaySamples.reset (sampleRate, 0.08);
    delaySamples.setCurrentAndTargetValue ((float) (timeMs->get() * 0.001 * sampleRate));
}

void DelayProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    double seconds = timeMs->get() * 0.001;
    if (sync->getIndex() > 0)
    {
        double bpm = 120;
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
                if (auto b = pos->getBpm()) bpm = *b;
        static const double beats[] = { 0, 1.0, 0.75, 0.5, 0.25 };
        seconds = beats[sync->getIndex()] * 60.0 / juce::jlimit (20.0, 400.0, bpm);
    }
    delaySamples.setTargetValue ((float) juce::jlimit (1.0, (double) line.getNumSamples() - 4, seconds * sr));
    const auto size = line.getNumSamples();
    const float fb = feedback->get(), wetGain = mix->get();
    const float coeff = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * (1500.0f + tone->get() * 14000.0f) / (float) sr);
    const int chans = juce::jmin (2, buffer.getNumChannels());
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const auto d = delaySamples.getNextValue();
        const auto readPos = (float) writePos - d + (float) size;
        const int i0 = (int) readPos % size;
        const int i1 = (i0 + 1) % size;
        const float frac = readPos - std::floor (readPos);
        for (int c = 0; c < chans; ++c)
        {
            auto* l = line.getWritePointer (c);
            const auto delayed = l[i0] + (l[i1] - l[i0]) * frac;
            lowState[c] += coeff * (delayed - lowState[c]);
            const auto in = buffer.getSample (c, i);
            l[writePos] = in + lowState[c] * fb;
            buffer.setSample (c, i, in * (1.0f - wetGain) + lowState[c] * wetGain);
        }
        writePos = (writePos + 1) % size;
    }
}

//==============================================================================
LatencyTestProcessor::LatencyTestProcessor (int latencySamples)
    : juce::AudioProcessor (BusesProperties().withInput ("In", juce::AudioChannelSet::stereo(), true)
                                             .withOutput ("Out", juce::AudioChannelSet::stereo(), true)),
      latency (latencySamples)
{
    setLatencySamples (latency);
}

void LatencyTestProcessor::prepareToPlay (double, int)
{
    delay.setSize (2, latency + 1);
    delay.clear();
    writePos = 0;
}

void LatencyTestProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    if (latency <= 0) return;
    const auto size = delay.getNumSamples();
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const auto readPos = (writePos + 1) % size;   // la muestra escrita hace `latency` vueltas
        for (int c = 0; c < juce::jmin (2, buffer.getNumChannels()); ++c)
        {
            const auto in = buffer.getSample (c, i);
            buffer.setSample (c, i, delay.getSample (c, readPos));
            delay.setSample (c, writePos, in);
        }
        writePos = (writePos + 1) % size;
    }
}

} // namespace wdaw
