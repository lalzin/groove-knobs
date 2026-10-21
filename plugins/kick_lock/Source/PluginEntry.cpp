#include "KickLockProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new kl::KickLockProcessor();
}
