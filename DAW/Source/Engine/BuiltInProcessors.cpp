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
