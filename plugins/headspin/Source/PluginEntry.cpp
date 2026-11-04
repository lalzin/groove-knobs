#include "HeadSpinProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new hs::HeadSpinProcessor();
}
