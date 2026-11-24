#include "HomeView.h"
#include "Model/Serialization.h"

namespace wdaw::ui
{

HomeView::HomeView (AppSettings& s) : settings (s)
{
    newButton.setButtonText ("+ Nuevo proyecto");
    newButton.setColour (juce::TextButton::buttonColourId, colours::accent);
    newButton.onClick = [this] { showNewProjectDialog ([this] (auto n, auto t, auto b, auto k, auto m) { if (onCreate) onCreate (n, t, b, k, m); }); };
    openButton.setButtonText ("Abrir…"_u);
    openButton.onClick = [this]
    {
        auto chooser = std::make_shared<juce::FileChooser> ("Abrir proyecto", settings.projectsFolder(), "*.worship;project.json");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories | juce::FileBrowserComponent::canSelectFiles,
                              [this, chooser] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f == juce::File()) return;
            if (f.getFileName() == "project.json") f = f.getParentDirectory();
            if (onOpen) onOpen (f);
        });
    };
    prefsButton.onClick = [this] { if (onPreferences) onPreferences(); };
    for (auto* b : { &newButton, &openButton, &prefsButton }) addAndMakeVisible (*b);
    refresh();
}

void HomeView::refresh()
{
    projects = io::listProjects (settings.projectsFolder());
    resized();
    repaint();
}

void HomeView::resized()
{
    auto r = getLocalBounds().reduced (40, 30);
    auto header = r.removeFromTop (60);
    newButton.setBounds (header.removeFromRight (170).withSizeKeepingCentre (170, 38));
    header.removeFromRight (8);
    openButton.setBounds (header.removeFromRight (100).withSizeKeepingCentre (100, 38));
    header.removeFromRight (8);
    prefsButton.setBounds (header.removeFromRight (130).withSizeKeepingCentre (130, 38));

    r.removeFromTop (40);
    templateCards.clear();
    const int cols = juce::jmax (1, juce::jmin (5, r.getWidth() / 220));
    const int cw = (r.getWidth() - (cols - 1) * 12) / cols;
    auto row = r.removeFromTop (118);
    for (int i = 0; i < 5; ++i)
    {
        if (i > 0 && i % cols == 0) { r.removeFromTop (12); row = r.removeFromTop (118); }
        templateCards.push_back (row.removeFromLeft (cw));
        row.removeFromLeft (12);
    }
    r.removeFromTop (56);
    projectCards.clear();
    const int pcols = juce::jmax (1, r.getWidth() / 280);
    const int pw = (r.getWidth() - (pcols - 1) * 12) / pcols;
    auto prow = r.removeFromTop (72);
    for (size_t i = 0; i < projects.size(); ++i)
    {
        if (i > 0 && (int) i % pcols == 0) { r.removeFromTop (12); if (r.getHeight() < 72) break; prow = r.removeFromTop (72); }
        projectCards.push_back (prow.removeFromLeft (pw));
        prow.removeFromLeft (12);
    }
}

