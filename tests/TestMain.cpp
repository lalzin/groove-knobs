#include <catch2/catch_session.hpp>
#include <juce_events/juce_events.h>

int main (int argc, char* argv[])
{
#if JUCE_WINDOWS
    _putenv_s ("GK_NO_UPDATE_CHECK", "1"); // no network: screenshots and tests never depend on the website
#else
    setenv ("GK_NO_UPDATE_CHECK", "1", 1); // no network: screenshots and tests never depend on the website
#endif
    juce::ScopedJuceInitialiser_GUI juceInit;
    return Catch::Session().run (argc, argv);
}
