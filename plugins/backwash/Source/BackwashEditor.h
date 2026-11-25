#pragma once

#include "BackwashProcessor.h"

namespace bw
{
class SwellMascot;
class BounceChip;

/** Backwash UI: the Groove Knobs editor, "retro surf" theme, LONG / SHIMMER
    chips, a knob whose arc breathes with each swell, and Swell. The WAV chip
    drags the swells of the bar heard into the DAW. */
class BackwashEditor final : public gk::OneKnobEditor
{
public:
    explicit BackwashEditor (BackwashProcessor& p);
    ~BackwashEditor() override;

    BounceChip& getBounceChip() noexcept { return *bounceChip; }

    static gk::Palette makePalette();

    /** Screenshots: fixes the breath (0..1), -1 = live. */
    void setInhaleOverride (float inhale);

protected:
    void editorTick (double dt) override;

private:
    BackwashProcessor& proc;
    std::unique_ptr<BounceChip> bounceChip;
    float inhaleOverride = -1.0f;
};
} // namespace bw
