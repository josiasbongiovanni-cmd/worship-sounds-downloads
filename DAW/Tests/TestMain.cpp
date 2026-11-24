#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;   // MessageManager para las pruebas del motor
    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    const juce::String only = argc > 1 ? juce::String (argv[1]) : juce::String();
    const juce::StringArray categories = only.isNotEmpty() ? juce::StringArray (only) : juce::StringArray ("Model", "Engine");

    // Cada runTestsInCategory reinicia los resultados: se cuentan después de cada categoría.
    int failures = 0, passes = 0;
    for (auto& category : categories)
    {
        runner.runTestsInCategory (category);
        for (int i = 0; i < runner.getNumResults(); ++i)
        {
            failures += runner.getResult (i)->failures;
            passes += runner.getResult (i)->passes;
        }
    }
    std::cout << "\n" << passes << " comprobaciones correctas, " << failures << " fallidas\n";
    std::cout << (failures == 0 ? "TODAS LAS PRUEBAS PASARON\n" : "FALLARON PRUEBAS\n");
    return failures == 0 ? 0 : 1;
}
