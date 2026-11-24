#include "Theme.h"
#include "BinaryData.h"

namespace wdaw::ui
{

namespace colours
{
    static const std::vector<std::pair<const char*, juce::uint32>> table {
        { "violet", 0xff9e7aff }, { "indigo", 0xff6b78ff }, { "sky", 0xff54b8ff }, { "teal", 0xff33d4cc },
        { "mint", 0xff61e69e }, { "lime", 0xffb3e64d }, { "amber", 0xffffbd45 }, { "coral", 0xffff8066 },
        { "rose", 0xffff6b9e }, { "slate", 0xff99a3bd } };

    juce::Colour named (const juce::String& id)
    {
        for (auto& [n, c] : table) if (id == n) return juce::Colour (c);
        return juce::Colour (0xff9e7aff);
    }

    juce::StringArray names()
    {
        juce::StringArray a;
        for (auto& [n, c] : table) a.add (n);
        return a;
    }

    juce::String displayName (const juce::String& id)
    {
        static const std::map<juce::String, const char*> es {
            { "violet", "Violeta" }, { "indigo", "Índigo" }, { "sky", "Cielo" }, { "teal", "Turquesa" }, { "mint", "Menta" },   // utf8-ok
            { "lime", "Lima" }, { "amber", "Ámbar" }, { "coral", "Coral" }, { "rose", "Rosa" }, { "slate", "Pizarra" } };   // utf8-ok
        if (auto it = es.find (id); it != es.end()) return juce::String::fromUTF8 (it->second);
        return id;
    }
}

juce::ColourGradient accentGradient (juce::Rectangle<float> r)
{
    return juce::ColourGradient (colours::accent, r.getTopLeft(), colours::accent2, r.getBottomRight(), false);
}

juce::Font font (float size, bool bold)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}

juce::Font mono (float size)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), size, juce::Font::plain));
}

//==============================================================================
LookAndFeel::LookAndFeel()
{
    using namespace colours;
    setColour (juce::ResizableWindow::backgroundColourId, background);
    setColour (juce::DocumentWindow::textColourId, text);
    setColour (juce::TextButton::buttonColourId, elevated);
    setColour (juce::TextButton::buttonOnColourId, accent);
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    setColour (juce::Label::textColourId, text);
    setColour (juce::TextEditor::backgroundColourId, juce::Colour (0x0fffffff));
    setColour (juce::TextEditor::outlineColourId, stroke);
    setColour (juce::TextEditor::focusedOutlineColourId, accent);
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::TextEditor::highlightColourId, accent.withAlpha (0.4f));
    setColour (juce::CaretComponent::caretColourId, accent2);
    setColour (juce::ComboBox::backgroundColourId, juce::Colour (0x0fffffff));
    setColour (juce::ComboBox::outlineColourId, stroke);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::arrowColourId, text2);
    setColour (juce::PopupMenu::backgroundColourId, elevated);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.35f));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::PopupMenu::headerTextColourId, text3);
    setColour (juce::Slider::thumbColourId, juce::Colours::white);
    setColour (juce::Slider::trackColourId, accent);
    setColour (juce::Slider::backgroundColourId, juce::Colour (0x1fffffff));
    setColour (juce::Slider::rotarySliderFillColourId, accent);
    setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0x14ffffff));
    setColour (juce::Slider::textBoxTextColourId, text2);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::ListBox::backgroundColourId, panel);
    setColour (juce::ListBox::textColourId, text);
    setColour (juce::ScrollBar::thumbColourId, juce::Colour (0x33ffffff));
    setColour (juce::ToggleButton::textColourId, text);
    setColour (juce::ToggleButton::tickColourId, accent);
    setColour (juce::ToggleButton::tickDisabledColourId, text3);
    setColour (juce::GroupComponent::outlineColourId, stroke);
    setColour (juce::GroupComponent::textColourId, text2);
    setColour (juce::TabbedButtonBar::tabTextColourId, text2);
    setColour (juce::TabbedButtonBar::frontTextColourId, juce::Colours::white);
    setColour (juce::TabbedComponent::backgroundColourId, panel);
    setColour (juce::TabbedComponent::outlineColourId, stroke);
    setColour (juce::AlertWindow::backgroundColourId, elevated);
    setColour (juce::AlertWindow::textColourId, text);
    setColour (juce::AlertWindow::outlineColourId, stroke);
    setColour (juce::TooltipWindow::backgroundColourId, elevated);
    setColour (juce::TooltipWindow::textColourId, text);
    setColour (juce::TreeView::backgroundColourId, panel);
    setColour (juce::ProgressBar::backgroundColourId, juce::Colour (0x1fffffff));
    setColour (juce::ProgressBar::foregroundColourId, accent);
    setColour (juce::HyperlinkButton::textColourId, accent2);
    setColour (juce::TableHeaderComponent::backgroundColourId, elevated);
    setColour (juce::TableHeaderComponent::textColourId, text2);
    setColour (juce::DirectoryContentsDisplayComponent::highlightColourId, accent.withAlpha (0.35f));
    regular = juce::Typeface::createSystemTypefaceFor (BinaryData::InterRegular_otf, BinaryData::InterRegular_otfSize);
    bold = juce::Typeface::createSystemTypefaceFor (BinaryData::InterSemiBold_otf, BinaryData::InterSemiBold_otfSize);
}

