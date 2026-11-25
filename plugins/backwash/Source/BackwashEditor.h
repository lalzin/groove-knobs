#pragma once

#include "BackwashProcessor.h"

namespace bw
{
class SwellMascot;
class BounceChip;
class DropOverlay;

/** Backwash UI: the Groove Knobs editor, "retro surf" theme, LONG / SHIMMER
    chips, a knob whose arc breathes with each swell, and Swell. A sound file
    dropped anywhere on it is bounced with its swells (the WAV chip). */
class BackwashEditor final : public gk::OneKnobEditor, public juce::FileDragAndDropTarget
{
public:
    explicit BackwashEditor (BackwashProcessor& p);
    ~BackwashEditor() override;

    BounceChip& getBounceChip() noexcept { return *bounceChip; }

    // FileDragAndDropTarget
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;
    /** Screenshots: shows the drop overlay. */
    void showDropOverlay (bool on);

    static gk::Palette makePalette();

    /** Screenshots: fixes the breath (0..1), -1 = live. */
    void setInhaleOverride (float inhale);

protected:
    void editorTick (double dt) override;

private:
    BackwashProcessor& proc;
    std::unique_ptr<BounceChip> bounceChip;
    std::unique_ptr<DropOverlay> dropOverlay;
    float inhaleOverride = -1.0f;
};
} // namespace bw
