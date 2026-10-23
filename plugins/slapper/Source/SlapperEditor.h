#pragma once

#include "SlapperProcessor.h"

namespace slapper
{
/** SLAPPER, one-knob layout (as Pew Pew, 420 x 520): Pluck on top, FUNK in
    the middle, ATTACK (finger -> slap -> pop) and MUTE on its left, TONE and
    VOL on its right, DRIVE bottom left, MONO | POLY bottom right, ENGINE
    (STRING | ANALOG | FM | PULSE) a column left of Pluck, the presets
    underneath; "⋯" opens the rest — PLAY (SLIDE, GHOST, HUMAN) and STRING
    (PICK, STRING, BODY). The
    other parameters come with the presets (and the host's automation).
    Amber, cream, wood brown, wine — no blue, teal, green or lime (the
    suite's other plugins). */
class SlapperEditor final : public gk::OneKnobEditor
{
public:
    explicit SlapperEditor (SlapperProcessor& p);
    ~SlapperEditor() override;
    static gk::Palette makePalette();

private:
    class Segments;
    class MoreButton;
    class SettingsPanel;
    std::vector<std::unique_ptr<gk::SmallKnob>> knobs;
    std::unique_ptr<Segments> mode, engineSwitch;
    std::unique_ptr<SettingsPanel> panel;
    std::unique_ptr<MoreButton> more;

public:
    /** Screenshots: the settings panel open. */
    void setPanelOpen (bool open);
};
} // namespace slapper
