#pragma once

#include "Session.h"

namespace wdaw::ui
{

/** Escucha los parámetros de los plugins de las pistas:
      - recuerda el último parámetro tocado (para "Automatización › Último parámetro tocado", como en REAPER);
      - en modo Touch/Latch, escribe en el carril lo que se mueve en la ventana del plugin.
    Solo cuenta los cambios hechos por una persona (entre inicio y fin de gesto, en el hilo de mensajes);
    los valores que pone la propia automatización no se escuchan. */
class ParamWatcher : private juce::Timer
{
public:
    explicit ParamWatcher (Session& s) : session (s) { startTimer (500); timerCallback(); }
    ~ParamWatcher() override { stopTimer(); watched.clear(); }

private:
    struct Watched : juce::AudioProcessorParameter::Listener
    {
        Watched (ParamWatcher& o, PluginInstance::Ptr p, juce::Uuid t) : owner (o), plugin (std::move (p)), track (t)
        {
            for (auto* prm : plugin->processor->getParameters()) prm->addListener (this);
        }
        ~Watched() override
        {
            for (auto* prm : plugin->processor->getParameters()) prm->removeListener (this);
        }
        AutomationTarget target (int index) const
        {
            auto& params = plugin->processor->getParameters();
            return { AutomationTarget::Kind::pluginParam, plugin->slotId, index, index < params.size() ? params[index]->getName (40) : juce::String() };
        }
        void parameterValueChanged (int index, float value) override
        {
            if (! juce::MessageManager::existsAndIsCurrentThread()) return;   // la automatización escribe desde el audio
            if (std::find (touching.begin(), touching.end(), index) == touching.end()) return;
            owner.session.setControl (track, target (index), value);
        }
        void parameterGestureChanged (int index, bool starting) override
        {
            if (! juce::MessageManager::existsAndIsCurrentThread()) return;
            const auto t = target (index);
            if (starting)
            {
                touching.push_back (index);
                owner.session.lastTouchedParam = Session::TouchedParam { track, t };
                owner.session.touchBegin (track, t);
            }
            else
            {
                touching.erase (std::remove (touching.begin(), touching.end(), index), touching.end());
                owner.session.touchEnd (track, t);
            }
        }
        ParamWatcher& owner;
        PluginInstance::Ptr plugin;
        juce::Uuid track;
        std::vector<int> touching;
    };

    void timerCallback() override
    {
        // Se engancha a los plugins cargados de cada pista (y se suelta de los que ya no están).
        std::map<juce::Uuid, std::pair<PluginInstance::Ptr, juce::Uuid>> wanted;
        for (auto& t : session.project().tracks)
        {
            std::vector<juce::Uuid> slots;
            if (t.instrument) slots.push_back (t.instrument->id);
            for (auto& s : t.inserts) slots.push_back (s.id);
            for (auto& id : slots)
                if (auto p = session.engine.findPlugin (id)) wanted[id] = { p, t.id };
        }
        for (auto it = watched.begin(); it != watched.end();)
        {
            auto w = wanted.find (it->first);
            if (w == wanted.end() || w->second.first != it->second->plugin || w->second.second != it->second->track) it = watched.erase (it);
            else ++it;
        }
        for (auto& [id, pair] : wanted)
            if (watched.count (id) == 0) watched[id] = std::make_unique<Watched> (*this, pair.first, pair.second);
    }

    Session& session;
    std::map<juce::Uuid, std::unique_ptr<Watched>> watched;
};

} // namespace wdaw::ui
