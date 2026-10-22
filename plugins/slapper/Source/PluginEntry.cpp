#include "SlapperProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new slapper::SlapperProcessor();
}
