#pragma once

#include "SingItBackProcessor.h"

namespace sib
{
/** Sing It Back UI ("Stage" theme). DSP stage: the shared editor with the
    theme colours; the scale bar, the advanced panel and Echo come with the
    UI stage. */
class SingItBackEditor final : public gk::OneKnobEditor
{
public:
    explicit SingItBackEditor (SingItBackProcessor& p);
    static gk::Palette makePalette();
};
} // namespace sib