juce::Typeface::Ptr LookAndFeel::getTypefaceForFont (const juce::Font& f)
{
    // Inter para todo el texto de la interfaz; la monoespaciada del sistema para tiempos y valores.
    if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName() && regular != nullptr)
        return f.isBold() ? bold : regular;
    return LookAndFeel_V4::getTypefaceForFont (f);
}

juce::Font LookAndFeel::getTextButtonFont (juce::TextButton&, int h)
{
    return font (juce::jmin (14.0f, (float) h * 0.5f), true);
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bg, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    auto c = on ? b.findColour (juce::TextButton::buttonOnColourId) : bg;
    if (over) c = c.brighter (0.12f);
    if (down) c = c.darker (0.1f);
    g.setColour (c);
    g.fillRoundedRectangle (r, 7.0f);
    g.setColour (colours::stroke);
    g.drawRoundedRectangle (r, 7.0f, 1.0f);
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3);
    const auto size = juce::jmin (r.getWidth(), r.getHeight());
    r = r.withSizeKeepingCentre (size, size);
    const auto radius = size / 2 - 2;
    const auto cx = r.getCentreX(), cy = r.getCentreY();
    const auto angle = start + pos * (end - start);
    juce::Path track, value;
    track.addCentredArc (cx, cy, radius, radius, 0, start, end, true);
    g.setColour (juce::Colour (0x14ffffff));
    g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const bool bipolar = s.getMinimum() < 0 && s.getMaximum() > 0;
    const auto from = bipolar ? (start + end) / 2 : start;
    value.addCentredArc (cx, cy, radius, radius, 0, juce::jmin (from, angle), juce::jmax (from, angle), true);
    g.setColour (s.findColour (juce::Slider::rotarySliderFillColourId));
    g.strokePath (value, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (colours::elevated);
    g.fillEllipse (r.reduced (6));
    juce::Path needle;
    needle.addRoundedRectangle (-1.0f, -radius + 5, 2.0f, radius * 0.45f, 1.0f);
    g.setColour (colours::text);
    g.fillPath (needle, juce::AffineTransform::rotation (angle).translated (cx, cy));
}

void LookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                    juce::Slider::SliderStyle style, juce::Slider& s)
{
    auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    if (style == juce::Slider::LinearHorizontal)
    {
        auto bar = r.withSizeKeepingCentre (r.getWidth(), 4);
        g.setColour (juce::Colour (0x1fffffff));
        g.fillRoundedRectangle (bar, 2);
        g.setColour (s.findColour (juce::Slider::trackColourId));
        g.fillRoundedRectangle (bar.withRight (pos), 2);
        g.setColour (juce::Colours::white);
        g.fillEllipse (juce::Rectangle<float> (12, 12).withCentre ({ pos, bar.getCentreY() }));
        return;
    }
    LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0, 0, style, s);
}

void LookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (colours::elevated);
    g.setColour (colours::strokeHi);
    g.drawRect (0, 0, w, h);
}

juce::Font LookAndFeel::getPopupMenuFont() { return font (14.0f); }

void LookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool)
{
    auto box = juce::Rectangle<float> (0, 0, 18, 18).withCentre ({ 10.0f, (float) b.getHeight() / 2 });
    const bool on = b.getToggleState();
    g.setColour (on ? colours::accent : juce::Colour (over ? 0x24ffffff : 0x14ffffff));
    g.fillRoundedRectangle (box, 5);
    if (! on) { g.setColour (colours::strokeHi); g.drawRoundedRectangle (box.reduced (0.5f), 5, 1); }
    else
    {
        juce::Path tick;
        tick.startNewSubPath (box.getX() + 4.5f, box.getCentreY());
        tick.lineTo (box.getX() + 7.8f, box.getBottom() - 5);
        tick.lineTo (box.getRight() - 4.5f, box.getY() + 5);
        g.setColour (juce::Colours::white);
        g.strokePath (tick, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.setColour (b.isEnabled() ? colours::text : colours::text3);
    g.setFont (font (13.5f));
    g.drawText (b.getButtonText(), b.getLocalBounds().withTrimmedLeft (28), juce::Justification::centredLeft, true);
}

void LookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, 6);
    g.setColour (colours::stroke);
    g.drawRoundedRectangle (r, 6, 1);
    juce::Path arrow;
    const auto ax = (float) w - 14, ay = (float) h / 2;
    arrow.startNewSubPath (ax - 4, ay - 2); arrow.lineTo (ax, ay + 2); arrow.lineTo (ax + 4, ay - 2);
    g.setColour (colours::text2);
    g.strokePath (arrow, juce::PathStrokeType (1.5f));
}

void LookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    g.setColour (colours::elevated);
    g.fillRoundedRectangle (0, 0, (float) w, (float) h, 6);
    g.setColour (colours::strokeHi);
    g.drawRoundedRectangle (0.5f, 0.5f, (float) w - 1, (float) h - 1, 6, 1);
    g.setColour (colours::text);
    g.setFont (font (13));
    g.drawFittedText (text, juce::Rectangle<int> (w, h).reduced (8, 4), juce::Justification::centredLeft, 3);
}

void LookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                                 int thumbStart, int thumbSize, bool over, bool down)
{
    juce::Rectangle<float> thumb = vertical ? juce::Rectangle<float> ((float) x + 2, (float) thumbStart, (float) w - 4, (float) thumbSize)
                                            : juce::Rectangle<float> ((float) thumbStart, (float) y + 2, (float) thumbSize, (float) h - 4);
    g.setColour (juce::Colour (over || down ? 0x55ffffff : 0x2affffff));
    g.fillRoundedRectangle (thumb, juce::jmin (thumb.getWidth(), thumb.getHeight()) / 2);
}

//==============================================================================
PillButton::PillButton (const juce::String& label, juce::Colour c) : juce::Button (label), onColour (c)
{
    setClickingTogglesState (false);
}

void PillButton::paintButton (juce::Graphics& g, bool over, bool)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    const bool on = getToggleState();
    if (on)
    {
        g.setColour (onColour.withAlpha (0.45f));
        g.fillRoundedRectangle (r.expanded (1.5f), 7);
        g.setColour (onColour);
        g.fillRoundedRectangle (r, 6);
    }
    else
    {
        g.setColour (juce::Colour (over ? 0x1fffffff : 0x0fffffff));
        g.fillRoundedRectangle (r, 6);
        g.setColour (colours::stroke);
        g.drawRoundedRectangle (r, 6, 1);
    }
    g.setColour (on ? juce::Colour (0xd9000000) : colours::text2);
    g.setFont (font (r.getHeight() * 0.48f, true));
    g.drawText (getName(), r, juce::Justification::centred);
}

//==============================================================================
IconButton::IconButton (Icon i, const juce::String& tip) : juce::Button (tip), icon (i)
{
    setTooltip (tip);
}

