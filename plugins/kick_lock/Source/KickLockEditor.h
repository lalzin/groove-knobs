#pragma once

#include "KickLockProcessor.h"

namespace kl
{
/** DSP stage: JUCE's generic editor (every parameter), until the 760 x 480 UI. */
class KickLockEditor final : public juce::GenericAudioProcessorEditor
{
public:
    explicit KickLockEditor (KickLockProcessor& p) : juce::GenericAudioProcessorEditor (p) { setSize (520, 480); }
};
} // namespace kl
