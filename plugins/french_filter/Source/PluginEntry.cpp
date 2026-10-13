#include "FrenchFilterProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ff::FrenchFilterProcessor();
}
