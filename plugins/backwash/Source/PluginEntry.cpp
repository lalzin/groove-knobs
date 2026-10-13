#include "BackwashProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new bw::BackwashProcessor();
}
