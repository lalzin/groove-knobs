#include "CrackleProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new crackle::CrackleProcessor();
}
