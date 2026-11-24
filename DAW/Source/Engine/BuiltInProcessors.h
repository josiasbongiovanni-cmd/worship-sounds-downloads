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
