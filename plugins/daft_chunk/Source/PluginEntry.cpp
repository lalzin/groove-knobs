#include "DaftChunkProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new daftchunk::DaftChunkProcessor();
}
