#include "PluginManager.h"
#include "BuiltInProcessors.h"

namespace wdaw
{

PluginManager::PluginManager()
{
    formats.addDefaultFormats();   // VST3 (Mac/Windows/Linux) + AudioUnit (Mac)
}

juce::File PluginManager::settingsFolder()
{
    auto f = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
    #if JUCE_MAC
                 .getChildFile ("Application Support")
    #endif
                 .getChildFile ("Worship DAW");
    f.createDirectory();
    return f;
}

void PluginManager::loadKnownList()
{
    if (auto xml = juce::parseXML (settingsFolder().getChildFile ("plugins.xml")))
        known.recreateFromXml (*xml);
}

void PluginManager::saveKnownList()
{
    if (auto xml = known.createXml())
        xml->writeTo (settingsFolder().getChildFile ("plugins.xml"));
}

void PluginManager::scanAsync (std::function<void (juce::String, float)> onProgress, std::function<void()> onFinished)
{
    if (scanning.exchange (true)) return;

    struct ScanThread : juce::Thread
    {
        ScanThread (PluginManager& o, std::function<void (juce::String, float)> p, std::function<void()> f)
            : juce::Thread ("Escaneo de plugins"), owner (o), progress (std::move (p)), finished (std::move (f)) {}

        void run() override
        {
            const auto pedal = settingsFolder().getChildFile ("plugin-scan-crash.txt");
            for (int i = 0; i < owner.formats.getNumFormats() && ! threadShouldExit(); ++i)
            {
                auto* format = owner.formats.getFormat (i);
                juce::PluginDirectoryScanner scanner (owner.known, *format, format->getDefaultLocationsToSearch(),
                                                      true, pedal, true);
                juce::String name;
                while (! threadShouldExit())
                {
                    const auto p = scanner.getProgress();
                    juce::MessageManager::callAsync ([cb = progress, name, p] { if (cb) cb (name, p); });
                    if (! scanner.scanNextFile (true, name)) break;
                }
            }
            juce::MessageManager::callAsync ([this]
            {
                owner.saveKnownList();
                owner.scanning = false;
                if (finished) finished();
            });
        }

        PluginManager& owner;
        std::function<void (juce::String, float)> progress;
        std::function<void()> finished;
    };

    if (scanThread != nullptr) scanThread->stopThread (2000);
    scanThread = std::make_unique<ScanThread> (*this, std::move (onProgress), std::move (onFinished));
    scanThread->startThread();
}

std::vector<juce::PluginDescription> PluginManager::effects() const
{
    std::vector<juce::PluginDescription> list;
    for (auto& d : known.getTypes()) if (! d.isInstrument) list.push_back (d);
    std::sort (list.begin(), list.end(), [] (auto& a, auto& b)
    {
        const bool wa = a.manufacturerName.containsIgnoreCase ("worship"), wb = b.manufacturerName.containsIgnoreCase ("worship");
        if (wa != wb) return wa;                     // Worship Sounds primero
        return a.name.compareIgnoreCase (b.name) < 0;
    });
    return list;
}

std::vector<juce::PluginDescription> PluginManager::instruments() const
{
    std::vector<juce::PluginDescription> list;
    for (auto& d : known.getTypes()) if (d.isInstrument) list.push_back (d);
    std::sort (list.begin(), list.end(), [] (auto& a, auto& b) { return a.name.compareIgnoreCase (b.name) < 0; });
    return list;
}

PluginSlot PluginManager::makeSlot (const juce::PluginDescription& d)
{
    PluginSlot s;
    s.name = d.name;
    s.manufacturer = d.manufacturerName;
    if (auto xml = d.createXml()) s.descriptionXml = xml->toString (juce::XmlElement::TextFormat().singleLine());
    return s;
}

PluginSlot PluginManager::internalSlot (const juce::String& internalId)
{
    PluginSlot s;
    s.descriptionXml = "internal:" + internalId;
    s.manufacturer = "Worship DAW";
    if (internalId == "piano")     s.name = juce::String::fromUTF8 ("Piano básico");
    else if (internalId == "pad")  s.name = juce::String::fromUTF8 ("Pad básico");
    else if (internalId == "reverb") s.name = juce::String::fromUTF8 ("Reverb básica");
    else if (internalId == "delay")  s.name = juce::String::fromUTF8 ("Delay básico");
    else                           s.name = internalId;
    return s;
}

void PluginManager::prepare (juce::AudioProcessor& p, double sampleRate, int blockSize, bool isInstrument)
{
    // Estéreo de entrada y salida (los instrumentos sin entrada).
    juce::AudioProcessor::BusesLayout stereo;
    if (! isInstrument) stereo.inputBuses.add (juce::AudioChannelSet::stereo());
    stereo.outputBuses.add (juce::AudioChannelSet::stereo());
    if (p.checkBusesLayoutSupported (stereo)) p.setBusesLayout (stereo);
    p.setRateAndBufferSizeDetails (sampleRate, blockSize);
    p.prepareToPlay (sampleRate, blockSize);
}

PluginInstance::Ptr PluginManager::createInstanceSync (const PluginSlot& slot, double sampleRate, int blockSize, juce::String& error)
{
    std::unique_ptr<juce::AudioProcessor> proc;
    bool instrument = false;
    if (isInternal (slot))
    {
        const auto id = slot.descriptionXml.fromFirstOccurrenceOf ("internal:", false, false);
        if (id == "piano" || id == "pad")
        {
            proc = std::make_unique<BasicSynthProcessor> (id == "piano" ? BasicSynthProcessor::Kind::piano : BasicSynthProcessor::Kind::pad);
            instrument = true;
        }
        else if (id == "reverb") proc = std::make_unique<ReverbProcessor>();
        else if (id == "delay")  proc = std::make_unique<DelayProcessor>();
        else if (id.startsWith ("latency:"))
            proc = std::make_unique<LatencyTestProcessor> (id.fromFirstOccurrenceOf (":", false, false).getIntValue());
    }
    else
    {
        juce::PluginDescription desc;
        if (auto xml = juce::parseXML (slot.descriptionXml); xml == nullptr || ! desc.loadFromXml (*xml))
        {
            error = "El plugin \"" + slot.name + "\" no tiene una descripción válida."_u;
            return {};
        }
        instrument = desc.isInstrument;
        proc = formats.createPluginInstance (desc, sampleRate, blockSize, error);
    }
    if (proc == nullptr)
    {
        if (error.isEmpty()) error = juce::String::fromUTF8 ("No se encontró el plugin \"") + slot.name + "\".";
        return {};
    }
    if (slot.state.getSize() > 0)
        proc->setStateInformation (slot.state.getData(), (int) slot.state.getSize());
    prepare (*proc, sampleRate, blockSize, instrument);

    PluginInstance::Ptr inst (new PluginInstance());
    inst->slotId = slot.id;
    inst->name = slot.name;
    inst->isInstrument = instrument;
    inst->processor = std::move (proc);
    return inst;
}

void PluginManager::createInstance (const PluginSlot& slot, double sampleRate, int blockSize,
                                    std::function<void (PluginInstance::Ptr, juce::String)> done)
{
    if (isInternal (slot))
    {
        juce::String error;
        auto inst = createInstanceSync (slot, sampleRate, blockSize, error);
        done (inst, error);
        return;
    }
    juce::PluginDescription desc;
    if (auto xml = juce::parseXML (slot.descriptionXml); xml == nullptr || ! desc.loadFromXml (*xml))
    {
        done (nullptr, "El plugin \"" + slot.name + "\" no tiene una descripción válida."_u);
        return;
    }
    formats.createPluginInstanceAsync (desc, sampleRate, blockSize,
        [slot, sampleRate, blockSize, done, isInst = desc.isInstrument] (std::unique_ptr<juce::AudioPluginInstance> proc, const juce::String& error)
        {
            if (proc == nullptr) { done (nullptr, error.isNotEmpty() ? error : juce::String::fromUTF8 ("No se pudo cargar \"") + slot.name + "\"."); return; }
            if (slot.state.getSize() > 0)
                proc->setStateInformation (slot.state.getData(), (int) slot.state.getSize());
            prepare (*proc, sampleRate, blockSize, isInst);
            PluginInstance::Ptr inst (new PluginInstance());
            inst->slotId = slot.id;
            inst->name = slot.name;
            inst->isInstrument = isInst;
            inst->processor = std::move (proc);
            done (inst, {});
        });
}

} // namespace wdaw
