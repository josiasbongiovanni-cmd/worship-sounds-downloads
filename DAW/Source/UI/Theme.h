#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "Model/Model.h"

namespace wdaw::ui
{

/** Paleta oscura y cálida, pensada para salas con poca luz. */
namespace colours
{
    inline const juce::Colour background { 0xff0b0b11 };
    inline const juce::Colour panel      { 0xff13131c };
    inline const juce::Colour elevated   { 0xff1c1c29 };
    inline const juce::Colour lane       { 0xff0f0f17 };
    inline const juce::Colour laneAlt    { 0xff11111a };
    inline const juce::Colour stroke     { 0x12ffffff };
    inline const juce::Colour strokeHi   { 0x24ffffff };
    inline const juce::Colour text       { 0xf0ffffff };
    inline const juce::Colour text2      { 0x94ffffff };
    inline const juce::Colour text3      { 0x57ffffff };
    inline const juce::Colour accent     { 0xff9978ff };
    inline const juce::Colour accent2    { 0xff54c7ff };
    inline const juce::Colour record     { 0xffff455c };
    inline const juce::Colour solo       { 0xffffc740 };
    inline const juce::Colour mute       { 0xfffa734a };
    inline const juce::Colour loop       { 0xff54c7ff };
    inline const juce::Colour marker     { 0xffff455c };

    /** Colores de pista y sección por nombre ("violet", "sky"…). */
    juce::Colour named (const juce::String& id);
    juce::StringArray names();
    juce::String displayName (const juce::String& id);
}

juce::ColourGradient accentGradient (juce::Rectangle<float> r);

juce::Font font (float size, bool bold = false);
juce::Font mono (float size);

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float min, float max,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    juce::Font getPopupMenuFont() override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void drawTooltip (juce::Graphics&, const juce::String&, int w, int h) override;
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                        int thumbStart, int thumbSize, bool over, bool down) override;
    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return font (13.5f); }
    juce::Font getLabelFont (juce::Label&) override { return font (13.5f); }
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;

private:
    juce::Typeface::Ptr regular, bold;
};

/** Botón de pastilla M / S / R. */
class PillButton : public juce::Button
{
public:
    PillButton (const juce::String& label, juce::Colour onColour);
    void paintButton (juce::Graphics&, bool over, bool down) override;
    juce::Colour onColour;
};

/** Botón redondo de icono (transporte). Dibuja el icono con un Path. */
class IconButton : public juce::Button
{
public:
    enum class Icon { play, stop, record, toStart, prev, next, loop, metronome, undo, redo, importAudio, exportAudio,
                      settings, mixer, inspector, home, plus, zoomIn, zoomOut, snap, follow, panic, piano, guide, speaker };
    IconButton (Icon, const juce::String& tooltip);
    void paintButton (juce::Graphics&, bool over, bool down) override;
    Icon icon;
    juce::Colour activeColour = colours::accent;
    bool active = false;
    bool primary = false;   // botón grande de reproducir con degradado
    static juce::Path path (Icon, juce::Rectangle<float>);
};

} // namespace wdaw::ui
