#pragma once

#include "Shared.h"

namespace wdaw::ui
{

class TimelineContent;
class RulerBar;
class HeadersContent;

/** Vista de arreglo: cabeceras de pista a la izquierda, regla con secciones arriba y la línea de tiempo. */
class Arrangement : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    explicit Arrangement (Context&);
    ~Arrangement() override;

    void resized() override;
    void paint (juce::Graphics&) override;
    void zoom (double factor, std::optional<int> anchorX = {});
    void refresh();

    static constexpr int rulerHeight = 50;
    static constexpr int headerWidth = 236;
    int trackHeight = 78;

    Context& ctx;
    juce::Viewport timelineView, headersView;
    std::unique_ptr<TimelineContent> timeline;
    std::unique_ptr<HeadersContent> headers;
    std::unique_ptr<RulerBar> ruler;

    struct Playhead : juce::Component { void paint (juce::Graphics&) override; } playheadLine, editCursorLine;

private:
    class Corner;
    std::unique_ptr<Corner> corner;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void syncScroll();
    int lastScrollX = -1, lastScrollY = -1;
};

} // namespace wdaw::ui
