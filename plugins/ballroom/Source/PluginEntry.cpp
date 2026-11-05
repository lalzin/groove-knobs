#include "BallroomProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new bl::BallroomProcessor();
}
