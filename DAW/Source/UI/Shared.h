#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "Session.h"
#include "Theme.h"

namespace wdaw::ui
{

/** Medidores: se leen del motor una sola vez por cuadro y se reparten a todas las vistas. */
class MeterModel : private juce::Timer
{
public:
    explicit MeterModel (AudioEngine& e) : engine (e) { startTimerHz (30); }
    float track (const juce::Uuid& id) const { auto it = levels.find (id); return it != levels.end() ? it->second : 0.0f; }
    float master() const { return masterLevel; }
    float input (int ch) const { return ch >= 0 && ch < (int) inputs.size() ? inputs[(size_t) ch] : 0.0f; }
    std::function<void()> onUpdate;
    void setTracks (std::vector<juce::Uuid> ids) { trackIds = std::move (ids); }

private:
    void timerCallback() override
    {
        constexpr float release = 0.85f;
        for (auto& id : trackIds) levels[id] = juce::jmax (engine.takeTrackPeak (id), levels[id] * release);
        masterLevel = juce::jmax (engine.takeMasterPeak(), masterLevel * release);
        inputs.resize ((size_t) juce::jmax (0, engine.numInputChannels()));
        for (size_t c = 0; c < inputs.size(); ++c) inputs[c] = juce::jmax (engine.takeInputPeak ((int) c), inputs[c] * release);
        if (onUpdate) onUpdate();
    }
    AudioEngine& engine;
    std::vector<juce::Uuid> trackIds;
    std::map<juce::Uuid, float> levels;
    float masterLevel = 0;
    std::vector<float> inputs;
};

/** Formas de onda (miniaturas) por archivo del proyecto, calculadas en segundo plano y cacheadas. */
class ThumbnailStore : public juce::ChangeBroadcaster, private juce::ChangeListener
{
public:
    ThumbnailStore() { formats.registerBasicFormats(); }
    juce::AudioThumbnail* get (const juce::File& file)
    {
        const auto key = file.getFullPathName();
        if (auto it = thumbs.find (key); it != thumbs.end()) return it->second.get();
        auto t = std::make_unique<juce::AudioThumbnail> (256, formats, cache);
        t->setSource (new juce::FileInputSource (file));
        t->addChangeListener (this);
        auto* raw = t.get();
        thumbs[key] = std::move (t);
        return raw;
    }
private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { sendChangeMessage(); }
    juce::AudioFormatManager formats;
    juce::AudioThumbnailCache cache { 400 };
    std::map<juce::String, std::unique_ptr<juce::AudioThumbnail>> thumbs;
};

/** Lo que comparten las vistas del editor. */
struct Context
{
    Session& session;
    MeterModel& meters;
    ThumbnailStore& thumbnails;
    std::function<void (const juce::Uuid& trackId, const juce::Uuid& slotId)> openPluginWindow;
    std::function<void (const juce::Uuid& trackId, bool instrument)> choosePlugin;   // navegador de plugins
    std::function<void()> addMasterPlugin;
    std::function<void()> openPianoRoll;
    std::function<void (const juce::String& title, const juce::String& current, std::function<void (juce::String)>)> askText;
};

/** Escala de fader: más resolución cerca de 0 dB (0 dB ≈ 78 % del recorrido). */
inline double faderPosition (double db) { return db <= Decibels::silence ? 0.0 : std::pow ((db - Decibels::silence) / 66.0, 2.2); }
inline double faderDb (double pos)       { return pos <= 0.0005 ? Decibels::silence : Decibels::silence + std::pow (juce::jlimit (0.0, 1.0, pos), 1.0 / 2.2) * 66.0; }
inline juce::String formatDb (double db) { return db <= Decibels::silence ? juce::String::fromUTF8 ("-∞") : (db > 0 ? "+" : "") + juce::String (db, 1); }
inline juce::String formatPan (double p) { return std::abs (p) < 0.01 ? juce::String ("C") : (p < 0 ? "L" : "R") + juce::String ((int) std::round (std::abs (p) * 100)); }

/** Barra de medidor con degradado verde → amarillo → rojo. */
void drawMeter (juce::Graphics&, juce::Rectangle<float>, float level, bool vertical);

/** Menús de pista compartidos (cabecera y mezclador): entrada, monitoreo, salida directa, Aprender MIDI. */
void addTrackInputMenus (juce::PopupMenu&, Session&, const Track&);
void addMidiLearnMenu (juce::PopupMenu&, Session&, const Track&);

} // namespace wdaw::ui
