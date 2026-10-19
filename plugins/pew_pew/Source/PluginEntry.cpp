#include "PewPewProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new pp::PewPewProcessor();
}
