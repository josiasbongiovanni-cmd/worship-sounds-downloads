#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

/*  Procesadores incluidos en la app (no necesitan plugins instalados):
      - "Piano básico" y "Pad básico": instrumentos para que una pista MIDI suene desde el primer momento,
        en Mac y en Windows, con pedal de sustain.
      - "Ganancia con latencia": solo para pruebas de compensación de latencia (PDC).
*/
namespace wdaw
{

class BasicSynthProcessor : public juce::AudioProcessor
{
public:
    enum class Kind { piano, pad };
    explicit BasicSynthProcessor (Kind);

    const juce::String getName() const override;
    void prepareToPlay (double sampleRate, int blockSize) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override { return new juce::GenericAudioProcessorEditor (*this); }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    Kind kind;
private:
    juce::Synthesiser synth;
    juce::AudioParameterFloat* volume = nullptr;
    juce::AudioParameterFloat* brightness = nullptr;
    juce::AudioParameterFloat* release = nullptr;
};

/** Base de los efectos incluidos: estéreo, parámetros guardados como valores normalizados. */
class BuiltInEffect : public juce::AudioProcessor
{
public:
    BuiltInEffect();
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override { return new juce::GenericAudioProcessorEditor (*this); }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    void releaseResources() override {}
    using juce::AudioProcessor::processBlock;
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;
};

/** "Reverb básica": sala/plato para un bus de efectos (100 % húmedo por defecto). */
class ReverbProcessor : public BuiltInEffect
{
public:
    ReverbProcessor();
    const juce::String getName() const override;
    void prepareToPlay (double sampleRate, int blockSize) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    double getTailLengthSeconds() const override { return 6.0; }
private:
    juce::Reverb reverb;
    juce::AudioParameterFloat *size, *damping, *width, *wet, *dry;
};

/** "Delay básico": eco estéreo con realimentación y filtro de agudos (tiempo en ms o sincronizado al tempo). */
class DelayProcessor : public BuiltInEffect
{
public:
    DelayProcessor();
    const juce::String getName() const override;
    void prepareToPlay (double sampleRate, int blockSize) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    double getTailLengthSeconds() const override { return 4.0; }
    /** Duraciones sincronizadas: 0 = libre (ms), luego 1/4, 1/8 con puntillo, 1/8, 1/16. */
    static juce::StringArray syncNames();
private:
    juce::AudioBuffer<float> line;
    int writePos = 0;
    double sr = 48000;
    float lowState[2] {};
    juce::LinearSmoothedValue<float> delaySamples;
    juce::AudioParameterFloat *timeMs, *feedback, *mix, *tone;
    juce::AudioParameterChoice* sync;
};

/** Ganancia unitaria que retrasa la señal N muestras y lo reporta (para probar PDC). */
class LatencyTestProcessor : public juce::AudioProcessor
{
public:
    explicit LatencyTestProcessor (int latencySamples);
    const juce::String getName() const override { return "Latency Test"; }
    void prepareToPlay (double, int) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    bool hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}
private:
    int latency;
    juce::AudioBuffer<float> delay;
    int writePos = 0;
};

} // namespace wdaw
