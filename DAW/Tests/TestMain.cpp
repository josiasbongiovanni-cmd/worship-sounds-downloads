#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;   // MessageManager para las pruebas del motor
    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    juce::String category = argc > 1 ? juce::String (argv[1]) : juce::String();
    if (category.isNotEmpty()) runner.runTestsInCategory (category);
    else { runner.runTestsInCategory ("Model"); runner.runTestsInCategory ("Engine"); }

    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        failures += runner.getResult (i)->failures;
    std::cout << (failures == 0 ? "\nTODAS LAS PRUEBAS PASARON\n" : "\nFALLARON PRUEBAS\n");
    return failures == 0 ? 0 : 1;
}
