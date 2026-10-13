#pragma once

#include "BoogieTrioProcessor.h"

namespace bt
{
/** Boogie Trio UI: the Groove Knobs editor with the "70s sunset" palette,
    the VINTAGE / WIDE chips and Velvet (who becomes a trio). */
class BoogieTrioEditor final : public gk::OneKnobEditor
{
public:
    explicit BoogieTrioEditor (BoogieTrioProcessor& p);

    static gk::Palette makePalette();
};
} // namespace bt