juce::Path IconButton::path (Icon i, juce::Rectangle<float> r)
{
    juce::Path p;
    const auto x = r.getX(), y = r.getY(), w = r.getWidth(), h = r.getHeight();
    auto line = [&] (float x1, float y1, float x2, float y2) { p.startNewSubPath (x + x1 * w, y + y1 * h); p.lineTo (x + x2 * w, y + y2 * h); };
    switch (i)
    {
        case Icon::play:     p.addTriangle (x + 0.2f * w, y, x + 0.2f * w, y + h, x + w, y + h / 2); break;
        case Icon::stop:     p.addRoundedRectangle (r.reduced (w * 0.08f), w * 0.12f); break;
        case Icon::record:   p.addEllipse (r.reduced (w * 0.15f)); break;
        case Icon::toStart:  p.addRectangle (x, y, w * 0.14f, h); p.addTriangle (x + w, y, x + w, y + h, x + w * 0.2f, y + h / 2); break;
        case Icon::prev:     line (0.55f, 0.1f, 0.15f, 0.5f); line (0.15f, 0.5f, 0.55f, 0.9f); line (0.95f, 0.1f, 0.55f, 0.5f); line (0.55f, 0.5f, 0.95f, 0.9f); break;
        case Icon::next:     line (0.45f, 0.1f, 0.85f, 0.5f); line (0.85f, 0.5f, 0.45f, 0.9f); line (0.05f, 0.1f, 0.45f, 0.5f); line (0.45f, 0.5f, 0.05f, 0.9f); break;
        case Icon::loop:     line (0.1f, 0.55f, 0.1f, 0.35f); line (0.1f, 0.35f, 0.85f, 0.35f); line (0.7f, 0.18f, 0.88f, 0.35f); line (0.88f, 0.35f, 0.7f, 0.52f);
                             line (0.9f, 0.45f, 0.9f, 0.65f); line (0.9f, 0.65f, 0.15f, 0.65f); line (0.3f, 0.48f, 0.12f, 0.65f); line (0.12f, 0.65f, 0.3f, 0.82f); break;
        case Icon::metronome: p.addTriangle (x + 0.3f * w, y + 0.05f * h, x + 0.7f * w, y + 0.05f * h, x + 0.95f * w, y + h);
                              p.addTriangle (x + 0.3f * w, y + 0.05f * h, x + 0.95f * w, y + h, x + 0.05f * w, y + h); break;
        case Icon::undo:     line (0.4f, 0.15f, 0.1f, 0.4f); line (0.1f, 0.4f, 0.4f, 0.65f); line (0.1f, 0.4f, 0.65f, 0.4f);
                             p.addCentredArc (x + 0.65f * w, y + 0.62f * h, 0.25f * w, 0.22f * h, 0, 0, juce::MathConstants<float>::pi, true); break;
        case Icon::redo:     line (0.6f, 0.15f, 0.9f, 0.4f); line (0.9f, 0.4f, 0.6f, 0.65f); line (0.9f, 0.4f, 0.35f, 0.4f);
                             p.addCentredArc (x + 0.35f * w, y + 0.62f * h, 0.25f * w, 0.22f * h, 0, 0, -juce::MathConstants<float>::pi, true); break;
        case Icon::importAudio: line (0.5f, 0.05f, 0.5f, 0.7f); line (0.25f, 0.45f, 0.5f, 0.7f); line (0.5f, 0.7f, 0.75f, 0.45f); line (0.1f, 0.92f, 0.9f, 0.92f); break;
        case Icon::exportAudio: line (0.5f, 0.75f, 0.5f, 0.08f); line (0.25f, 0.33f, 0.5f, 0.08f); line (0.5f, 0.08f, 0.75f, 0.33f); line (0.1f, 0.92f, 0.9f, 0.92f); break;
        case Icon::settings: for (int k = 0; k < 8; ++k)
                             {
                                 const auto a = k * juce::MathConstants<float>::pi / 4;
                                 p.startNewSubPath (r.getCentreX() + std::cos (a) * w * 0.28f, r.getCentreY() + std::sin (a) * h * 0.28f);
                                 p.lineTo (r.getCentreX() + std::cos (a) * w * 0.48f, r.getCentreY() + std::sin (a) * h * 0.48f);
                             }
                             p.addEllipse (r.reduced (w * 0.25f)); break;
        case Icon::mixer:    line (0.2f, 0.05f, 0.2f, 0.95f); line (0.5f, 0.05f, 0.5f, 0.95f); line (0.8f, 0.05f, 0.8f, 0.95f);
                             p.addRectangle (x + 0.08f * w, y + 0.55f * h, 0.24f * w, 0.14f * h); p.addRectangle (x + 0.38f * w, y + 0.25f * h, 0.24f * w, 0.14f * h);
                             p.addRectangle (x + 0.68f * w, y + 0.65f * h, 0.24f * w, 0.14f * h); break;
        case Icon::inspector: p.addRoundedRectangle (r, w * 0.12f); line (0.62f, 0.0f, 0.62f, 1.0f); break;
        case Icon::home:     for (int k = 0; k < 4; ++k) p.addRoundedRectangle (x + (k % 2) * 0.55f * w, y + (k / 2) * 0.55f * h, 0.42f * w, 0.42f * h, 0.08f * w); break;
        case Icon::plus:     line (0.5f, 0.1f, 0.5f, 0.9f); line (0.1f, 0.5f, 0.9f, 0.5f); break;
        case Icon::zoomIn:   p.addEllipse (x, y, w * 0.7f, h * 0.7f); line (0.62f, 0.62f, 0.95f, 0.95f); line (0.35f, 0.18f, 0.35f, 0.52f); line (0.18f, 0.35f, 0.52f, 0.35f); break;
        case Icon::zoomOut:  p.addEllipse (x, y, w * 0.7f, h * 0.7f); line (0.62f, 0.62f, 0.95f, 0.95f); line (0.18f, 0.35f, 0.52f, 0.35f); break;
        case Icon::snap:     for (int k = 0; k < 3; ++k) { line (0.15f + k * 0.35f, 0.05f, 0.15f + k * 0.35f, 0.95f); line (0.05f, 0.15f + k * 0.35f, 0.95f, 0.15f + k * 0.35f); } break;
        case Icon::follow:   line (0.05f, 0.5f, 0.7f, 0.5f); line (0.45f, 0.25f, 0.7f, 0.5f); line (0.7f, 0.5f, 0.45f, 0.75f); line (0.92f, 0.1f, 0.92f, 0.9f); break;
        case Icon::panic:    p.addStar (r.getCentre(), 8, w * 0.25f, w * 0.48f); break;
        case Icon::piano:    p.addRoundedRectangle (r.reduced (0.5f), 2); line (0.35f, 0.0f, 0.35f, 0.6f); line (0.65f, 0.0f, 0.65f, 0.6f); break;
        case Icon::guide:    p.addEllipse (x, y + 0.1f * h, w * 0.4f, h * 0.4f); p.addPieSegment (x - 0.05f * w, y + 0.55f * h, w * 0.5f, h * 0.8f, -1.57f, 1.57f, 0);
                             p.addCentredArc (x + 0.55f * w, y + 0.35f * h, 0.2f * w, 0.25f * h, 0, 0.5f, 2.6f, true); break;
        case Icon::speaker:  p.addTriangle (x, y + 0.35f * h, x + 0.45f * w, y, x + 0.45f * w, y + h); p.addRectangle (x, y + 0.32f * h, 0.25f * w, 0.36f * h);
                             p.addCentredArc (x + 0.55f * w, y + 0.5f * h, 0.25f * w, 0.3f * h, 0, 0.6f, 2.5f, true); break;
    }
    return p;
}

void IconButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (1);
    const auto isOn = active || getToggleState();
    if (primary)
    {
        g.setColour (colours::accent.withAlpha (isOn ? 0.55f : 0.3f));
        g.fillEllipse (r.expanded (1));
        g.setGradientFill (accentGradient (r));
        g.fillEllipse (r.reduced (2));
    }
    else if (icon == Icon::record)
    {
        g.setColour (colours::record.withAlpha (isOn ? 0.3f : (over ? 0.15f : 0.08f)));
        g.fillEllipse (r);
        g.setColour (colours::record.withAlpha (isOn ? 0.9f : 0.35f));
        g.drawEllipse (r.reduced (0.5f), 1.0f);
        g.setColour (colours::record);
        g.fillEllipse (r.withSizeKeepingCentre (r.getWidth() * 0.42f, r.getHeight() * 0.42f));
        return;
    }
    else
    {
        g.setColour (isOn ? activeColour.withAlpha (0.18f) : juce::Colour (over ? 0x18ffffff : 0x0dffffff));
        g.fillEllipse (r);
        g.setColour (isOn ? activeColour.withAlpha (0.6f) : colours::stroke);
        g.drawEllipse (r.reduced (0.5f), 1.0f);
    }
    if (down) r = r.reduced (1);
    const auto inner = r.withSizeKeepingCentre (r.getWidth() * 0.4f, r.getHeight() * 0.4f);
    auto p = path (icon, inner);
    const auto c = primary ? juce::Colours::white : (isOn ? activeColour : colours::text.withAlpha (isEnabled() ? 0.85f : 0.3f));
    g.setColour (c);
    const bool filled = icon == Icon::play || icon == Icon::stop || icon == Icon::toStart || icon == Icon::metronome
                     || icon == Icon::home || icon == Icon::panic || icon == Icon::speaker;
    if (filled) g.fillPath (p);
    else g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

} // namespace wdaw::ui
