#pragma once

#include "Dialogs.h"
#include "Model/Serialization.h"

namespace wdaw::ui
{

class HomeView : public juce::Component
{
public:
    explicit HomeView (AppSettings&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void refresh();

    std::function<void (const juce::File&)> onOpen;
    std::function<void (juce::String, edit::Template, double, juce::String, int)> onCreate;
    std::function<void()> onPreferences;

private:
    AppSettings& settings;
    std::vector<io::Entry> projects;
    juce::TextButton newButton, openButton, prefsButton { "Preferencias" };
    std::vector<juce::Rectangle<int>> templateCards, projectCards;
    int hoverTemplate = -1, hoverProject = -1;
    void showProjectMenu (int index);
};

} // namespace wdaw::ui
