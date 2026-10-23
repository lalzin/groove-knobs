#pragma once

#include "CrackleProcessor.h"

namespace crackle
{
/** CRACKLE, one-knob layout (as Sing It Back, 420 x 520): Hiss on top, GRAIN
    in the middle, MIX left, VOL right, the presets at the bottom, and "⋯"
    opening every setting sorted by kind of effect — TAPE (wow, flutter, hiss,
    dropouts), VINYL (crackle, noise level), RADIO (band, radio), SATURATION
    (amount, focus, type), DIGITAL (crush) — each effect's knob with its AUTO
    light (lit: it follows GRAIN; click it to give the knob back to GRAIN).
    Lilac, ink, paper, warm grey. */
class CrackleEditor final : public gk::OneKnobEditor
{
public:
    explicit CrackleEditor (CrackleProcessor& p);
    ~CrackleEditor() override;
    static gk::Palette makePalette();

private:
public:
    /** Screenshots: the settings panel open. */
    void setPanelOpen (bool open);

private:
    class Segments;
    class AutoLight;
    class MoreButton;
    class SettingsPanel;
    std::vector<std::unique_ptr<gk::SmallKnob>> frontKnobs;
    std::unique_ptr<SettingsPanel> panel;
    std::unique_ptr<MoreButton> more;
};
} // namespace crackle
