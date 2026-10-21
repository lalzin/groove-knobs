#include "KickSendProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new kls::KickSendProcessor();
}
