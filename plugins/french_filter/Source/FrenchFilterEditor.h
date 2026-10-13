#pragma once

#include "FrenchFilterProcessor.h"

namespace ff
{
/** French Filter UI: the generic Groove Knobs editor with the French palette
    (charcoal, tricolour knob) and the beret mascot. */
class FrenchFilterEditor final : public gk::OneKnobEditor
{
public:
    explicit FrenchFilterEditor (FrenchFilterProcessor& p);

    static gk::Palette makePalette();
};
} // namespace ff