void HomeView::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
    // Halo de color de fondo
    g.setGradientFill (juce::ColourGradient (colours::accent.withAlpha (0.20f), 0, 0, colours::accent.withAlpha (0.0f), 700, 500, true));
    g.fillRect (getLocalBounds());
    g.setGradientFill (juce::ColourGradient (colours::accent2.withAlpha (0.10f), (float) getWidth(), 0, colours::accent2.withAlpha (0.0f), (float) getWidth() - 600, 420, true));
    g.fillRect (getLocalBounds());

    auto r = getLocalBounds().reduced (40, 30);
    auto header = r.removeFromTop (60);
    auto logo = header.removeFromLeft (56).withSizeKeepingCentre (52, 52).toFloat();
    g.setColour (colours::accent.withAlpha (0.4f));
    g.fillRoundedRectangle (logo.expanded (3), 15);
    g.setGradientFill (accentGradient (logo));
    g.fillRoundedRectangle (logo, 14);
    g.setColour (juce::Colours::white);
    const float bars[] = { 0.3f, 0.55f, 0.85f, 1.0f, 0.85f, 0.55f, 0.3f };
    for (int i = 0; i < 7; ++i)
    {
        const auto h = 26.0f * bars[i];
        g.fillRoundedRectangle (logo.getX() + 10 + i * 4.8f, logo.getCentreY() - h / 2, 2.6f, h, 1.3f);
    }
    header.removeFromLeft (14);
    g.setColour (colours::text);
    g.setFont (font (28, true));
    g.drawText ("Worship DAW", header.removeFromTop (34), juce::Justification::bottomLeft);
    g.setColour (colours::text2);
    g.setFont (font (13.5f));
    g.drawText (juce::String::fromUTF8 ("Producción para equipos de alabanza · Mac y Windows"), header, juce::Justification::topLeft);

    r.removeFromTop (14);
    g.setColour (colours::text2);
    g.setFont (font (13, true));
    g.drawText ("Empezar", r.removeFromTop (20), juce::Justification::centredLeft);

    for (int i = 0; i < (int) templateCards.size(); ++i)
    {
        auto c = templateCards[(size_t) i].toFloat();
        g.setColour (i == hoverTemplate ? colours::elevated : colours::panel);
        g.fillRoundedRectangle (c, 14);
        g.setColour (i == hoverTemplate ? colours::accent.withAlpha (0.5f) : colours::stroke);
        g.drawRoundedRectangle (c.reduced (0.5f), 14, 1);
        auto inner = c.reduced (16, 14);
        auto iconBox = inner.removeFromTop (26).withWidth (26);
        g.setGradientFill (accentGradient (iconBox));
        g.fillRoundedRectangle (iconBox, 7);
        static const IconButton::Icon icons[] = { IconButton::Icon::plus, IconButton::Icon::mixer, IconButton::Icon::guide,
                                                  IconButton::Icon::speaker, IconButton::Icon::home };
        g.setColour (juce::Colours::white);
        auto glyph = IconButton::path (icons[i], iconBox.reduced (7));
        if (icons[i] == IconButton::Icon::home || icons[i] == IconButton::Icon::speaker) g.fillPath (glyph);
        else g.strokePath (glyph, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        inner.removeFromTop (10);
        g.setColour (colours::text);
        g.setFont (font (15, true));
        g.drawText (edit::displayName ((edit::Template) i), inner.removeFromTop (22), juce::Justification::centredLeft);
        g.setColour (colours::text2);
        g.setFont (font (12));
        g.drawFittedText (edit::subtitle ((edit::Template) i), inner.toNearestInt(), juce::Justification::topLeft, 2);
    }

    auto pr = getLocalBounds().reduced (40, 30);
    pr.setY (templateCards.empty() ? 200 : templateCards.back().getBottom() + 30);
    g.setColour (colours::text2);
    g.setFont (font (13, true));
    g.drawText ("Proyectos  ·  "_u + settings.projectsFolder().getFullPathName(), pr.removeFromTop (20), juce::Justification::centredLeft, true);
    if (projects.empty())
    {
        g.setColour (colours::text3);
        g.setFont (font (13));
        g.drawText (juce::String::fromUTF8 ("Todavía no hay proyectos. Crea uno desde una plantilla."), pr.removeFromTop (40), juce::Justification::centredLeft);
    }
    for (size_t i = 0; i < projectCards.size() && i < projects.size(); ++i)
    {
        auto c = projectCards[i].toFloat();
        auto& p = projects[i];
        g.setColour ((int) i == hoverProject ? colours::elevated : colours::panel);
        g.fillRoundedRectangle (c, 12);
        g.setColour (colours::stroke);
        g.drawRoundedRectangle (c.reduced (0.5f), 12, 1);
        auto badge = c.reduced (12).removeFromLeft (46).withSizeKeepingCentre (46, 46);
        g.setGradientFill (juce::ColourGradient (colours::accent.withAlpha (0.5f), badge.getTopLeft(), colours::accent2.withAlpha (0.3f), badge.getBottomRight(), false));
        g.fillRoundedRectangle (badge, 10);
        g.setColour (juce::Colours::white);
        g.setFont (font (14, true));
        g.drawText (p.key, badge, juce::Justification::centred);
        auto text = c.reduced (12).withTrimmedLeft (58);
        g.setColour (colours::text);
        g.setFont (font (14, true));
        g.drawText (p.name, text.removeFromTop (20), juce::Justification::centredLeft, true);
        g.setColour (colours::text2);
        g.setFont (font (11.5f));
        g.drawText (juce::String ((int) p.bpm) + " BPM · "_u + juce::String (p.trackCount) + " pistas", text.removeFromTop (16), juce::Justification::centredLeft);
        g.setColour (colours::text3);
        g.setFont (font (10.5f));
        g.drawText (p.modified.formatted ("%d/%m/%Y %H:%M"), text, juce::Justification::centredLeft);
    }
}

void HomeView::mouseMove (const juce::MouseEvent& e)
{
    int t = -1, p = -1;
    for (size_t i = 0; i < templateCards.size(); ++i) if (templateCards[i].contains (e.getPosition())) t = (int) i;
    for (size_t i = 0; i < projectCards.size(); ++i) if (projectCards[i].contains (e.getPosition())) p = (int) i;
    if (t != hoverTemplate || p != hoverProject) { hoverTemplate = t; hoverProject = p; repaint(); }
    setMouseCursor (t >= 0 || p >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void HomeView::mouseDown (const juce::MouseEvent& e)
{
    for (size_t i = 0; i < templateCards.size(); ++i)
        if (templateCards[i].contains (e.getPosition()))
        {
            showNewProjectDialog ([this] (auto n, auto t, auto b, auto k, auto m) { if (onCreate) onCreate (n, t, b, k, m); }, (edit::Template) i);
            return;
        }
    for (size_t i = 0; i < projectCards.size() && i < projects.size(); ++i)
        if (projectCards[i].contains (e.getPosition()))
        {
            if (e.mods.isPopupMenu()) showProjectMenu ((int) i);
            else if (onOpen) onOpen (projects[i].folder);
            return;
        }
}

void HomeView::showProjectMenu (int index)
{
    auto entry = projects[(size_t) index];
    juce::PopupMenu m;
    m.addItem ("Abrir", [this, entry] { if (onOpen) onOpen (entry.folder); });
    m.addItem ("Duplicar", [this, entry]
    {
        auto dest = io::createProjectFolder (settings.projectsFolder(), entry.name + " copia");
        dest.deleteRecursively();
        entry.folder.copyDirectoryTo (dest);
        if (auto p = io::load (dest)) { p->id = juce::Uuid(); p->name = dest.getFileNameWithoutExtension(); io::save (*p, dest); }
        refresh();
    });
    m.addItem ("Mostrar en la carpeta", [entry] { entry.folder.revealToUser(); });
    m.addSeparator();
    m.addItem ("Mover a la Papelera…"_u, [this, entry]
    {
        confirm ("Mover a la Papelera", juce::String::fromUTF8 ("¿Mover \"") + entry.name + juce::String::fromUTF8 ("\" a la Papelera? Se mueve con todo su audio; puedes recuperarlo desde la Papelera."),
                 "Mover a la Papelera", [this, entry] { io::moveToTrash (entry.folder); refresh(); });
    });
    m.showMenuAsync (juce::PopupMenu::Options());
}

} // namespace wdaw::ui
