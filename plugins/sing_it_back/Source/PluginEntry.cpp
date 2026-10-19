#include "SingItBackProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new sib::SingItBackProcessor();
}
