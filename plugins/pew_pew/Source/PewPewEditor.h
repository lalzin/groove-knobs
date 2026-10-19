#pragma once

#include "PewPewProcessor.h"

namespace pp
{
/** Pew Pew UI ("Mirror" theme). DSP stage: the shared editor with the
    theme colours; the drum-skin knob, ROOM / ECHO and Tomi come with the UI
    stage. */
class PewPewEditor final : public gk::OneKnobEditor
{
public:
    explicit PewPewEditor (PewPewProcessor& p);
    static gk::Palette makePalette();
};
} // namespace pp
