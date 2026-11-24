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
    static constexpr int headerWidth = 250;
    int trackHeight = 78;
    static constexpr int laneHeight = 58;

    /** Filas de la vista: cada pista y debajo sus carriles de automatización visibles. */
    struct Row { int track = -1; int lane = -1; int y = 0, h = 0; };
    std::vector<Row> rows;
    void computeLayout();
    int trackY (int track) const;
    int contentHeight() const { return rows.empty() ? 0 : rows.back().y + rows.back().h; }
    const Row* rowAt (float y) const;
    /** Pista de la fila en `y` (también si es uno de sus carriles); -1 si no hay. */
    int trackIndexAt (float y) const { auto* r = rowAt (y); return r != nullptr ? r->track : -1; }

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
    int valueTicks = 0;
};

} // namespace wdaw::ui
