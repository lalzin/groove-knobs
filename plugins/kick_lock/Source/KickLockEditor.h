#pragma once

#include "KickLockProcessor.h"

namespace kl
{
/** Kick Lock UI, 760 x 480 (75..200 %, fixed ratio):

        made by LALZIN            KICK LOCK                  [● status]  (power)
        ┌───────────┬──────────────────────────────────────┬─────────────────┐
        │ phase     │ OSCILLOSCOPE: the last kicks overlaid │ SHAPE  RELEASE  │
        │ gauge     │ (sub cyan, kick orange, target line)  │ TARGET  OFFSET  │
        │  Phaze    │ + the last 2 s underneath             │ SUB CUT WAVE ST │
        │           ├──────────────────────────────────────┤      DEPTH      │
        │ sub wave  │ THRESHOLD  ( DRY ◄ MIX ► WET )  LOCK  │                 │
        │           │                                  MODE │                 │
        └───────────┴──────────────────────────────────────┴─────────────────┘
        erreur · confiance · somme mono · f0 · latence            [Réinitialiser]

    Everything reads the engine's lock-free telemetry; no allocation per frame. */
class KickLockEditor final : public gk::OneKnobEditor
{
public:
    explicit KickLockEditor (KickLockProcessor& p);
    ~KickLockEditor() override;
    static gk::Palette makePalette();

    /** Screenshots: the mascot and the status show this status (-1 = live). */
    void setPreviewStatus (int status, int kicks);

protected:
    void editorTick (double dt) override;

private:
    class StatusPill;
    class PhaseGauge;
    class Scope;
    class SubWave;
    class MetricsBar;
    class LockButton;

    KickLockProcessor& proc;
    std::unique_ptr<StatusPill> status;
    std::unique_ptr<PhaseGauge> gauge;
    std::unique_ptr<Scope> scope;
    std::unique_ptr<SubWave> wave;
    std::unique_ptr<MetricsBar> metrics;
    std::unique_ptr<LockButton> lockButton;
    std::unique_ptr<juce::ButtonParameterAttachment> lockAttachment;
    std::unique_ptr<gk::ChoiceSelector> shape, release, mode, trigger;
    std::vector<std::unique_ptr<gk::SmallKnob>> knobs;
    std::unique_ptr<juce::Component> panelsHolder;
    int previewStatus = -1, frameCount = 0;
};
} // namespace kl
