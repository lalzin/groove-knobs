#pragma once

#include "BackwashProcessor.h"

namespace bw
{
class SwellMascot;

/** Backwash UI: the Groove Knobs editor, "retro surf" theme, LONG / SHIMMER
    chips, a knob whose arc breathes with each swell, and Swell. */
class BackwashEditor final : public gk::OneKnobEditor
{
public:
    explicit BackwashEditor (BackwashProcessor& p);

    static gk::Palette makePalette();

    /** Screenshots: fixes the breath (0..1), -1 = live. */
    void setInhaleOverride (float inhale);

protected:
    void editorTick (double dt) override;

private:
    BackwashProcessor& proc;
    float inhaleOverride = -1.0f;
};
} // namespace bw
