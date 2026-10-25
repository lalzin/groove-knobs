#pragma once

#include "BoogieTrioProcessor.h"

namespace bt
{
/** Boogie Trio UI: the Groove Knobs editor with the "70s sunset" palette,
    the VINTAGE / WIDE chips and Velvet (who becomes a trio). "⋯" (bottom
    right) opens the rest, sorted: VOICES (RATE, DEPTH, VIBRATO, DRIFT),
    COLOUR (TONE, LOW CUT, FEEDBACK, SPACE), MOTION (TREMOLO, PHASER, MIX,
    WIDTH). */
class BoogieTrioEditor final : public gk::OneKnobEditor
{
public:
    explicit BoogieTrioEditor (BoogieTrioProcessor& p);
    ~BoogieTrioEditor() override;

    static gk::Palette makePalette();

    /** Screenshots: the settings panel open. */
    void setPanelOpen (bool open);

private:
    class MoreButton;
    class SettingsPanel;
    std::unique_ptr<SettingsPanel> panel;
    std::unique_ptr<MoreButton> more;
};
} // namespace bt
